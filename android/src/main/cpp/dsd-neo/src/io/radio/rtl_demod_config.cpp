// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief RTL-SDR demodulation configuration helpers.
 *
 * Centralizes initialization and configuration of the demodulation state
 * used by the RTL-SDR stream pipeline, including mode selection, env/opts
 * driven DSP toggles, and rate-dependent helpers.
 */

#include <dsd-neo/io/rtl_demod_config.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/dsp/demod_pipeline.h>
#include <dsd-neo/dsp/demod_state.h>
#include <dsd-neo/dsp/fll.h>
#include <dsd-neo/dsp/math_utils.h>
#include <dsd-neo/dsp/resampler.h>
#include <dsd-neo/dsp/ted.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/log.h>
#include <dsd-neo/runtime/mem.h>
#include <dsd-neo/runtime/ring.h>
#include <dsd-neo/runtime/unicode.h>
#include <dsd-neo/runtime/worker_pool.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Debug/compat toggles via env (mirrored from rtl_sdr_fm.cpp responsibilities). */
int combine_rotate_enabled = 1;      /* DSD_NEO_COMBINE_ROT (1 default) */
int upsample_fixedpoint_enabled = 1; /* DSD_NEO_UPSAMPLE_FP (1 default) */

/* Allow disabling the fs/4 capture frequency shift via env for trunking/exact-center use cases. */
int disable_fs4_shift = 0; /* Set by env DSD_NEO_DISABLE_FS4_SHIFT=1 */

namespace {

enum DemodMode { DEMOD_DIGITAL = 0, DEMOD_ANALOG = 1, DEMOD_RO2 = 2 };

struct DemodInitParams {
    int deemph_default;
};

static void
demod_init_mode(struct demod_state* s, DemodMode mode, const DemodInitParams* p, int rtl_dsp_bw_hz,
                struct output_state* output) {
    /* Common defaults */
    s->rate_in = rtl_dsp_bw_hz;
    s->rate_out = rtl_dsp_bw_hz;
    s->squelch_level = 0.0f;
    s->conseq_squelch = 10;
    s->terminate_on_squelch = 0;
    s->squelch_hits = 11;
    s->downsample_passes = 0;
    s->post_downsample = 1;
    s->custom_atan = 0;
    s->deemph = 0;
    s->rate_out2 = -1;
    s->mode_demod = &dsd_fm_demod;
    s->pre_j = s->pre_r = 0.0f;
    s->prev_lpr_index = 0;
    s->deemph_a = 0.0f;
    s->deemph_avg = 0.0f;
    /* Channel LPF (post-HB). At 48 kHz with Blackman, up to 135 taps (hist = 134). */
    s->channel_lpf_enable = 0;     /* configured later by env/mode helper */
    s->channel_lpf_hist_len = 143; /* max history for 144-tap filter */
    s->channel_lpf_profile = DSD_CH_LPF_PROFILE_WIDE;
    for (int k = 0; k < 144; k++) {
        s->channel_lpf_hist_i[k] = 0;
        s->channel_lpf_hist_q[k] = 0;
    }
    s->channel_pwr = 0.0f;
    s->channel_squelch_level = 0.0f; /* 0 = disabled; set from opts later */
    s->channel_squelched = 0;
    /* Audio LPF defaults */
    s->audio_lpf_enable = 0;
    s->audio_lpf_alpha = 0.0f;
    s->audio_lpf_state = 0.0f;
    s->now_lpr = 0.0f;
    s->dc_block = 1;
    s->dc_avg = 0.0f;
    /* Resampler defaults */
    s->resamp_enabled = 0;
    s->resamp_target_hz = 0;
    s->resamp_L = 1;
    s->resamp_M = 1;
    s->resamp_phase = 0;
    s->resamp_taps_len = 0;
    s->resamp_taps_per_phase = 0;
    s->resamp_taps = NULL;
    s->resamp_hist = NULL;
    /* Post-demod audio polyphase decimator defaults */
    s->post_polydecim_enabled = 0;
    s->post_polydecim_M = 1;
    s->post_polydecim_K = 0;
    s->post_polydecim_hist_head = 0;
    s->post_polydecim_taps = NULL;
    s->post_polydecim_hist = NULL;
    /* FLL/TED defaults (GNU Radio-style native float) */
    s->fll_enabled = 0;
    s->fll_alpha = 0.0f;
    s->fll_beta = 0.0f;
    s->fll_freq = 0.0f;
    s->fll_phase = 0.0f;
    s->fll_deadband = 0.0f;
    s->fll_slew_max = 0.0f;
    s->fll_prev_r = 0.0f;
    s->fll_prev_j = 0.0f;
    s->ted_enabled = 0;
    s->ted_gain = 0.0f;
    s->ted_sps = 0;
    s->ted_sps_override = 0;
    s->costas_reset_pending = 0;
    s->ted_mu = 0.0f;
    s->sps_is_integer = 1; /* assume integer SPS until proven otherwise */
    /* Initialize FLL and TED module states */
    fll_init_state(&s->fll_state);
    ted_init_state(&s->ted_state);
    /* Squelch estimator init */
    s->squelch_running_power = 0;
    {
        /* Keep squelch timing roughly constant in seconds across rate changes.
           Baseline: 12 kHz -> stride 16, window 2048 (~170 ms). */
        const int base_fs = 12000;
        int stride = (s->rate_in > 0) ? (int)((int64_t)s->rate_in * 16 / base_fs) : 16;
        if (stride < 4) {
            stride = 4;
        } else if (stride > 256) {
            stride = 256;
        }
        s->squelch_decim_stride = stride;
        int window = (s->rate_in > 0) ? (int)((int64_t)s->rate_in * 2048 / base_fs) : 2048;
        if (window < 256) {
            window = 256;
        } else if (window > 32768) {
            window = 32768;
        }
        s->squelch_window = window;
    }
    s->squelch_decim_phase = 0;
    /* Squelch soft gate defaults */
    s->squelch_gate_open = 1;
    s->squelch_env = 1.0f;
    s->squelch_env_attack = 0.125f;
    s->squelch_env_release = 0.03125f;
    /* HB decimator histories */
    for (int st = 0; st < 10; st++) {
        memset(s->hb_hist_i[st], 0, sizeof(s->hb_hist_i[st]));
        memset(s->hb_hist_q[st], 0, sizeof(s->hb_hist_q[st]));
    }
    /* Input ring does not require double-buffer init */
    s->lowpassed = s->input_cb_buf;
    s->lp_len = 0;
    /* Reset IQ balance EMA */
    s->iqbal_alpha_ema_r = 0.0f;
    s->iqbal_alpha_ema_i = 0.0f;
    dsd_cond_init(&s->ready);
    dsd_mutex_init(&s->ready_m);
    s->output_target = output;

    /* FM AGC auto-tune per-instance state */
    s->fm_agc_ema_rms = 0.0;

    /* CQPSK path default (may be overridden by rtl_demod_config_from_env_and_opts
       based on mod_qpsk and DSD_NEO_CQPSK env). */
    s->cqpsk_enable = 0;
    /* CQPSK differential history: Initialize to (1, 0) not (0, 0).
     * When prev is (0, 0), the first diff decode produces zero output,
     * which corrupts the Costas phase error and causes the loop to hunt.
     * Using (1, 0) means the first sample's diff output equals raw input. */
    s->cqpsk_diff_prev_r = 1.0f;
    s->cqpsk_diff_prev_j = 0.0f;

    /* Mode-specific adjustments */
    if (mode == DEMOD_ANALOG) {
        s->downsample_passes = 1;
        s->deemph = 1;
    } else {
        s->downsample_passes = 0;
        s->deemph = (p && p->deemph_default) ? 1 : 0;
    }
    s->custom_atan = 0;
    s->rate_out2 = rtl_dsp_bw_hz;

    /* Legacy discriminator path removed; keep placeholders NULL. */
    s->discriminator = NULL;
    /* Initialize minimal worker pool (env-gated via DSD_NEO_MT). */
    demod_mt_init(s);

    /* Generic IQ balance defaults (image suppression); mode-aware guards in DSP pipeline.
       Start disabled so the UI/DSP menu fully controls this DSP block. */
    s->iqbal_enable = 0;
    s->iqbal_thr = 0.02f;
    s->iqbal_alpha_ema_r = 0.0f;
    s->iqbal_alpha_ema_i = 0.0f;
    s->iqbal_alpha_ema_a = 0.2f;
}

} // namespace

/**
 * @brief Initialize the demodulator for the requested mode and attach output ring.
 *
 * Chooses RO2/digital/analog initialization based on @p opts flags, seeds mode
 * defaults, primes the worker pool, and wires up the output ring target.
 * Returns immediately when inputs are NULL.
 *
 * @param demod         Demodulator state to initialize.
 * @param output        Output ring target for demodulated audio.
 * @param opts          Decoder options to derive mode/config flags.
 * @param rtl_dsp_bw_hz Baseband bandwidth in Hz for initial rate defaults.
 */
void
rtl_demod_init_for_mode(struct demod_state* demod, struct output_state* output, const dsd_opts* opts,
                        int rtl_dsp_bw_hz) {
    if (!demod || !output || !opts) {
        return;
    }

    DemodInitParams params = {};
    if (opts->frame_p25p1 == 1 || opts->frame_p25p2 == 1 || opts->frame_provoice == 1) {
        demod_init_mode(demod, DEMOD_RO2, &params, rtl_dsp_bw_hz, output);
    } else if (opts->analog_only == 1 || opts->m17encoder == 1) {
        params.deemph_default = 1;
        demod_init_mode(demod, DEMOD_ANALOG, &params, rtl_dsp_bw_hz, output);
    } else {
        demod_init_mode(demod, DEMOD_DIGITAL, &params, rtl_dsp_bw_hz, output);
    }
}

/**
 * @brief Apply environment/runtime overrides to the demodulator state.
 *
 * Mirrors CLI/env-driven configuration into the demodulator, covering DSP
 * toggles (FS/4 shift, combine-rotate), resampler targets, FLL/TED tuning,
 * CQPSK path enable, AGC knobs, and IQ balance defaults. Early-exits on NULL inputs.
 *
 * @param demod Demodulator state to configure.
 * @param opts  Decoder options used for runtime flags.
 */
void
rtl_demod_config_from_env_and_opts(struct demod_state* demod, dsd_opts* opts) {
    if (!demod || !opts) {
        return;
    }

    dsd_neo_config_init(opts);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (!cfg) {
        return;
    }

    if (cfg->combine_rot_is_set) {
        combine_rotate_enabled = (cfg->combine_rot != 0);
    }
    if (cfg->upsample_fp_is_set) {
        upsample_fixedpoint_enabled = (cfg->upsample_fp != 0);
    }

    if (cfg->fs4_shift_disable_is_set) {
        disable_fs4_shift = (cfg->fs4_shift_disable != 0);
    }

    /* rtltcp-specific sane defaults unless explicitly overridden via env/config */
    if (opts->rtltcp_enabled) {
        /* For rtl_tcp, prefer consistency with USB: allow fs/4 shift fallback when offset tuning unavailable */
        if (!cfg->fs4_shift_disable_is_set) {
            disable_fs4_shift = 0;
        }
    }

    int enable_resamp = 1;
    int target = 48000;
    if (cfg->resamp_is_set) {
        enable_resamp = cfg->resamp_disable ? 0 : 1;
        target = cfg->resamp_target_hz > 0 ? cfg->resamp_target_hz : 48000;
    }
    /* Defer resampler design until after capture settings establish actual rates */
    demod->resamp_target_hz = enable_resamp ? target : 0;
    demod->resamp_enabled = 0;

    /* Default: keep residual-CFO FLL off unless explicitly enabled via env/CLI/UI.
       CQPSK/QPSK uses a separate OP25 band-edge FLL block in the CQPSK chain. */
    int default_fll = 0;
    demod->fll_enabled = cfg->fll_is_set ? (cfg->fll_enable != 0) : default_fll;
    /* Native float FLL parameters (equivalent to legacy Q15 values) */
    demod->fll_alpha = cfg->fll_alpha_is_set ? cfg->fll_alpha : 0.0015f;          /* ~50/32768 */
    demod->fll_beta = cfg->fll_beta_is_set ? cfg->fll_beta : 0.00015f;            /* ~5/32768 */
    demod->fll_deadband = cfg->fll_deadband_is_set ? cfg->fll_deadband : 0.0086f; /* ~45/16384*π rad */
    demod->fll_slew_max = cfg->fll_slew_is_set ? cfg->fll_slew_max : 0.012f;      /* ~64/32768*2π rad */
    demod->fll_freq = 0.0f;
    demod->fll_phase = 0.0f;
    demod->fll_prev_r = demod->fll_prev_j = 0.0f;
    /* Costas loop state (OP25-aligned control loop)
       OP25 defaults from gardner_costas_cc.make():
         alpha = costas_alpha = 0.04
         beta = 0.125 * alpha * alpha = 0.0002
         fmax = 2*pi * 2400 / if_rate; for 24kHz: ~0.628 rad/sample */
    dsd_costas_loop_state_t* cl = &demod->costas_state;
    cl->phase = 0.0f;
    cl->freq = 0.0f;
    /* OP25: fmax = 2*pi * 2400 / if_rate; scale to current demod sample rate */
    const float kTwoPi = 6.28318530717958647692f;
    float if_rate = (demod->rate_out > 0) ? (float)demod->rate_out : 24000.0f;
    cl->max_freq = kTwoPi * 2400.0f / if_rate;
    cl->min_freq = -cl->max_freq;
    cl->loop_bw = cfg->costas_bw_is_set ? (float)cfg->costas_loop_bw : 0.008f; /* OP25 default (not used directly) */
    cl->damping = cfg->costas_damping_is_set ? (float)cfg->costas_damping : dsd_neo_costas_default_damping();
    /* OP25 direct parameters: alpha=0.04, beta=0.125*alpha^2=0.0002 */
    cl->alpha = 0.04f;
    cl->beta = 0.125f * cl->alpha * cl->alpha; /* 0.0002 */
    cl->error = 0.0f;
    cl->initialized = 0;
    demod->costas_err_avg_q14 = 0;

    /* Default: keep TED off unless explicitly enabled via env/CLI/UI.
       CQPSK/QPSK requires TED and is forced on when the CQPSK path is enabled. */
    int ted_default = 0;
    demod->ted_enabled = cfg->ted_is_set ? (cfg->ted_enable != 0) : ted_default;
    /* Native float TED gain (controls tracking aggressiveness, bounded internally)
       OP25 default: gain_mu = 0.025 */
    demod->ted_gain = cfg->ted_gain_is_set ? cfg->ted_gain : 0.025f;
    demod->ted_sps = 10;
    demod->ted_mu = 0.0f;
    demod->ted_force = cfg->ted_force_is_set ? (cfg->ted_force != 0) : 0;

    /* CQPSK path: auto-enable for QPSK modulation (P25 LSM/TDMA, etc.).
       Env DSD_NEO_CQPSK overrides: 1/y/t to force on, 0/n/f to force off. */
    int default_cqpsk = (opts->mod_qpsk == 1) ? 1 : 0;
    demod->cqpsk_enable = default_cqpsk;
    if (cfg->cqpsk_is_set) {
        demod->cqpsk_enable = (cfg->cqpsk_enable != 0) ? 1 : 0;
    }
    /* CQPSK Costas/differential stage assumes symbol-rate samples from the Gardner TED.
       Require TED whenever the CQPSK path is enabled so the pipeline never feeds
       oversampled I/Q into op25_diff_phasor_cc/op25_costas_loop_cc. Also set mode_demod so that
       full_demod's TED gate check (mode_demod != &dsd_fm_demod) passes. */
    if (demod->cqpsk_enable) {
        if (!demod->ted_enabled) {
            demod->ted_enabled = 1;
        }
        extern void qpsk_differential_demod(struct demod_state*);
        demod->mode_demod = &qpsk_differential_demod;
        /* Reset differential decode history to (1,0) not (0,0).
         * See comment in demod_init_mode for rationale. */
        demod->cqpsk_diff_prev_r = 1.0f;
        demod->cqpsk_diff_prev_j = 0.0f;
    }

    if (demod->cqpsk_enable) {
        demod->fll_enabled = 0;
    }

    /* FM/C4FM amplitude AGC (pre-discriminator): default OFF for all modes.
       Users can enable via env `DSD_NEO_FM_AGC=1` or the UI toggle. */
    int default_fm_agc = 0;
    demod->fm_agc_enable = cfg->fm_agc_is_set ? (cfg->fm_agc_enable != 0) : default_fm_agc;
    demod->fm_agc_target_rms = cfg->fm_agc_target_is_set ? cfg->fm_agc_target_rms : 0.30f;
    demod->fm_agc_min_rms = cfg->fm_agc_min_is_set ? cfg->fm_agc_min_rms : 0.06f;
    demod->fm_agc_alpha_up = cfg->fm_agc_alpha_up_is_set ? cfg->fm_agc_alpha_up : 0.25f;
    demod->fm_agc_alpha_down = cfg->fm_agc_alpha_down_is_set ? cfg->fm_agc_alpha_down : 0.75f;
    if (demod->fm_agc_gain <= 0.0f) {
        demod->fm_agc_gain = 1.0f; /* unity */
    }
    demod->fm_agc_ema_rms = (demod->fm_agc_target_rms > 0.0f) ? (double)demod->fm_agc_target_rms : 0.0;
    demod->fm_limiter_enable = cfg->fm_limiter_is_set ? (cfg->fm_limiter_enable != 0) : 0;
    demod->iq_dc_block_enable = cfg->iq_dc_block_is_set ? (cfg->iq_dc_block_enable != 0) : 0;
    demod->iq_dc_shift = cfg->iq_dc_shift_is_set ? cfg->iq_dc_shift : 11;
    demod->iq_dc_avg_r = demod->iq_dc_avg_i = 0;
    {
        const char* iqb = getenv("DSD_NEO_IQ_BALANCE");
        if (iqb && *iqb) {
            demod->iqbal_enable = (atoi(iqb) != 0) ? 1 : 0;
        }
    }

    /* Channel complex low-pass (post-HB, complex baseband).
       Default policy (Fs~=24 kHz RTL DSP baseband):
         - For analog-like modes, enable a wide channel LPF to narrow
           out-of-channel noise while preserving audio bandwidth.
         - For digital voice modes, use a mode-appropriate cutoff to balance
           noise rejection vs. modulation bandwidth.
       Env override:
         - DSD_NEO_CHANNEL_LPF=0 forces off (all modes).
         - DSD_NEO_CHANNEL_LPF!=0 forces on (all modes). */
    int channel_lpf = 0;
    int channel_lpf_profile = DSD_CH_LPF_PROFILE_WIDE;
    int high_fs = (demod->rate_in >= 20000); /* currently 24 kHz DSP baseband */
    if (cfg->channel_lpf_is_set) {
        channel_lpf = (cfg->channel_lpf_enable != 0);
    } else if (high_fs) {
        channel_lpf = 1;
    }
    if (channel_lpf) {
        if (opts->frame_p25p1 == 1 || opts->frame_p25p2 == 1) {
            channel_lpf_profile = demod->cqpsk_enable ? DSD_CH_LPF_PROFILE_P25_CQPSK : DSD_CH_LPF_PROFILE_P25_C4FM;
        } else if (opts->frame_provoice == 1) {
            channel_lpf_profile = DSD_CH_LPF_PROFILE_PROVOICE;
        } else if (opts->frame_nxdn48 == 1 || opts->frame_dpmr == 1 || opts->frame_dstar == 1) {
            channel_lpf_profile = DSD_CH_LPF_PROFILE_6K25;
        } else if (opts->frame_dmr == 1 || opts->frame_nxdn96 == 1 || opts->frame_x2tdma == 1 || opts->frame_ysf == 1
                   || opts->frame_m17 == 1) {
            channel_lpf_profile = DSD_CH_LPF_PROFILE_12K5;
        } else {
            channel_lpf_profile = DSD_CH_LPF_PROFILE_WIDE;
        }
    }
    demod->channel_lpf_enable = channel_lpf ? 1 : 0;
    demod->channel_lpf_profile = channel_lpf_profile;

    /* Copy RTL squelch level to demod state for channel-based squelch */
    demod->channel_squelch_level = (float)opts->rtl_squelch_level;
}

/**
 * @brief Apply sane defaults for digital vs analog demodulation when unset.
 *
 * Populates TED/FLL defaults, TED SPS, channel/audio filter profiles, and
 * analog deemphasis based on the selected mode when the user has not
 * overridden settings via env/CLI. Relies on @p output for effective rate.
 *
 * @param demod  Demodulator state to update.
 * @param opts   Decoder options (mode flags).
 * @param output Output ring used to infer sample rate.
 */
void
rtl_demod_select_defaults_for_mode(struct demod_state* demod, dsd_opts* opts, const struct output_state* output) {
    if (!demod || !opts || !output) {
        return;
    }
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (!cfg) {
        return;
    }

    int env_fll_alpha_set = cfg->fll_alpha_is_set;
    int env_fll_beta_set = cfg->fll_beta_is_set;
    int env_fll_deadband_set = cfg->fll_deadband_is_set;
    int env_fll_slew_set = cfg->fll_slew_is_set;
    int env_ted_gain_set = cfg->ted_gain_is_set;
    /* Treat all digital voice modes as digital for FLL/TED defaults */
    int digital_mode =
        (opts->frame_p25p1 == 1 || opts->frame_p25p2 == 1 || opts->frame_provoice == 1 || opts->frame_dmr == 1
         || opts->frame_nxdn48 == 1 || opts->frame_nxdn96 == 1 || opts->frame_x2tdma == 1 || opts->frame_ysf == 1
         || opts->frame_dstar == 1 || opts->frame_dpmr == 1 || opts->frame_m17 == 1);
    if (digital_mode) {
        /* For digital modes, never auto-enable FLL/TED.
           Leave on/off decisions to env/CLI/UI, but still derive sane defaults
           for TED/FLL parameters when not explicitly provided. */
        {
            int Fs_cx = 0;
            /* TED operates on complex baseband at demod->rate_out.
               Prefer that rate even when an audio resampler is enabled. */
            if (demod->rate_out > 0) {
                Fs_cx = demod->rate_out;
            } else {
                Fs_cx = (int)output->rate;
            }
            if (Fs_cx <= 0) {
                Fs_cx = 48000; /* safe default */
            }
            /* Choose symbol rate by mode; keep explicit branches for narrow paths. */
            int sym_rate = 4800; /* generic 4.8 ksps */
            if (opts->frame_p25p2 == 1 || opts->frame_x2tdma == 1) {
                sym_rate = 6000;
            } else if (opts->frame_nxdn48 == 1 || opts->frame_dpmr == 1) {
                sym_rate = 2400;
            }
            if (Fs_cx < (sym_rate * 2)) {
                LOG_WARNING("TED SPS: demod rate %d Hz is low for ~%d sym/s; clamping to minimum SPS.\n", Fs_cx,
                            sym_rate);
            }
            int sps = (Fs_cx + (sym_rate / 2)) / sym_rate; /* round(Fs/sym_rate) */
            if (sps < 2) {
                sps = 2;
            }
            if (sps > 64) {
                sps = 64;
            }
            demod->ted_sps = sps;

            /* Check if SPS is truly integer: Fs_cx must be exactly divisible by sym_rate.
               Non-integer SPS causes TED/FLL band-edge to malfunction. */
            int remainder = Fs_cx % sym_rate;
            if (remainder == 0) {
                demod->sps_is_integer = 1;
            } else {
                demod->sps_is_integer = 0;
                if (demod->cqpsk_enable) {
                    LOG_WARNING(
                        "Non-integer SPS detected: %d Hz / %d sym/s = %.3f (rounded to %d). "
                        "FLL band-edge will be auto-disabled; CQPSK will continue to run Gardner TED at the "
                        "rounded SPS. Use a DSP bandwidth that results in integer SPS for optimal performance.\n",
                        Fs_cx, sym_rate, (float)Fs_cx / (float)sym_rate, sps);
                } else {
                    LOG_WARNING("Non-integer SPS detected: %d Hz / %d sym/s = %.3f (rounded to %d). "
                                "TED and FLL band-edge will be auto-disabled. "
                                "Use a DSP bandwidth that results in integer SPS for optimal performance.\n",
                                Fs_cx, sym_rate, (float)Fs_cx / (float)sym_rate, sps);
                    /* Auto-disable TED when non-integer SPS is detected for non-CQPSK paths */
                    if (demod->ted_enabled && !demod->ted_force) {
                        demod->ted_enabled = 0;
                        LOG_INFO("TED auto-disabled due to non-integer SPS.\n");
                    }
                }
            }
        }
        if (!env_ted_gain_set) {
            /* OP25 TED defaults: gain_mu = 0.025, gain_omega = 0.1 * gain_mu^2 = 0.0000625 */
            demod->ted_gain = 0.025f;
        }
        /* Digital defaults: slightly stronger, lower-deadband FLL for CQPSK/FM. */
        if (!env_fll_alpha_set) {
            demod->fll_alpha = 0.008f; /* ~150/32768 but in native float */
        }
        if (!env_fll_beta_set) {
            demod->fll_beta = 0.0008f; /* ~15/32768 but in native float */
        }
        if (!env_fll_deadband_set) {
            demod->fll_deadband = 0.002f; /* ~32/16384 but in native float */
        }
        if (!env_fll_slew_set) {
            demod->fll_slew_max = 0.004f; /* ~128/32768 but in native float */
        }
    } else {
        /* For analog-like modes, also avoid auto-enabling FLL/TED.
           Respect any explicit env/CLI/UI decisions, but do not change gates. */
        if (!env_fll_alpha_set) {
            demod->fll_alpha = 0.0015f; /* ~50/32768 but in native float */
        }
        if (!env_fll_beta_set) {
            demod->fll_beta = 0.00015f; /* ~5/32768 but in native float */
        }
    }
}

/**
 * @brief Recompute resampler design after rate changes.
 *
 * Updates the resampler taps/ratios based on the current demod/output rates
 * and the requested target, falling back to @p rtl_dsp_bw_hz when needed.
 * Also updates output.rate to reflect the new sink rate.
 *
 * @param demod         Demodulator state (contains resampler config).
 * @param output        Output ring state to refresh.
 * @param rtl_dsp_bw_hz DSP baseband bandwidth in Hz when rate_out is unset.
 */
void
rtl_demod_maybe_update_resampler_after_rate_change(struct demod_state* demod, struct output_state* output,
                                                   int rtl_dsp_bw_hz) {
    if (!demod || !output) {
        return;
    }
    /* Resolve actual input rate: prefer rate_out, fall back to DSP bandwidth. */
    int inRate = demod->rate_out > 0 ? demod->rate_out : rtl_dsp_bw_hz;
    if (demod->resamp_target_hz <= 0) {
        demod->resamp_enabled = 0;
        output->rate = inRate;
        return;
    }
    int target = demod->resamp_target_hz;
    /* Bypass the resampler when the input rate already matches the target. */
    if (target == inRate) {
        if (demod->resamp_enabled) {
            if (demod->resamp_taps) {
                dsd_neo_aligned_free(demod->resamp_taps);
                demod->resamp_taps = NULL;
            }
            if (demod->resamp_hist) {
                dsd_neo_aligned_free(demod->resamp_hist);
                demod->resamp_hist = NULL;
            }
        }
        demod->resamp_enabled = 0;
        demod->resamp_L = 1;
        demod->resamp_M = 1;
        demod->resamp_phase = 0;
        demod->resamp_hist_head = 0;
        output->rate = inRate;
        return;
    }
    int g = gcd_int(inRate, target);
    int L = target / g;
    int M = inRate / g;
    if (L < 1) {
        L = 1;
    }
    if (M < 1) {
        M = 1;
    }
    int scale = (M > 0) ? ((L + M - 1) / M) : 1;

    if (scale > 12) {
        if (demod->resamp_enabled) {
            /* Disable and free on out-of-bounds ratio */
            if (demod->resamp_taps) {
                dsd_neo_aligned_free(demod->resamp_taps);
                demod->resamp_taps = NULL;
            }
            if (demod->resamp_hist) {
                dsd_neo_aligned_free(demod->resamp_hist);
                demod->resamp_hist = NULL;
            }
        }
        demod->resamp_enabled = 0;
        output->rate = inRate;
        LOG_WARNING("Resampler ratio too large on retune (L=%d,M=%d). Disabled.\n", L, M);
        return;
    }

    /* Re-design only if params changed or buffers not allocated */
    if (!demod->resamp_enabled || demod->resamp_L != L || demod->resamp_M != M || demod->resamp_taps == NULL
        || demod->resamp_hist == NULL) {
        if (demod->resamp_taps) {
            dsd_neo_aligned_free(demod->resamp_taps);
            demod->resamp_taps = NULL;
        }
        if (demod->resamp_hist) {
            dsd_neo_aligned_free(demod->resamp_hist);
            demod->resamp_hist = NULL;
        }
        resamp_design(demod, L, M);
        demod->resamp_L = L;
        demod->resamp_M = M;
        demod->resamp_enabled = 1;
        LOG_INFO("Resampler reconfigured: %d -> %d Hz (L=%d,M=%d).\n", inRate, target, L, M);
    }
    output->rate = target;
}

/**
 * @brief Refresh TED SPS after capture/output rate changes.
 *
 * When TED SPS is not explicitly forced via runtime configuration, recompute
 * the nominal samples-per-symbol from the current output rate and mode.
 *
 * @param demod  Demodulator state.
 * @param opts   Decoder options (mode flags).
 * @param output Output ring (for sink rate).
 */
void
rtl_demod_maybe_refresh_ted_sps_after_rate_change(struct demod_state* demod, const dsd_opts* opts,
                                                  const struct output_state* output) {
    if (!demod || !output) {
        return;
    }

    int Fs_cx = 0;
    /* TED always sees complex baseband at demod->rate_out; compute SPS in that domain,
       independent of any post-demod audio resampling. */
    if (demod->rate_out > 0) {
        Fs_cx = demod->rate_out;
    } else {
        Fs_cx = (int)output->rate;
    }
    if (Fs_cx <= 0) {
        Fs_cx = 48000;
    }
    int sps = 0;
    if (opts) {
        int sym_rate = 4800;
        /* When mod_qpsk is set (e.g., -mq for P25P1 CQPSK), use 4800 sym/s.
         * When only P25P2/X2-TDMA is enabled (without P25P1), use 6000 sym/s.
         * When both P25P1 and P25P2 are enabled (trunking mode), default to
         * P25P1 rate (4800) since CC is typically encountered first; the trunk
         * state machine will override via ted_sps_override when tuning to P25P2 VC. */
        if (opts->mod_qpsk == 1) {
            sym_rate = 4800; /* P25P1 CQPSK */
        } else if ((opts->frame_p25p2 == 1 || opts->frame_x2tdma == 1) && opts->frame_p25p1 == 0) {
            sym_rate = 6000; /* P25P2/X2-TDMA only */
        } else if (opts->frame_nxdn48 == 1 || opts->frame_dpmr == 1) {
            sym_rate = 2400;
        }
        if (Fs_cx < (sym_rate * 2)) {
            LOG_WARNING("TED SPS: demod rate %d Hz is low for ~%d sym/s; clamping to minimum SPS.\n", Fs_cx, sym_rate);
        }
        sps = (Fs_cx + (sym_rate / 2)) / sym_rate;
        /* Check if SPS is truly integer: Fs_cx must be exactly divisible by sym_rate. */
        int remainder = Fs_cx % sym_rate;
        if (remainder == 0) {
            demod->sps_is_integer = 1;
        } else {
            demod->sps_is_integer = 0;
            if (demod->cqpsk_enable) {
                LOG_WARNING("Non-integer SPS after rate change: %d Hz / %d sym/s = %.3f. "
                            "FLL band-edge auto-disabled; CQPSK continues to run Gardner TED at the rounded SPS.\n",
                            Fs_cx, sym_rate, (float)Fs_cx / (float)sym_rate);
            } else {
                LOG_WARNING("Non-integer SPS after rate change: %d Hz / %d sym/s = %.3f. "
                            "TED and FLL band-edge auto-disabled.\n",
                            Fs_cx, sym_rate, (float)Fs_cx / (float)sym_rate);
            }
        }
    } else {
        sps = (Fs_cx + 2400) / 4800;
        /* Check if SPS is truly integer for default 4800 sym/s. */
        int remainder = Fs_cx % 4800;
        demod->sps_is_integer = (remainder == 0) ? 1 : 0;
        if (!demod->sps_is_integer) {
            if (demod->cqpsk_enable) {
                LOG_WARNING("Non-integer SPS after rate change: %d Hz / 4800 sym/s = %.3f. "
                            "FLL band-edge auto-disabled; CQPSK continues to run Gardner TED at the rounded SPS.\n",
                            Fs_cx, (float)Fs_cx / 4800.0f);
            } else {
                LOG_WARNING("Non-integer SPS after rate change: %d Hz / 4800 sym/s = %.3f. "
                            "TED and FLL band-edge auto-disabled.\n",
                            Fs_cx, (float)Fs_cx / 4800.0f);
            }
        }
    }
    if (sps < 2) {
        sps = 2;
    }
    if (sps > 64) {
        sps = 64;
    }
    /* Respect manual override (e.g., from trunking P25P2 voice channel tune).
       When ted_sps_override > 0, it takes precedence over calculated SPS. */
    if (demod->ted_sps_override > 0) {
        demod->ted_sps = demod->ted_sps_override;
    } else {
        demod->ted_sps = sps;
    }
}

/**
 * @brief Release resources allocated by demod_init_mode/config helpers.
 *
 * Tears down resampler/filter buffers, worker pools, and any dynamically
 * allocated state within the demodulator instance. Safe on partially
 * initialized structures.
 *
 * @param demod Demodulator state to clean up.
 */
void
rtl_demod_cleanup(struct demod_state* demod) {
    if (!demod) {
        return;
    }
    dsd_cond_destroy(&demod->ready);
    dsd_mutex_destroy(&demod->ready_m);
    demod_mt_destroy(demod);
    if (demod->resamp_taps) {
        dsd_neo_aligned_free(demod->resamp_taps);
        demod->resamp_taps = NULL;
    }
    if (demod->resamp_hist) {
        dsd_neo_aligned_free(demod->resamp_hist);
        demod->resamp_hist = NULL;
    }
    if (demod->post_polydecim_taps) {
        dsd_neo_aligned_free(demod->post_polydecim_taps);
        demod->post_polydecim_taps = NULL;
    }
    if (demod->post_polydecim_hist) {
        dsd_neo_aligned_free(demod->post_polydecim_hist);
        demod->post_polydecim_hist = NULL;
    }
}
