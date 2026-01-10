// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief RTL-SDR stream orchestration and demodulation pipeline.
 *
 * Sets up the RTL-SDR device and worker threads, configures capture
 * settings and the demodulation pipeline, manages rings and UDP control,
 * and exposes a consumer API for audio samples and tuning.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/power.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/dsp/costas.h>
#include <dsd-neo/dsp/demod_pipeline.h>
#include <dsd-neo/dsp/demod_state.h>
#include <dsd-neo/dsp/fll.h>
#include <dsd-neo/dsp/math_utils.h>
#include <dsd-neo/dsp/resampler.h>
#include <dsd-neo/dsp/snr_bias.h>
#include <dsd-neo/dsp/ted.h>
#include <dsd-neo/io/rtl_demod_config.h>
#include <dsd-neo/io/rtl_device.h>
#include <dsd-neo/io/rtl_metrics.h>
#include <dsd-neo/io/rtl_stream_c.h>
#include <dsd-neo/io/udp_control.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/platform/timing.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/exitflag.h>
#include <dsd-neo/runtime/input_ring.h>
#include <dsd-neo/runtime/log.h>
#include <dsd-neo/runtime/mem.h>
#include <dsd-neo/runtime/ring.h>
#include <dsd-neo/runtime/rt_sched.h>
#include <dsd-neo/runtime/threading.h>
#include <dsd-neo/runtime/unicode.h>
#include <dsd-neo/runtime/worker_pool.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if DSD_PLATFORM_POSIX
#include <unistd.h>
#endif
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif
/* Forward declarations for internal helpers used by shims */
void dsd_rtl_stream_clear_output(void);
double dsd_rtl_stream_return_pwr(void);
unsigned int dsd_rtl_stream_output_rate(void);
int dsd_rtl_stream_ted_bias(void);
int dsd_rtl_stream_set_rtltcp_autotune(int onoff);
int dsd_rtl_stream_get_rtltcp_autotune(void);
#ifdef __cplusplus
}
#endif

/* Forward declaration for eye ring append used in demod loop */
static inline void eye_ring_append_i_chan(const float* iq_interleaved, int len_interleaved);

#define DEFAULT_SAMPLE_RATE 48000
#define AUTO_GAIN           (-100)
#define BUFFER_DUMP         4096

#define FREQUENCIES_LIMIT   1000

static int lcm_post[17] = {1, 1, 1, 3, 1, 5, 3, 7, 1, 9, 5, 11, 3, 13, 7, 15, 1};
static int ACTUAL_BUF_LENGTH;

static const double kPi = 3.14159265358979323846;

#if defined(__GNUC__) || defined(__clang__)
#define DSD_NEO_PRAGMA(x) _Pragma(#x)
#define DSD_NEO_IVDEP     DSD_NEO_PRAGMA(GCC ivdep)

/**
 * @brief Hint that a pointer is aligned to a compile-time boundary for vectorization.
 *
 * This is a lightweight wrapper over compiler intrinsics to improve
 * auto-vectorization by promising the compiler that the pointer meets the
 * specified alignment. Use with care and only when the alignment guarantee
 * is actually met.
 *
 * @tparam T Element type of the pointer.
 * @param p  Pointer to memory that is at least `align_unused` aligned.
 * @param align_unused Alignment in bytes (ignored at runtime; for readability).
 * @return Pointer `p` with alignment assumption applied.
 */
template <typename T>
static inline T*
assume_aligned_ptr(T* p, size_t /*align_unused*/) {
    return (T*)__builtin_assume_aligned(p, 64);
}

template <typename T>
static inline const T*
assume_aligned_ptr(const T* p, size_t /*align_unused*/) {
    return (const T*)__builtin_assume_aligned(p, 64);
}
#else
#define DSD_NEO_IVDEP

/**
 * @brief See aligned variant: noop fallback when compiler does not support alignment
 * assumptions.
 * @tparam T Element type of the pointer.
 * @param p  Pointer to return as-is.
 * @param align_unused Unused parameter for signature compatibility.
 * @return Pointer `p` unchanged.
 */
template <typename T>
static inline T*
assume_aligned_ptr(T* p, size_t /*align_unused*/) {
    return p;
}

template <typename T>
static inline const T*
assume_aligned_ptr(const T* p, size_t /*align_unused*/) {
    return p;
}
#endif
#ifndef DSD_NEO_ALIGN
#define DSD_NEO_ALIGN 64
#endif

/* Compiler-friendly restrict qualifier */
#if defined(__GNUC__) || defined(__clang__)
#define DSD_NEO_RESTRICT __restrict__
#else
#define DSD_NEO_RESTRICT
#endif

/* Debug/compat toggles via env (implemented in rtl_demod_config.cpp). */
extern int combine_rotate_enabled;      /* DSD_NEO_COMBINE_ROT (1 default) */
extern int upsample_fixedpoint_enabled; /* DSD_NEO_UPSAMPLE_FP (1 default) */

/* Allow disabling the fs/4 capture frequency shift via env for trunking/exact-center use cases. */
extern int disable_fs4_shift; /* Set by env DSD_NEO_DISABLE_FS4_SHIFT=1 */

// UDP control handle
static struct udp_control* g_udp_ctrl = NULL;

/* DSP baseband for RTL path in Hz (derived from opts->rtl_dsp_bw_khz). */
int rtl_dsp_bw_hz;
short int volume_multiplier;
uint16_t port;

struct dongle_state {
    int exit_flag;
    dsd_thread_t thread;
    int dev_index;
    uint32_t freq;
    uint32_t rate;
    int gain;
    uint32_t buf_len;
    int ppm_error;
    int offset_tuning;
    int direct_sampling;
    std::atomic<int> mute;
    struct demod_state* demod_target;
};

struct demod_mt_worker_arg {
    struct demod_state* s;
    int id;
};

struct controller_state {
    int exit_flag;
    dsd_thread_t thread;
    uint32_t freqs[FREQUENCIES_LIMIT];
    int freq_len;
    int freq_now;
    int edge;
    int wb_mode;
    dsd_cond_t hop;
    dsd_mutex_t hop_m;
    /* Marshalled retune request from external threads (UDP/API). */
    std::atomic<int> manual_retune_pending;
    uint32_t manual_retune_freq;
    /* Cold start gate: demod thread skips CQPSK until controller signals ready.
     * This prevents the race where demod processes samples with uninitialized
     * TED/Costas state before the controller finishes cold start configuration. */
    std::atomic<int> cold_start_ready;
    /* Retune gate: demod thread skips processing while retune is in progress.
     * This prevents the race where demod processes transient/stale samples
     * during hardware retune before TED/Costas/AGC are reset. Set to 1 at
     * start of retune, cleared to 0 after reset complete. */
    std::atomic<int> retune_in_progress;
    /* Retune completion signaling: allows dsd_rtl_stream_tune() to block until
     * the controller thread has finished the hardware retune and DSP reset.
     * This prevents the race where trunking code sets SPS parameters before
     * demod_reset_on_retune() has executed, causing Costas/FLL state corruption. */
    dsd_cond_t retune_done_cond;
    dsd_mutex_t retune_done_m;
    std::atomic<int> retune_done_flag;
    /* Request ID for matching completion signals to requests (prevents stale wakeups) */
    std::atomic<uint32_t> retune_request_id;
    std::atomic<uint32_t> retune_complete_id;
};

struct rtl_device* rtl_device_handle = NULL;
struct dongle_state dongle;
struct demod_state demod;
struct output_state output;
struct controller_state controller;
static struct input_ring_state input_ring;
/* Controller can request a ring purge; consumer/demod performs the discard safely. */
static std::atomic<int> g_ring_purge_pending{0};

struct RtlSdrInternals {
    struct rtl_device* device;
    struct dongle_state* dongle;
    struct demod_state* demod;
    struct output_state* output;
    struct controller_state* controller;
    struct input_ring_state* input_ring;
    struct udp_control** udp_ctrl_ptr;
    const dsd_opts* opts; /* snapshot for mode hints (P25p1/2, etc.) */
    const dsdneoRuntimeConfig* cfg;
    /* Cooperative shutdown flag for threads launched by this stream */
    std::atomic<int> should_exit;
};

static struct RtlSdrInternals* g_stream = NULL;

static inline int
debug_cqpsk_enabled(void) {
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    return (cfg && cfg->debug_cqpsk_enable) ? 1 : 0;
}

/*
 * Pick a hardware tuner bandwidth.
 *
 * Priority:
 *  - If env DSD_NEO_TUNER_BW_HZ is set:
 *      - value "auto" or 0 => return 0 (driver automatic)
 *      - positive integer => clamp and use that value (in Hz)
 *  - Otherwise, prefer setting BW ~= capture sample rate to avoid
 *    overly narrow IF filtering across retunes/hops.
 *  - As a conservative fallback, derive from DSP bandwidth with a
 *    safety margin and clamp to practical bounds.
 */
static uint32_t
choose_tuner_bw_hz(uint32_t capture_rate_hz, uint32_t dsp_bw_hz) {
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (cfg && cfg->tuner_bw_hz_is_set) {
        return (uint32_t)cfg->tuner_bw_hz; /* 0 => driver automatic */
    }

    /* Mode-aware policy:
       - Scanning (multiple freqs): prefer BW ~= capture rate for consistent IF while hopping.
       - Single freq:
           - Digital-like (no deemphasis): target ~2x channel BW with clamps.
           - Analog-like (deemphasis enabled): keep wide but sane; use min(capture, 1.8 MHz).
       If uncertain, fall back to prior heuristics. */

    int scanning = (controller.freq_len > 1) ? 1 : 0;
    int analog_like = (demod.deemph != 0) ? 1 : 0;
    /* When offset_tuning is unavailable, we apply an fs/4 capture shift.
       That places the desired channel capture_rate/4 away from tuner center,
       so ensure the IF filter is wide enough to avoid attenuating it.
       Tuner BW is total (double-sided), so we need 2×(fs/4 + half-channel). */
    int fs4_shift_active = (!disable_fs4_shift && dongle.offset_tuning == 0) ? 1 : 0;
    uint32_t fs4_guard_bw = 0;
    if (fs4_shift_active && capture_rate_hz > 0) {
        /* Channel center sits at fs/4 from tuner center; tuner BW is total passband.
           Need: 2 × (offset + half_channel) = fs/2 + dsp_bw */
        uint64_t guard = (uint64_t)(capture_rate_hz / 2);
        if (dsp_bw_hz > 0) {
            guard += (uint64_t)dsp_bw_hz;
        }
        /* Clamp to capture rate - no point requesting wider than what we sample. */
        if (guard > (uint64_t)capture_rate_hz) {
            guard = capture_rate_hz;
        }
        fs4_guard_bw = (uint32_t)guard;
    }
    auto apply_fs4_guard = [&](uint32_t bw) -> uint32_t {
        if (fs4_guard_bw > 0 && bw < fs4_guard_bw) {
            return fs4_guard_bw;
        }
        return bw;
    };

    if (scanning) {
        if (capture_rate_hz >= 225000 && capture_rate_hz <= 5000000) {
            return apply_fs4_guard(capture_rate_hz);
        }
    } else {
        if (!analog_like && dsp_bw_hz > 0) {
            /* Digital single-channel: ~2x channel bandwidth (guard), clamps 100 kHz..1.5 MHz */
            uint64_t tgt = (uint64_t)dsp_bw_hz * 2ULL;
            if (tgt < 100000ULL) {
                tgt = 100000ULL;
            }
            if (tgt > 1500000ULL) {
                tgt = 1500000ULL;
            }
            /* Optional: don't exceed capture rate much */
            if (capture_rate_hz > 0 && tgt > capture_rate_hz) {
                tgt = capture_rate_hz;
            }
            return apply_fs4_guard((uint32_t)tgt);
        }
        if (analog_like && capture_rate_hz > 0) {
            uint32_t maxa = 1800000U; /* ~1.8 MHz ceiling for analog */
            return apply_fs4_guard((capture_rate_hz < maxa) ? capture_rate_hz : maxa);
        }
    }

    /* Fallback: derive from DSP bandwidth with margin (x8), clamped. */
    uint32_t bw = 0;
    if (dsp_bw_hz > 0) {
        uint64_t hinted = (uint64_t)dsp_bw_hz * 8ULL; /* generous guard */
        if (hinted < 100000ULL) {
            hinted = 100000ULL; /* min 100 kHz */
        }
        if (hinted > 3000000ULL) {
            hinted = 3000000ULL; /* max 3 MHz */
        }
        bw = (uint32_t)hinted;
    }
    if (bw == 0) {
        /* Last-resort default */
        bw = 1200000; /* 1.2 MHz */
    }
    return apply_fs4_guard(bw);
}

/* Forward declarations for visualization ring clears (defined later in file) */
static void constellation_ring_clear(void);
static void eye_ring_clear(void);
static void snr_ema_reset(void);

/**
 * @brief On retune/hop, drain audio output ring for a short time to avoid
 * cutting off transmissions. If configured to clear, force-clear instead.
 *
 * Also clears the constellation and eye diagram buffers to prevent stale
 * samples from the previous frequency/SPS from contaminating the display.
 */
static void
drain_output_on_retune(void) {
    struct output_state* outp = &output;
    if (g_stream && g_stream->output) {
        outp = g_stream->output;
    }
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int force_clear = 0;
    int drain_ms = 50;
    if (cfg) {
        if (cfg->output_clear_on_retune_is_set) {
            force_clear = (cfg->output_clear_on_retune != 0);
        }
        if (cfg->retune_drain_ms_is_set) {
            drain_ms = cfg->retune_drain_ms;
        }
    }
    if (drain_ms < 0) {
        drain_ms = 0;
    }

    /* Clear visualization buffers to avoid mixing old/new frequency samples.
     * This is critical for P25P1->P25P2 transitions where SPS changes from 5 to 4;
     * stale constellation points from the old SPS create the appearance of
     * degraded SNR even when the DSP is performing correctly. */
    constellation_ring_clear();
    eye_ring_clear();
    snr_ema_reset();

    if (force_clear || drain_ms == 0) {
        dsd_rtl_stream_clear_output();
        return;
    }
    size_t before = ring_used(outp);
    int waited_ms = 0;
    while (!ring_is_empty(outp) && waited_ms < drain_ms) {
        dsd_sleep_ms(1);
        waited_ms++;
    }
    if (!ring_is_empty(outp)) {
        /* Timed out; clear remainder to avoid stale backlog */
        dsd_rtl_stream_clear_output();
    }
    (void)before; /* reserved for future diagnostics */
}

/* C-linkage helper to toggle bias tee on the active RTL device.
   For rtl_tcp sources, forwards the request via protocol; for USB, uses
   librtlsdr API when available. Returns 0 on success; negative on error. */
extern "C" int
dsd_rtl_stream_set_bias_tee(int on) {
    if (!rtl_device_handle) {
        return -1;
    }
    return rtl_device_set_bias_tee(rtl_device_handle, on ? 1 : 0);
}

/* Export applied tuner gain for UI without exposing internals. */
extern "C" int
dsd_rtl_stream_get_gain(int* out_tenth_db, int* out_is_auto) {
    if (out_tenth_db) {
        *out_tenth_db = 0;
    }
    if (out_is_auto) {
        *out_is_auto = 1;
    }
    if (!rtl_device_handle) {
        return -1;
    }
    int is_auto = rtl_device_is_auto_gain(rtl_device_handle);
    if (out_is_auto) {
        *out_is_auto = (is_auto > 0) ? 1 : 0;
    }
    if (is_auto > 0) {
        /* In auto mode, report AGC without a specific gain value. */
        return 0;
    }
    int g = rtl_device_get_tuner_gain(rtl_device_handle);
    if (g < 0) {
        return -1;
    }
    if (out_tenth_db) {
        *out_tenth_db = g;
    }
    return 0;
}

/**
 * @brief Reset demodulator state on retune/hop to avoid stale "lock"/bias.
 *
 * Clears squelch accumulators, FLL/TED integrators, deemphasis/audio LPF/DC
 * state, history buffers for HB/CIC paths, and resampler phase/history.
 * This ensures each new frequency starts from a neutral state.
 *
 * @param s Demodulator state to reset.
 */

static void
demod_reset_on_retune(struct demod_state* s) {
    if (!s) {
        return;
    }
    /* Track SPS transitions for CQPSK so we can fully reset timing/carrier/filter
     * state when jumping between different symbol rates. */
    {
        static int prev_ted_sps = 0;
        int next_sps = (s->ted_sps_override > 0) ? s->ted_sps_override : (s->ted_sps > 0 ? s->ted_sps : 5);
        int sps_changed = (prev_ted_sps > 0 && next_sps != prev_ted_sps);

        if (s->cqpsk_enable && sps_changed) {
            /* Purge half-band/channel filter histories to avoid cross-channel residue. */
            for (int st = 0; st < 10; st++) {
                memset(s->hb_hist_i[st], 0, sizeof(s->hb_hist_i[st]));
                memset(s->hb_hist_q[st], 0, sizeof(s->hb_hist_q[st]));
            }
            memset(s->channel_lpf_hist_i, 0, sizeof(s->channel_lpf_hist_i));
            memset(s->channel_lpf_hist_q, 0, sizeof(s->channel_lpf_hist_q));
            s->channel_lpf_hist_len = 0;
            /* Reset resampler bookkeeping just in case it was enabled. */
            s->resamp_phase = 0;
            s->resamp_hist_head = 0;
            if (s->resamp_hist && s->resamp_taps_per_phase > 0) {
                memset(s->resamp_hist, 0, (size_t)s->resamp_taps_per_phase * sizeof(float));
            }
            /* Reset timing/carrier loops for the new symbol rate. */
            ted_init_state(&s->ted_state);
            s->ted_mu = 0.0f;
            s->costas_state.freq = 0.0f;
            s->costas_state.phase = 0.0f;
            s->costas_state.error = 0.0f;
            /* Force fresh FLL acquisition after the SPS jump. */
            s->fll_band_edge_state.freq = 0.0f;
        }
        prev_ted_sps = next_sps;
    }
    /* Squelch */
    s->squelch_hits = 0;
    s->squelch_running_power = 0;
    s->squelch_decim_phase = 0;
    s->prev_lpr_index = 0;
    s->now_lpr = 0;
    /* Clear any staged block so power API does not see stale data */
    s->lp_len = 0;
    memset(s->input_cb_buf, 0, sizeof(s->input_cb_buf));
    /* FLL */
    fll_init_state(&s->fll_state);
    s->fll_freq = 0.0f;
    s->fll_phase = 0.0f;
    s->fll_prev_r = 0.0f;
    s->fll_prev_j = 0.0f;
    /* CQPSK: Initialize differential phasor state for clean acquisition on new signal.
     *
     * Unlike OP25's continuous sample flow where set_omega() only clears the delay line,
     * dsd-neo has discrete retune events with muting. When samples resume after retune,
     * they're from a NEW signal - the old diff_prev is completely stale.
     *
     * Reset diff_prev to (1,0) so the first symbol's differential decode produces:
     *   y[0] = x[0] * conj(1+j0) = x[0]
     * This means the first symbol is passed through unchanged, avoiding garbage output
     * that would cascade through subsequent differential decodes.
     */
    s->cqpsk_diff_prev_r = 1.0f;
    s->cqpsk_diff_prev_j = 0.0f;
    s->costas_err_avg_q14 = 0;
    /* Preserve AGC state so gain does not restart from unity on each retune.
     * This mirrors OP25’s continuous-flow behavior and avoids a post-retune
     * re-settle period. */
    /* Costas reset: clear phase and error, but PRESERVE freq estimate.
     *
     * The carrier frequency offset (c->freq) is primarily a property of the RTL-SDR
     * local oscillator, not the channel. When retuning from CC to VC on the same
     * system, the oscillator offset should be similar (~50-100 Hz typically).
     *
     * Preserving freq allows the Costas loop to start tracking immediately rather
     * than needing to slew from 0 Hz to the actual offset. This is critical for
     * P25p2 HDQPSK where the loop must acquire quickly to decode the first TDMA
     * superframe.
     *
     * We DO reset phase because the phase relationship changes with RF frequency.
     */
    /* Costas: Reset phase, error, AND frequency on every CQPSK retune.
     *
     * Previously we preserved freq for "nearby" same-SPS tunes (P25P1 CC↔VC),
     * but P25 channels can be several MHz apart. A preserved Costas freq from
     * the old channel will be wrong and cause the loop to fight against the
     * new signal. Starting from 0 Hz and acquiring fresh is more reliable. */
    s->costas_state.phase = 0.0f;
    s->costas_state.error = 0.0f;
    if (s->cqpsk_enable) {
        s->costas_state.freq = 0.0f;
    }

    /* FLL: Reset frequency and initialize band-edge filters on every CQPSK retune.
     *
     * Previously we only reset FLL freq for P25P2 tunes, but the same logic applies
     * to all retunes: channels can be several MHz apart, so a preserved FLL freq
     * from the old channel will be wrong. Starting from 0 Hz is more reliable.
     *
     * CRITICAL: We must design the band-edge filters HERE, not lazily on first
     * sample block. Lazy initialization causes the first few blocks after cold
     * start or retune to run with uninitialized/wrong filter taps, corrupting
     * the FLL error signal and preventing proper frequency acquisition. */
    {
        int sps = s->ted_sps_override > 0 ? s->ted_sps_override : (s->ted_sps > 0 ? s->ted_sps : 5);
        if (s->cqpsk_enable) {
            s->fll_band_edge_state.freq = 0.0f;
        }
        dsd_fll_band_edge_init(&s->fll_band_edge_state, sps);
    }
    /* TED: Use soft reset to preserve mu/omega for phase continuity across retunes.
     * The Gardner TED has multiple stable lock points and a full reset can cause
     * convergence to a suboptimal symbol phase, degrading CQPSK performance. */
    ted_soft_reset(&s->ted_state);
    /* Clear TED delay line on EVERY CQPSK retune to remove stale samples from the
     * previous RF frequency. This is critical for ANY frequency change, not just
     * P25P2 tunes. For example, P25P1 VC → P25P1 CC may tune several MHz away;
     * stale VC samples in the TED delay line corrupt timing recovery on the CC.
     * This matches OP25's reset() behavior which clears d_queue on every retune. */
    if (s->cqpsk_enable) {
        memset(s->ted_state.dl, 0, sizeof(s->ted_state.dl));
        s->ted_state.dl_index = 0;
        /* Reinitialize mu to force delay line refill before first output.
         * CRITICAL: Must also update the legacy ted_mu field because
         * gardner_timing_adjust() syncs ted_mu -> ted_state.mu before processing,
         * which would overwrite our reset if we only set ted_state.mu. */
        float mu_init = (float)(s->ted_state.twice_sps + 1);
        s->ted_state.mu = mu_init;
        s->ted_mu = mu_init;
    }
    /* Apply any pending TED SPS override NOW, after hardware retune completes.
     *
     * The override is set by trunking code BEFORE retune (when the new channel's
     * symbol rate is known), but must not be applied until AFTER retune when new
     * samples arrive. Applying it earlier would process stale samples (old freq)
     * with the wrong SPS. This function runs after hardware retune completes,
     * so it's safe to apply the override here.
     *
     * CRITICAL: When SPS changes (e.g., P25p1 5sps -> P25p2 4sps), we must reset
     * the Costas loop frequency. OP25 does this explicitly in set_omega() by
     * calling costas_reset() which zeros both frequency and phase.
     *
     * The Costas loop operates at SYMBOL RATE (after TED decimation), so its
     * frequency estimate (rad/symbol) represents different Hz offsets at
     * different symbol rates. A preserved frequency that tracked 100 Hz at
     * 4800 sym/s would track 125 Hz at 6000 sym/s - a 25% error that causes
     * the loop to fight against correct carrier recovery.
     *
     * The costas_reset_pending flag is set by dsd_rtl_stream_set_ted_sps() when
     * the SPS override is different from the current SPS. This captures the
     * SPS change before other code paths may equalize ted_sps and ted_sps_override. */
    /* Debug: Log costas_reset_pending state */
    {
        if (debug_cqpsk_enabled()) {
            fprintf(stderr, "[COSTAS-RESET] pending=%d ted_sps=%d override=%d\n", s->costas_reset_pending, s->ted_sps,
                    s->ted_sps_override);
        }
    }
    if (s->costas_reset_pending) {
        /* SPS is changing - reset Costas loop to allow fresh acquisition.
         * This matches OP25's set_omega() -> costas_reset() behavior. */
        s->costas_state.freq = 0.0f;
        s->costas_state.phase = 0.0f;
        s->costas_state.error = 0.0f;
        s->costas_reset_pending = 0;
    }
    if (s->ted_sps_override > 0 && s->ted_sps != s->ted_sps_override) {
        s->ted_sps = s->ted_sps_override;
    }
    /* Note: s->ted_mu is a legacy field used by the Farrow path; the OP25-style
     * Gardner uses ted_state.mu internally. Don't reset it here. */

    /* CRITICAL: Clear filter histories on EVERY retune for CQPSK mode.
     *
     * When retuning between frequencies (e.g., P25P1 VC → CC or P25P2 VC → CC),
     * the HB/LPF filter histories contain samples from the OLD RF frequency.
     * Even with ring purging that prevents NEW stale samples from leaking,
     * the filter delay lines themselves hold several samples of convolution
     * history. When new samples from the NEW frequency arrive, they convolve
     * with old samples from the previous frequency, producing corrupted output.
     *
     * This is especially severe for P25P1 VC → P25P1 CC transitions where the
     * SPS doesn't change (both use 10 SPS at 48kHz), so the conditional reset
     * at lines ~555-578 is skipped. The stale filter history from a VC several
     * MHz away corrupts the first ~100ms of CC samples, causing constellation
     * magnitude collapse (~0.2 instead of ~0.9) and sync loss.
     *
     * Solution: Always clear filter histories for CQPSK. The AGC/filter settle
     * time (~10-50ms) is much less costly than the prolonged (~1-3s) sync loss
     * caused by stale history corruption. */
    if (s->cqpsk_enable) {
        for (int st = 0; st < 10; st++) {
            memset(s->hb_hist_i[st], 0, sizeof(s->hb_hist_i[st]));
            memset(s->hb_hist_q[st], 0, sizeof(s->hb_hist_q[st]));
        }
        memset(s->channel_lpf_hist_i, 0, sizeof(s->channel_lpf_hist_i));
        memset(s->channel_lpf_hist_q, 0, sizeof(s->channel_lpf_hist_q));
        s->channel_lpf_hist_len = 0;
        /* Clear resampler state defensively (typically off for CQPSK). */
        s->resamp_phase = 0;
        s->resamp_hist_head = 0;
        if (s->resamp_hist && s->resamp_taps_per_phase > 0) {
            memset(s->resamp_hist, 0, (size_t)s->resamp_taps_per_phase * sizeof(float));
        }
    }

    /* Debug: summarize key CQPSK/TED state after retune when DSD_NEO_DEBUG_CQPSK=1.
       This runs in the controller thread after hardware retune and SPS refresh. */
    {
        if (debug_cqpsk_enabled()) {
            /* Use the OP25-compatible FLL band-edge state, not legacy fll_freq.
             * Convert rad/sample to Hz for readability. */
            float fll_freq_hz = s->fll_band_edge_state.freq * ((float)s->rate_out / 6.28318530717958647692f);
            float costas_freq_hz =
                s->costas_state.freq
                * (((float)s->rate_out / (float)(s->ted_sps > 0 ? s->ted_sps : 5)) / 6.28318530717958647692f);
            fprintf(stderr,
                    "[RETUNE] ted_sps=%d override=%d cqpsk=%d fll_freq=%.1fHz fll_phase=%.3f costas_freq=%.1fHz "
                    "costas_phase=%.3f gardner_omega=%.3f gardner_mu=%.3f\n",
                    s->ted_sps, s->ted_sps_override, s->cqpsk_enable, fll_freq_hz, s->fll_band_edge_state.phase,
                    costas_freq_hz, s->costas_state.phase, s->ted_state.omega, s->ted_state.mu);
        }
    }
}

/**
 * @brief Demodulation worker: consume input ring, run pipeline, and produce audio.
 *
 * Reads baseband I/Q blocks from the input ring, invokes the full demodulation
 * pipeline, and writes audio samples to the output ring with optional
 * resampling or legacy upsampling. Runs until global exit flag is set.
 *
 * @param arg Pointer to `demod_state`.
 * @return NULL on exit.
 */
/* SNR buffers for different modulations
 * NOTE: These are updated from the demod thread and read from the UI thread.
 * Use atomics to avoid data races and stale reads. Relaxed ordering is
 * sufficient because we only need value coherence, not synchronization. */
#include <atomic>
std::atomic<double> g_snr_c4fm_db{-100.0};
std::atomic<double> g_snr_qpsk_db{-100.0};
std::atomic<double> g_snr_gfsk_db{-100.0};
/* Supervisory tuner autogain gate owned by rtl_metrics.cpp */
extern std::atomic<int> g_tuner_autogain_on;
/* Track recency and source of SNR updates: src 1=direct (symbols), 2=fallback (eye/constellation) */
std::atomic<long long> g_snr_c4fm_last_ms{0};
std::atomic<int> g_snr_c4fm_src{0};
std::atomic<long long> g_snr_qpsk_last_ms{0};
std::atomic<int> g_snr_qpsk_src{0};
std::atomic<long long> g_snr_gfsk_last_ms{0};
std::atomic<int> g_snr_gfsk_src{0};
/* EMA state for direct SNR estimation (reset on retune for fast acquisition).
 * These are atomic because snr_ema_reset() is called from the controller thread
 * while the demod thread reads/writes them during SNR computation. */
static std::atomic<double> g_snr_ema_c4fm{-100.0};
static std::atomic<double> g_snr_ema_qpsk{-100.0};
static std::atomic<double> g_snr_ema_gfsk{-100.0};
/* QPSK accumulator reset flag (actual buffer is in demod loop) */
static std::atomic<int> g_snr_qpsk_acc_reset{0};

static void
snr_ema_reset(void) {
    g_snr_ema_c4fm.store(-100.0, std::memory_order_relaxed);
    g_snr_ema_qpsk.store(-100.0, std::memory_order_relaxed);
    g_snr_ema_gfsk.store(-100.0, std::memory_order_relaxed);
    g_snr_c4fm_db.store(-100.0, std::memory_order_relaxed);
    g_snr_qpsk_db.store(-100.0, std::memory_order_relaxed);
    g_snr_gfsk_db.store(-100.0, std::memory_order_relaxed);
    g_snr_c4fm_src.store(0, std::memory_order_relaxed);
    g_snr_qpsk_src.store(0, std::memory_order_relaxed);
    g_snr_gfsk_src.store(0, std::memory_order_relaxed);
    g_snr_qpsk_acc_reset.store(1, std::memory_order_relaxed);
}

/* Apply a single gain factor to the final demod block before handing it to consumers. */
static inline void
apply_output_scale(struct demod_state* d, float* buf, int len) {
    if (!d || !buf || len <= 0) {
        return;
    }
    float s = d->output_scale;
    if (s == 0.0f) {
        return;
    }
    if (s == 1.0f) {
        return;
    }
    for (int i = 0; i < len; i++) {
        buf[i] *= s;
    }
}

/* Fwd decl: eye-based C4FM SNR fallback */
extern "C" double dsd_rtl_stream_estimate_snr_c4fm_eye(void);
/* Fwd decl: QPSK and GFSK fallbacks */
extern "C" double dsd_rtl_stream_estimate_snr_qpsk_const(void);
extern "C" double dsd_rtl_stream_estimate_snr_gfsk_eye(void);

/**
 * @brief Get the current C4FM SNR estimator bias (exposed for UI/external use).
 * @return Bias in dB, computed dynamically based on current DSP settings.
 */
extern "C" double
dsd_rtl_stream_get_snr_bias_c4fm(void) {
    return dsd_snr_bias_c4fm_db(demod.rate_out, demod.ted_sps, demod.channel_lpf_profile);
}

/**
 * @brief Get the current EVM/GFSK/QPSK SNR estimator bias (exposed for UI/external use).
 * @return Bias in dB, computed dynamically based on current DSP settings.
 */
extern "C" double
dsd_rtl_stream_get_snr_bias_evm(void) {
    return dsd_snr_bias_evm_db(demod.rate_out, demod.ted_sps, demod.channel_lpf_profile);
}

/* Fwd decl: spectrum snapshot getter used for spectral SNR gating */
extern "C" int dsd_rtl_stream_spectrum_get(float* out_db, int max_bins, int* out_rate);
/* Tuner autogain runtime get/set (implemented in rtl_sdr_fm.cpp) */
extern "C" int dsd_rtl_stream_get_tuner_autogain(void);
extern "C" void dsd_rtl_stream_set_tuner_autogain(int onoff);

/* Spectrum updater used in demod thread (implemented in rtl_metrics.cpp). */
extern "C" void rtl_metrics_update_spectrum_from_iq(const float* iq_interleaved, int len_interleaved, int out_rate_hz);

static DSD_THREAD_RETURN_TYPE
#if DSD_PLATFORM_WIN_NATIVE
    __stdcall
#endif
    demod_thread_fn(void* arg) {
    struct demod_state* d = static_cast<demod_state*>(arg);
    struct output_state* o = d->output_target;
    maybe_set_thread_realtime_and_affinity("DEMOD");
    /* Optional supervisory tuner autogain (env-gated).
       Goals:
       - Avoid constant twitching: throttle adjustments and add retune/scan holdoff.
       - Be conservative on quiet systems: allow down-steps anytime on clipping,
         but only permit up-steps when a carrier is actually present (squelch open
         and reasonable SNR).
       - If device starts in driver auto-gain, we may exit into manual on threshold
         events and continue supervising thereafter. */
    static int ag_initialized = 0;
    static int ag_blocks = 0, ag_high = 0, ag_low = 0;
    static int ag_manual_target = 180;    /* 18.0 dB initial manual target if needed */
    static int ag_target_initialized = 0; /* latched from current driver gain on first window */
    /* Adjustment throttle (ms) and retune holdoff (ms) */
    static auto ag_next_allowed = std::chrono::steady_clock::now();
    static auto ag_hold_until = std::chrono::steady_clock::time_point{};
    static auto ag_probe_until = std::chrono::steady_clock::time_point{};
    static uint32_t ag_last_freq = 0;
    const int ag_throttle_ms = 1500; /* min interval between changes */
    const int ag_hold_ms = 1200;     /* pause after retune/scanning before adjusting */
    /* Up-step gating: now uses spectral SNR + in-band power ratio (see below). */
    /* Probe window: allow RTL device auto-gain to settle before any takeover. */
    static int s_probe_ms = 3000; /* allow device AGC to work first */
    /* Manual seed gain used when exiting device auto due to persistently low level. */
    static int s_seed_gain_db10 = 300; /* 30.0 dB in tenth-dB units */
    /* Spectral gating for up-steps: thresholds and persistence */
    static double s_ag_spec_snr_db = 6.0;   /* min spectral SNR (dB) within channel */
    static double s_ag_inband_ratio = 0.60; /* min fraction of power near center */
    static int s_ag_up_step_db10 = 30;      /* up-step size in tenth-dB (default +3.0 dB) */
    static int s_ag_up_persist = 2;         /* require consecutive passes before stepping up */
    static int ag_spec_pass = 0;            /* persistence counter for spectral gate */
    const int is_rtltcp_input = (g_stream && g_stream->opts && g_stream->opts->rtltcp_enabled) ? 1 : 0;
    while (!exitflag && !(g_stream && g_stream->should_exit.load())) {
        /* Preserve rtltcp prebuffer: hold the consumer until cold start finishes. */
        if (is_rtltcp_input && !controller.cold_start_ready.load(std::memory_order_acquire)) {
            dsd_sleep_ms(1); /* short sleep to avoid busy spinning */
            continue;
        }
        /* Honor pending purge requests in the consumer thread to keep SPSC ownership intact. */
        if (g_ring_purge_pending.exchange(0, std::memory_order_acq_rel)) {
            input_ring_discard_all_consumer(&input_ring);
            continue;
        }
        /* Read a block from input ring */
        int got = input_ring_read_block(&input_ring, d->input_cb_buf, static_cast<size_t>(MAXIMUM_BUF_LENGTH));
        if (got <= 0) {
            continue;
        }
        if (!ag_initialized) {
            const dsdneoRuntimeConfig* cfg = (g_stream && g_stream->cfg) ? g_stream->cfg : dsd_neo_get_config();
            if (cfg) {
                g_tuner_autogain_on.store(cfg->tuner_autogain_enable ? 1 : 0, std::memory_order_relaxed);
                s_probe_ms = cfg->tuner_autogain_probe_ms;
                s_seed_gain_db10 = (int)lrint(cfg->tuner_autogain_seed_db * 10.0);
                s_ag_spec_snr_db = cfg->tuner_autogain_spec_snr_db;
                s_ag_inband_ratio = cfg->tuner_autogain_inband_ratio;
                s_ag_up_step_db10 = (int)lrint(cfg->tuner_autogain_up_step_db * 10.0);
                s_ag_up_persist = cfg->tuner_autogain_up_persist;
            }
            ag_initialized = 1;
        }
        /* Update simple occupancy metrics on pre-DSP input for autogain */
        if (g_tuner_autogain_on.load(std::memory_order_relaxed)) {
            /* Detect retune and apply short holdoff to avoid reacting on noise */
            if (ag_last_freq != dongle.freq) {
                ag_last_freq = dongle.freq;
                ag_blocks = ag_high = ag_low = 0;
                ag_hold_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ag_hold_ms);
                /* On retune, defer any takeover to allow device auto to settle */
                if (s_probe_ms > 0) {
                    ag_probe_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(s_probe_ms);
                } else {
                    ag_probe_until = std::chrono::steady_clock::time_point{};
                }
                ag_spec_pass = 0; /* reset spectral gate persistence on retune */
            }
            float max_abs = 0.0f;
            double sum_abs = 0.0;
            int pairs = got >> 1;
            const float* p = d->input_cb_buf;
            for (int n = 0; n < pairs; n++) {
                float I = p[(size_t)(n << 1) + 0];
                float Q = p[(size_t)(n << 1) + 1];
                float aI = fabsf(I);
                float aQ = fabsf(Q);
                float m = (aI > aQ) ? aI : aQ;
                if (m > max_abs) {
                    max_abs = m;
                }
                sum_abs += (aI + aQ);
            }
            float mean_abs = (pairs > 0) ? (float)(sum_abs / (double)pairs) : 0.0f;
            ag_blocks++;
            if (max_abs > 0.9f) {
                ag_high++;
            }
            if (mean_abs < 0.06f) {
                ag_low++;
            }
            /* Every ~40 blocks, consider a nudge */
            if (ag_blocks >= 40) {
                /* Initialize manual target from current driver state once */
                if (!ag_target_initialized) {
                    int cg = rtl_device_get_tuner_gain(rtl_device_handle);
                    int is_auto_boot = rtl_device_is_auto_gain(rtl_device_handle);
                    /* If the device is already in manual mode, honor the exact manual setting, including 0 dB.
                       When still in device auto, skip latching (to avoid capturing a synthetic 0).
                       We'll bootstrap out of auto separately if needed. */
                    if (!is_auto_boot && cg >= 0) {
                        ag_manual_target = cg;
                    }
                    ag_target_initialized = 1;
                }
                /* Respect retune holdoff and throttle */
                auto now = std::chrono::steady_clock::now();
                bool in_hold = (ag_hold_until.time_since_epoch().count() != 0) && (now < ag_hold_until);
                bool in_probe = (ag_probe_until.time_since_epoch().count() != 0) && (now < ag_probe_until)
                                && (rtl_device_is_auto_gain(rtl_device_handle) > 0);
                bool throttled = now < ag_next_allowed;

                if (!in_hold && !throttled) {
                    int is_auto = rtl_device_is_auto_gain(rtl_device_handle);
                    bool changed = false;
                    /* During probe window, let device AGC act; skip takeover/adjustments unless clipping. */
                    if (in_probe) {
                        if (ag_high >= 3) {
                            /* Severe clipping even in auto: take control and step down. */
                            int seed = s_seed_gain_db10;
                            if (seed < 0) {
                                seed = 0;
                            }
                            if (seed > 490) {
                                seed = 490;
                            }
                            ag_manual_target = seed - 50; /* start slightly below seed when clipping */
                            if (ag_manual_target < 0) {
                                ag_manual_target = 0;
                            }
                            rtl_device_set_gain_nearest(rtl_device_handle, ag_manual_target);
                            dongle.gain = ag_manual_target;
                            ag_next_allowed = now + std::chrono::milliseconds(ag_throttle_ms);
                            LOG_INFO("AUTOGAIN: exiting probe due to clipping; set ~%d.%d dB.\n", ag_manual_target / 10,
                                     ag_manual_target % 10);
                        }
                        goto after_adjustments; /* skip below logic during probe */
                    }
                    /* One-time bootstrap: if device is still in auto and input level is consistently low,
                       exit auto into a reasonable manual gain even when SNR is not yet measurable. */
                    if (is_auto > 0 && ag_high == 0 && ag_low >= (ag_blocks * 3) / 4) {
                        int kick = s_seed_gain_db10;
                        if (kick < 0) {
                            kick = 0;
                        }
                        if (kick > 490) {
                            kick = 490;
                        }
                        rtl_device_set_gain_nearest(rtl_device_handle, kick);
                        dongle.gain = kick;
                        ag_manual_target = kick; /* keep target in sync with seeded manual gain */
                        ag_next_allowed = now + std::chrono::milliseconds(ag_throttle_ms);
                        LOG_INFO("AUTOGAIN: bootstrapping from device auto to ~%d.%d dB due to low input level.\n",
                                 kick / 10, kick % 10);
                        /* After exiting auto, subsequent adjustments use normal thresholds */
                        changed = false; /* already applied */
                    }
                    /* Always allow downward steps on clipping */
                    if (ag_high >= 3) {
                        ag_manual_target -= 50; /* -5.0 dB */
                        if (ag_manual_target < 0) {
                            ag_manual_target = 0;
                        }
                        changed = true;
                    } else {
                        /* Only consider upward steps when squelch gate is open and spectral SNR indicates
                           a dominant in-band carrier. Avoid chasing spurs or OOB energy by requiring:
                           - spectral SNR >= threshold (peak - median)
                           - energy concentrated near DC (central band ratio)
                           - reject sharp isolated DC spikes
                           - persistence over s_ag_up_persist windows */
                        bool spec_ok = false;
                        if (d->squelch_gate_open) {
                            const int kLocalMaxSpec = 1024;
                            float spec_db[kLocalMaxSpec];
                            int rate_hz = 0;
                            int N = dsd_rtl_stream_spectrum_get(spec_db, kLocalMaxSpec, &rate_hz);
                            if (N >= 64 && N <= kLocalMaxSpec) {
                                int i_max = 0;
                                float p_max = -1e30f;
                                for (int i = 0; i < N; i++) {
                                    float v = spec_db[i];
                                    if (v > p_max) {
                                        p_max = v;
                                        i_max = i;
                                    }
                                }
                                /* median noise (dB) */
                                float tmp_med[kLocalMaxSpec];
                                for (int i = 0; i < N; i++) {
                                    tmp_med[i] = spec_db[i];
                                }
                                int mid = N / 2;
                                std::nth_element(tmp_med, tmp_med + mid, tmp_med + N);
                                float noise_med_db = tmp_med[mid];
                                float spec_snr_db = p_max - noise_med_db;
                                /* DC spur guard */
                                int k_center = N / 2;
                                bool dc_spur = false;
                                if (i_max == k_center && i_max > 0 && i_max + 1 < N) {
                                    float l = spec_db[i_max - 1];
                                    float r = spec_db[i_max + 1];
                                    float side_max = (l > r) ? l : r;
                                    if ((p_max - side_max) > 12.0f) {
                                        dc_spur = true;
                                    }
                                }
                                /* In-band ratio: central +/- N/8 bins */
                                int half = N / 8;
                                if (half < 2) {
                                    half = 2;
                                }
                                int i0 = k_center - half;
                                int i1 = k_center + half;
                                if (i0 < 0) {
                                    i0 = 0;
                                }
                                if (i1 > (N - 1)) {
                                    i1 = N - 1;
                                }
                                double sum_all = 0.0, sum_center = 0.0;
                                for (int i = 0; i < N; i++) {
                                    /* convert dB to linear power */
                                    double p = pow(10.0, (double)spec_db[i] / 10.0);
                                    sum_all += p;
                                    if (i >= i0 && i <= i1) {
                                        sum_center += p;
                                    }
                                }
                                double ratio_center = (sum_all > 0.0) ? (sum_center / sum_all) : 0.0;
                                /* Require peak within central band */
                                bool peak_in_center = (i_max >= i0 && i_max <= i1);
                                bool gate = (!dc_spur) && peak_in_center && (spec_snr_db >= s_ag_spec_snr_db)
                                            && (ratio_center >= s_ag_inband_ratio);
                                if (gate) {
                                    ag_spec_pass++;
                                } else {
                                    ag_spec_pass = 0;
                                }
                                spec_ok = (ag_spec_pass >= s_ag_up_persist);
                            } else {
                                ag_spec_pass = 0;
                            }
                        } else {
                            ag_spec_pass = 0;
                        }
                        if (spec_ok && ag_low >= (ag_blocks * 3) / 4) {
                            ag_manual_target += s_ag_up_step_db10; /* up-step */
                            if (ag_manual_target > 490) {
                                ag_manual_target = 490;
                            }
                            changed = true;
                        }
                    }

                    if (changed) {
                        /* Apply manual gain near target regardless of prior auto/manual. */
                        rtl_device_set_gain_nearest(rtl_device_handle, ag_manual_target);
                        dongle.gain = ag_manual_target;
                        ag_next_allowed = now + std::chrono::milliseconds(ag_throttle_ms);
                        ag_spec_pass = 0; /* reset persistence after applying */
                        if (is_auto > 0) {
                            LOG_INFO("AUTOGAIN: threshold hit; exiting device auto and setting ~%d.%d dB.\n",
                                     ag_manual_target / 10, ag_manual_target % 10);
                        } else {
                            LOG_INFO("AUTOGAIN: adjusting manual gain to ~%d.%d dB.\n", ag_manual_target / 10,
                                     ag_manual_target % 10);
                        }
                    }
                after_adjustments:;
                }
                ag_blocks = ag_high = ag_low = 0;
            }
        }
        d->lowpassed = d->input_cb_buf;
        d->lp_len = got;
        /* Gate: skip demod processing until controller signals cold start complete.
         * This prevents the race where we process samples with uninitialized
         * TED/AGC/filter state, causing symbol timing or gain to lock incorrectly.
         * Applies to all modulations (C4FM, GFSK, QPSK, etc). */
        if (!controller.cold_start_ready.load(std::memory_order_acquire)) {
            continue;
        }
        /* Gate: skip demod processing while a retune is in progress.
         * This prevents the race where we process transient/stale samples during
         * hardware retune before TED/AGC/filters are reset for the new frequency.
         * Applies to all modulations (C4FM, GFSK, QPSK, etc). */
        if (controller.retune_in_progress.load(std::memory_order_acquire)) {
            continue;
        }
        full_demod(d);
        /* Capture decimated I/Q for constellation view after DSP. */
        extern void constellation_ring_append(const float* iq, int len, int sps_hint);
        constellation_ring_append(d->lowpassed, d->lp_len, d->ted_sps);
        /* Capture I-channel for eye diagram */
        eye_ring_append_i_chan(d->lowpassed, d->lp_len);
        /* Update spectrum snapshot from post-filter complex baseband */
        rtl_metrics_update_spectrum_from_iq(d->lowpassed, d->lp_len, d->rate_out);

        /* Estimate SNR per modulation using post-filter samples */
        static int c4fm_missed = 0; /* counts loops without C4FM SNR update */
        static int qpsk_missed = 0; /* counts loops without QPSK SNR update */
        static int gfsk_missed = 0; /* counts loops without GFSK SNR update */
        const float* iq = d->lowpassed;
        const int n_iq = d->lp_len;
        const int sps = d->ted_sps;
        if (iq && n_iq >= 4 && sps >= 2) {
            const int pairs = n_iq / 2;
            const int mid = sps / 2;
            int win = sps / 10;
            if (win < 1) {
                win = 1;
            }
            if (win > mid) {
                win = mid;
            }
            bool qpsk_updated = false;
            bool c4fm_updated = false;
            bool gfsk_updated = false;

            /* QPSK/CQPSK: compute SNR directly from symbol-rate I/Q samples */
            /* Accumulate across blocks since individual blocks may be small */
            enum { QPSK_ACCUM_MAX = 256 };

            static float qpsk_acc_i[QPSK_ACCUM_MAX], qpsk_acc_q[QPSK_ACCUM_MAX];
            static int qpsk_acc_n = 0;
            /* Check for reset request from retune */
            if (g_snr_qpsk_acc_reset.exchange(0, std::memory_order_relaxed)) {
                qpsk_acc_n = 0;
            }
            if (sps >= 2 && sps <= 12) {
                /* Extract symbol-center I/Q pairs and accumulate */
                for (int k = 0; k < pairs && qpsk_acc_n < QPSK_ACCUM_MAX; k++) {
                    int phase = k % sps;
                    if (phase >= mid - win && phase <= mid + win) {
                        qpsk_acc_i[qpsk_acc_n] = iq[2 * k + 0];
                        qpsk_acc_q[qpsk_acc_n] = iq[2 * k + 1];
                        qpsk_acc_n++;
                    }
                }
                int m = qpsk_acc_n;
                if (m >= 64) {
                    /* Per-axis normalization (handles I/Q gain imbalance) */
                    double sum_abs_i = 0.0, sum_abs_q = 0.0;
                    for (int i = 0; i < m; i++) {
                        sum_abs_i += fabs((double)qpsk_acc_i[i]);
                        sum_abs_q += fabs((double)qpsk_acc_q[i]);
                    }
                    double aI = sum_abs_i / (double)m;
                    double aQ = sum_abs_q / (double)m;
                    if (aI > 1e-9 && aQ > 1e-9) {
                        /* Compute error to nearest QPSK target (axis-aligned) */
                        double e2 = 0.0;
                        for (int i = 0; i < m; i++) {
                            double I = (double)qpsk_acc_i[i];
                            double Q = (double)qpsk_acc_q[i];
                            double ti = (I >= 0.0) ? aI : -aI;
                            double tq = (Q >= 0.0) ? aQ : -aQ;
                            double ei = I - ti;
                            double eq = Q - tq;
                            e2 += ei * ei + eq * eq;
                        }
                        double t2 = (double)m * (aI * aI + aQ * aQ);
                        if (e2 > 1e-12 && t2 > 1e-9) {
                            double ratio = t2 / e2;
                            double snr_raw = 10.0 * log10(ratio);
                            double bias = dsd_snr_bias_evm_db(d->rate_out, d->ted_sps, d->channel_lpf_profile);
                            double snr = snr_raw - bias;
                            double ema = g_snr_ema_qpsk.load(std::memory_order_relaxed);
                            if (ema < -50.0) {
                                ema = snr;
                            } else {
                                /* Faster EMA: 50% old, 50% new for quicker settling */
                                ema = 0.5 * ema + 0.5 * snr;
                            }
                            g_snr_ema_qpsk.store(ema, std::memory_order_relaxed);
                            g_snr_qpsk_db.store(ema, std::memory_order_relaxed);
                            g_snr_qpsk_src.store(1, std::memory_order_relaxed);
                            auto now = std::chrono::steady_clock::now();
                            long long ms =
                                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                            g_snr_qpsk_last_ms.store(ms, std::memory_order_relaxed);
                            qpsk_updated = true;
                        }
                    }
                    /* Reset accumulator after computing */
                    qpsk_acc_n = 0;
                }
            }
            /* Include low-SPS FSK paths (e.g., 5 sps ProVoice/EDACS) in the SNR estimator. */
            if (sps >= 4 && sps <= 12) {
                /* FSK family: compute both 4-level (C4FM) and 2-level (GFSK-like) */
                enum { MAXS = 8192 };

                static float vals[(size_t)MAXS];
                int m = 0;
                for (int k = 0; k < pairs && m < MAXS; k++) {
                    int phase = k % sps;
                    if (phase >= mid - win && phase <= mid + win) {
                        vals[m++] = iq[2 * k + 0]; /* I-channel */
                    }
                }
                if (m > 32) {
                    /* Compute quartiles in O(n) using nth_element */
                    int idx1 = (int)((size_t)m / 4);
                    int idx2 = (int)((size_t)m / 2);
                    int idx3 = (int)((size_t)(3 * (size_t)m) / 4);
                    std::nth_element(vals, vals + idx2, vals + m);
                    float q2 = vals[idx2];
                    std::nth_element(vals, vals + idx1, vals + idx2);
                    float q1 = vals[idx1];
                    std::nth_element(vals + idx2 + 1, vals + idx3, vals + m);
                    float q3 = vals[idx3];
                    /* 4-level (C4FM-like) */
                    {
                        double sum[4] = {0, 0, 0, 0};
                        int cnt[4] = {0, 0, 0, 0};
                        for (int i = 0; i < m; i++) {
                            float v = vals[i];
                            int b = (v <= q1) ? 0 : (v <= q2) ? 1 : (v <= q3) ? 2 : 3;
                            sum[b] += v;
                            cnt[b]++;
                        }
                        if (cnt[0] && cnt[1] && cnt[2] && cnt[3]) {
                            double mu[4];
                            int total = 0;
                            for (int b = 0; b < 4; b++) {
                                mu[b] = sum[b] / (double)cnt[b];
                                total += cnt[b];
                            }
                            double nsum = 0.0;
                            for (int i = 0; i < m; i++) {
                                float v = vals[i];
                                int b = (v <= q1) ? 0 : (v <= q2) ? 1 : (v <= q3) ? 2 : 3;
                                double e = (double)v - mu[b];
                                nsum += e * e;
                            }
                            double noise_var = nsum / (double)total;
                            if (noise_var > 1e-9) {
                                double mu_all = 0.0;
                                for (int b = 0; b < 4; b++) {
                                    mu_all += mu[b] * (double)cnt[b] / (double)total;
                                }
                                double ssum = 0.0;
                                for (int b = 0; b < 4; b++) {
                                    double d = mu[b] - mu_all;
                                    ssum += (double)cnt[b] * d * d;
                                }
                                double sig_var = ssum / (double)total;
                                if (sig_var > 1e-9) {
                                    double bias = dsd_snr_bias_c4fm_db(d->rate_out, d->ted_sps, d->channel_lpf_profile);
                                    double snr = 10.0 * log10(sig_var / noise_var) - bias;
                                    double ema = g_snr_ema_c4fm.load(std::memory_order_relaxed);
                                    if (ema < -50.0) {
                                        ema = snr;
                                    } else {
                                        /* Faster EMA: 50% old, 50% new for quicker settling */
                                        ema = 0.5 * ema + 0.5 * snr;
                                    }
                                    g_snr_ema_c4fm.store(ema, std::memory_order_relaxed);
                                    g_snr_c4fm_db.store(ema, std::memory_order_relaxed);
                                    g_snr_c4fm_src.store(1, std::memory_order_relaxed);
                                    auto now = std::chrono::steady_clock::now();
                                    long long ms =
                                        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
                                            .count();
                                    g_snr_c4fm_last_ms.store(ms, std::memory_order_relaxed);
                                    c4fm_updated = true;
                                }
                            }
                        }
                        /* 2-level (GFSK-like) using median split */
                        {
                            double sumL = 0.0, sumH = 0.0;
                            int cntL = 0, cntH = 0;
                            for (int i = 0; i < m; i++) {
                                float v = vals[i];
                                if (v <= q2) {
                                    sumL += v;
                                    cntL++;
                                } else {
                                    sumH += v;
                                    cntH++;
                                }
                            }
                            if (cntL > 0 && cntH > 0) {
                                double muL = sumL / (double)cntL, muH = sumH / (double)cntH;
                                int total = cntL + cntH;
                                double nsum = 0.0;
                                for (int i = 0; i < m; i++) {
                                    float v = vals[i];
                                    double mu = (v <= q2) ? muL : muH;
                                    double e = (double)v - mu;
                                    nsum += e * e;
                                }
                                double noise_var = nsum / (double)total;
                                if (noise_var > 1e-9) {
                                    double mu_all = (muL * (double)cntL + muH * (double)cntH) / (double)total;
                                    double ssum = (double)cntL * (muL - mu_all) * (muL - mu_all)
                                                  + (double)cntH * (muH - mu_all) * (muH - mu_all);
                                    double sig_var = ssum / (double)total;
                                    if (sig_var > 1e-9) {
                                        double bias =
                                            dsd_snr_bias_evm_db(d->rate_out, d->ted_sps, d->channel_lpf_profile);
                                        double snr = 10.0 * log10(sig_var / noise_var) - bias;
                                        double ema = g_snr_ema_gfsk.load(std::memory_order_relaxed);
                                        if (ema < -50.0) {
                                            ema = snr;
                                        } else {
                                            /* Faster EMA: 50% old, 50% new for quicker settling */
                                            ema = 0.5 * ema + 0.5 * snr;
                                        }
                                        g_snr_ema_gfsk.store(ema, std::memory_order_relaxed);
                                        g_snr_gfsk_db.store(ema, std::memory_order_relaxed);
                                        g_snr_gfsk_src.store(1, std::memory_order_relaxed);
                                        auto now = std::chrono::steady_clock::now();
                                        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                           now.time_since_epoch())
                                                           .count();
                                        g_snr_gfsk_last_ms.store(ms, std::memory_order_relaxed);
                                        gfsk_updated = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            /* If no C4FM update occurred for a while, synthesize a value from the eye buffer */
            if (!c4fm_updated) {
                if (++c4fm_missed >= 50) { /* ~guard against long stalls */
                    double fb = dsd_rtl_stream_estimate_snr_c4fm_eye();
                    if (fb > -50.0) {
                        double prev = g_snr_c4fm_db.load(std::memory_order_relaxed);
                        /* Slightly quicker fallback blending to avoid long stalls */
                        double blended = (prev < -50.0) ? fb : (0.8 * prev + 0.2 * fb);
                        g_snr_c4fm_db.store(blended, std::memory_order_relaxed);
                        g_snr_c4fm_src.store(2, std::memory_order_relaxed);
                        auto now = std::chrono::steady_clock::now();
                        long long ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                        g_snr_c4fm_last_ms.store(ms, std::memory_order_relaxed);
                    }
                    c4fm_missed = 0;
                }
            } else {
                c4fm_missed = 0;
            }
            /* If no QPSK update, synthesize from the constellation snapshot */
            if (!qpsk_updated) {
                if (++qpsk_missed >= 10) {
                    double fb = dsd_rtl_stream_estimate_snr_qpsk_const();
                    if (fb > -50.0) {
                        double prev = g_snr_qpsk_db.load(std::memory_order_relaxed);
                        /* Fast initial acquisition, then slower tracking for stability */
                        double alpha = (prev < -50.0) ? 1.0 : 0.5;
                        double blended = alpha * fb + (1.0 - alpha) * prev;
                        g_snr_qpsk_db.store(blended, std::memory_order_relaxed);
                        g_snr_qpsk_src.store(2, std::memory_order_relaxed);
                        auto now = std::chrono::steady_clock::now();
                        long long ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                        g_snr_qpsk_last_ms.store(ms, std::memory_order_relaxed);
                    }
                    qpsk_missed = 0;
                }
            } else {
                qpsk_missed = 0;
            }
            /* If no GFSK update, synthesize from the eye buffer */
            if (!gfsk_updated) {
                if (++gfsk_missed >= 50) {
                    double fb = dsd_rtl_stream_estimate_snr_gfsk_eye();
                    if (fb > -50.0) {
                        double prev = g_snr_gfsk_db.load(std::memory_order_relaxed);
                        /* Slightly quicker fallback blending to avoid long stalls */
                        double blended = (prev < -50.0) ? fb : (0.8 * prev + 0.2 * fb);
                        g_snr_gfsk_db.store(blended, std::memory_order_relaxed);
                        g_snr_gfsk_src.store(2, std::memory_order_relaxed);
                        auto now = std::chrono::steady_clock::now();
                        long long ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                        g_snr_gfsk_last_ms.store(ms, std::memory_order_relaxed);
                    }
                    gfsk_missed = 0;
                }
            } else {
                gfsk_missed = 0;
            }
        }

        if (d->exit_flag) {
            exitflag = 1;
        }
        /* Frequency hop on squelch: when channel power is below threshold, signal
         * the controller to try the next frequency in a scan list.
         * Note: We still write samples to the ring buffer (don't use 'continue') to
         * maintain continuous flow for UI responsiveness. The hop signal is async. */
        if (d->channel_squelch_level > 0.0f && d->channel_squelched) {
            d->squelch_hits++;
            if (d->squelch_hits > d->conseq_squelch) {
                d->squelch_hits = d->conseq_squelch + 1; /* hair trigger */
                safe_cond_signal(&controller.hop, &controller.hop_m);
                /* Don't 'continue' here - still write zeros to keep pipeline flowing */
            }
        } else {
            d->squelch_hits = 0;
        }
        /* For CQPSK mode, bypass the resampler entirely. The TED already decimates
         * to symbol rate, and the symbol stream should go directly to the ring buffer
         * without upsampling. The downstream symbol reader expects 1 sample per symbol.
         * Upsampling would cause every-other-symbol to be read, corrupting the data. */
        if (d->cqpsk_enable) {
            /* CQPSK: direct symbol passthrough, no resampling or scaling */
            if (d->result_len > 0) {
                ring_write_signal_on_empty_transition(o, d->result, (size_t)d->result_len);
            }
        } else if (d->resamp_enabled) {
            int out_n = resamp_process_block(d, d->result, d->result_len, d->resamp_outbuf);
            if (out_n > 0) {
                apply_output_scale(d, d->resamp_outbuf, out_n);
                ring_write_signal_on_empty_transition(o, d->resamp_outbuf, (size_t)out_n);
            }
        } else {
            /* When resampler is disabled, pass-through. */
            if (d->result_len > 0) {
                apply_output_scale(d, d->result, d->result_len);
                ring_write_signal_on_empty_transition(o, d->result, (size_t)d->result_len);
            }
        }
        /* Signaling occurs only when the ring transitions from empty to non-empty. */
    }
    DSD_THREAD_RETURN;
}

/**
 * @brief Compute and stage tuner/demodulator capture settings based on the
 * requested center frequency and current demod configuration. The actual
 * device programming occurs elsewhere after these fields are updated.
 *
 * @param freq Desired RF center frequency in Hz.
 * @param rate Current input sample rate (unused).
 */
static void
optimal_settings(int freq, int rate) {
    UNUSED(rate);

    int capture_freq, capture_rate;
    struct dongle_state* d = &dongle;
    struct demod_state* dm = &demod;
    struct controller_state* cs = &controller;
    /* Compute integer oversample factor to target ~1 MS/s capture then map
       to a cascade of 2:1 decimators via passes = ceil(log2(ds)). */
    int downsample_factor = (1000000 / dm->rate_in) + 1;
    {
        int ds = downsample_factor;
        if (ds <= 1) {
            dm->downsample_passes = 0;
            downsample_factor = 1;
        } else {
#if defined(__GNUC__) || defined(__clang__)
            int floor_log2 = 31 - __builtin_clz(ds);
#else
            int floor_log2 = 0;
            {
                int t = ds;
                while (t >>= 1) {
                    floor_log2++;
                }
            }
#endif
            int is_pow2 = (ds & (ds - 1)) == 0;
            int passes = is_pow2 ? floor_log2 : (floor_log2 + 1);
            if (passes < 0) {
                passes = 0;
            }
            if (passes > 10) {
                passes = 10; /* practical guard */
            }
            /* Small adjustment: prefer capture rates that fall near known-stable
               RTL2832U clocks (e.g., 960k, 1024k, 1200k, 1536k, 1920k, 2048k, 2400k).
               We keep the power-of-two structure by choosing among nearby pass counts. */
            auto choose_passes_near_good_rate = [&](int rate_in_hz, int suggested_passes) {
                const int good_rates[] = {960000, 1024000, 1200000, 1536000, 1920000, 2048000, 2400000};
                int best_p = suggested_passes;
                long long best_err = LLONG_MAX;
                for (int delta = -1; delta <= 1; delta++) {
                    int p = suggested_passes + delta;
                    if (p < 0) {
                        p = 0;
                    }
                    if (p > 10) {
                        p = 10;
                    }
                    long long cap = (long long)rate_in_hz * (1LL << p);
                    /* stay in a reasonable RTL range */
                    if (cap < 225000LL || cap > 3200000LL) {
                        continue;
                    }
                    for (size_t i = 0; i < sizeof(good_rates) / sizeof(good_rates[0]); i++) {
                        long long err = cap - (long long)good_rates[i];
                        if (err < 0) {
                            err = -err;
                        }
                        if (err < best_err) {
                            best_err = err;
                            best_p = p;
                        }
                    }
                }
                return best_p;
            };
            int adj_passes = choose_passes_near_good_rate(dm->rate_in, passes);
            dm->downsample_passes = adj_passes;
            downsample_factor = 1 << adj_passes;
        }
    }
    capture_freq = freq;
    capture_rate = downsample_factor * dm->rate_in;
    /* Apply fs/4 shift for zero-IF DC spur avoidance when offset_tuning is disabled. */
    if (!d->offset_tuning && !disable_fs4_shift) {
        capture_freq = freq + capture_rate / 4;
    }
    capture_freq += cs->edge * dm->rate_in / 2;
    /* Normalize discriminator radians into roughly [-1,1] for float pipeline. */
    dm->output_scale = (float)(1.0 / M_PI);
    /* Update the effective discriminator output sample rate based on current settings.
       HB cascade reduces by (1<<downsample_passes). Apply optional post_downsample on audio. */
    {
        int base_decim = (dm->downsample_passes > 0) ? (1 << dm->downsample_passes) : 1;
        int out_rate = capture_rate / base_decim;
        if (dm->post_downsample > 1) {
            out_rate /= dm->post_downsample;
            if (out_rate < 1) {
                out_rate = 1;
            }
        }
        dm->rate_out = out_rate;
    }
    d->freq = (uint32_t)capture_freq;
    d->rate = (uint32_t)capture_rate;
}

/**
 * @brief Program device to new center frequency and sample rate using a
 * single, consistent path. Applies fs/4 shift when offset_tuning is off.
 *
 * @param center_freq_hz Desired RF center frequency in Hz.
 */
static void
apply_capture_settings(uint32_t center_freq_hz) {
    optimal_settings((int)center_freq_hz, demod.rate_in);
    rtl_device_set_frequency(rtl_device_handle, dongle.freq);
    rtl_device_set_sample_rate(rtl_device_handle, dongle.rate);
    /* Use driver auto hardware bandwidth by default, or override via env */
    rtl_device_set_tuner_bandwidth(rtl_device_handle, choose_tuner_bw_hz(dongle.rate, (uint32_t)rtl_dsp_bw_hz));
    /* Sync to actual device rate (USB may quantize). If it changed, update rate_out. */
    int actual = rtl_device_get_sample_rate(rtl_device_handle);
    if (actual > 0 && (uint32_t)actual != dongle.rate) {
        uint32_t prev = dongle.rate;
        dongle.rate = (uint32_t)actual;
        int base_decim = (demod.downsample_passes > 0) ? (1 << demod.downsample_passes) : 1;
        if (base_decim < 1) {
            base_decim = 1;
        }
        int out_rate = (int)(dongle.rate / (uint32_t)base_decim);
        if (demod.post_downsample > 1) {
            out_rate /= demod.post_downsample;
            if (out_rate < 1) {
                out_rate = 1;
            }
        }
        demod.rate_out = out_rate;
        LOG_INFO("Adjusted to actual device rate: requested=%u, actual=%u, demod_out=%d Hz.\n", prev, dongle.rate,
                 demod.rate_out);
    }
    /* Ensure TED SPS reflects the current effective sampling rate unless explicitly overridden. */
    rtl_demod_maybe_refresh_ted_sps_after_rate_change(&demod, g_stream ? g_stream->opts : NULL, &output);
}

/* Resampler and TED SPS helpers are implemented in rtl_demod_config.cpp. */

/**
 * @brief Controller worker: scans/hops through configured center frequencies.
 *
 * Programs tuner frequency/sample rate according to current optimal settings
 * and hops when signaled by the demod path (e.g., squelch-triggered).
 *
 * @param arg Pointer to `controller_state`.
 * @return NULL on exit.
 */
static DSD_THREAD_RETURN_TYPE
#if DSD_PLATFORM_WIN_NATIVE
    __stdcall
#endif
    controller_thread_fn(void* arg) {
    int i;
    struct controller_state* s = static_cast<controller_state*>(arg);

    if (s->wb_mode) {
        for (i = 0; i < s->freq_len; i++) {
            s->freqs[i] += 16000;
        }
    }

    /* set up primary channel */
    optimal_settings(s->freqs[0], demod.rate_in);
    if (dongle.direct_sampling) {
        rtl_device_set_direct_sampling(rtl_device_handle, dongle.direct_sampling);
    }

    /* Try enabling offset tuning before any other tuning calls (rtl_fm order).
       Respect explicit env override when provided. */
    {
        int want = 1;
        if (g_stream && g_stream->opts && g_stream->opts->rtltcp_enabled) {
            /* rtl_tcp: keep fs/4 + combine-rotate path consistent with USB defaults */
            want = 0;
        }
        const dsdneoRuntimeConfig* cfg = (g_stream && g_stream->cfg) ? g_stream->cfg : dsd_neo_get_config();
        if (cfg && cfg->rtl_offset_tuning_is_set) {
            want = cfg->rtl_offset_tuning_enable ? 1 : 0;
        }
        int r = rtl_device_set_offset_tuning_enabled(rtl_device_handle, want);
        if (r == 0) {
            dongle.offset_tuning = want ? 1 : 0;
        } else {
            dongle.offset_tuning = 0;
        }
    }

    /* Recompute capture parameters now that offset_tuning may have changed. */
    optimal_settings(s->freqs[0], demod.rate_in);

    /* Set the frequency then sample rate (rtl_fm order). */
    rtl_device_set_frequency(rtl_device_handle, dongle.freq);
    LOG_INFO("Oversampling input by: %ix.\n", (demod.downsample_passes > 0) ? (1 << demod.downsample_passes) : 1);
    LOG_INFO("Oversampling output by: %ix.\n", demod.post_downsample);
    LOG_INFO("Buffer size: %0.2fms\n", 1000 * 0.5 * (float)ACTUAL_BUF_LENGTH / (float)dongle.rate);

    /* Set the sample rate after frequency */
    rtl_device_set_sample_rate(rtl_device_handle, dongle.rate);
    /* USB may quantize the applied sample rate; sync and update out rate if changed. */
    {
        int actual = rtl_device_get_sample_rate(rtl_device_handle);
        if (actual > 0 && (uint32_t)actual != dongle.rate) {
            uint32_t prev = dongle.rate;
            dongle.rate = (uint32_t)actual;
            int base_decim = (demod.downsample_passes > 0) ? (1 << demod.downsample_passes) : 1;
            if (base_decim < 1) {
                base_decim = 1;
            }
            int out_rate = (int)(dongle.rate / (uint32_t)base_decim);
            if (demod.post_downsample > 1) {
                out_rate /= demod.post_downsample;
                if (out_rate < 1) {
                    out_rate = 1;
                }
            }
            demod.rate_out = out_rate;
            LOG_INFO("Adjusted to actual device rate: requested=%u, actual=%u, demod_out=%d Hz.\n", prev, dongle.rate,
                     demod.rate_out);
        }
    }
    /* Apply tuner IF bandwidth with mode-aware heuristic */
    rtl_device_set_tuner_bandwidth(rtl_device_handle, choose_tuner_bw_hz(dongle.rate, (uint32_t)rtl_dsp_bw_hz));
    LOG_INFO("Demod output at %u Hz.\n", (unsigned int)demod.rate_out);

    /* Cold start initialization: apply the same reset sequence used on retunes.
     *
     * Without this, the CQPSK chain has inconsistent startup behavior:
     *   1. TED SPS stays at init default (10) instead of correct value for rate/mode
     *   2. Costas loop and TED have stale/zero state instead of clean acquisition state
     *   3. RTL-SDR startup transients (~10-50ms of garbage) flow into demod
     *
     * These issues cause the Gardner TED to converge to wrong timing phase or the
     * Costas loop to fail acquisition on first run, while subsequent retunes work
     * because they go through demod_reset_on_retune(). */
    rtl_demod_maybe_refresh_ted_sps_after_rate_change(&demod, g_stream ? g_stream->opts : NULL, &output);
    demod_reset_on_retune(&demod);

    /* Signal demod thread that cold start initialization is complete.
     * The demod thread gates CQPSK processing on this flag to prevent the race
     * where it processes samples before TED SPS/Costas/AGC are properly reset. */
    s->cold_start_ready.store(1, std::memory_order_release);

    while (!exitflag && !(g_stream && g_stream->should_exit.load())) {
        /* Wait for a hop signal or a pending retune, with proper predicate guard */
        dsd_mutex_lock(&s->hop_m);
        while (!s->manual_retune_pending.load() && !exitflag && !(g_stream && g_stream->should_exit.load())) {
            dsd_cond_wait(&s->hop, &s->hop_m);
        }
        dsd_mutex_unlock(&s->hop_m);
        if (exitflag || (g_stream && g_stream->should_exit.load())) {
            break;
        }
        /* Process marshalled manual retunes first */
        if (s->manual_retune_pending.load()) {
            uint32_t tgt = s->manual_retune_freq;
            s->manual_retune_pending.store(0);
            /* Gate demod thread: prevent processing transient samples during retune */
            s->retune_in_progress.store(1, std::memory_order_release);
            /* Ask consumer to purge ring safely (keeps SPSC producer/consumer contract). */
            g_ring_purge_pending.store(1, std::memory_order_release);
            apply_capture_settings((uint32_t)tgt);
            rtl_demod_maybe_update_resampler_after_rate_change(&demod, &output, rtl_dsp_bw_hz);
            rtl_demod_maybe_refresh_ted_sps_after_rate_change(&demod, g_stream ? g_stream->opts : NULL, &output);
            demod_reset_on_retune(&demod);
            /* Retune complete: allow demod thread to resume processing */
            s->retune_in_progress.store(0, std::memory_order_release);
            /* Signal completion to any waiting callers (e.g., dsd_rtl_stream_tune).
             * Increment the complete_id to match the request_id that was generated
             * when the tune was requested. This ensures waiters wake up only for
             * their own request, handling the case where multiple retunes are queued. */
            dsd_mutex_lock(&s->retune_done_m);
            s->retune_complete_id.fetch_add(1, std::memory_order_release);
            dsd_cond_broadcast(&s->retune_done_cond);
            dsd_mutex_unlock(&s->retune_done_m);
            drain_output_on_retune();
            LOG_INFO("Retune applied: %u Hz.\n", tgt);
            continue;
        }
        if (s->freq_len <= 1) {
            continue;
        }
        s->freq_now = (s->freq_now + 1) % s->freq_len;
        /* Gate demod thread: prevent processing transient samples during hop */
        s->retune_in_progress.store(1, std::memory_order_release);
        /* Flush any leftover IQ from the previous frequency before applying the new center. */
        g_ring_purge_pending.store(1, std::memory_order_release);
        apply_capture_settings((uint32_t)s->freqs[s->freq_now]);
        rtl_demod_maybe_update_resampler_after_rate_change(&demod, &output, rtl_dsp_bw_hz);
        rtl_demod_maybe_refresh_ted_sps_after_rate_change(&demod, g_stream ? g_stream->opts : NULL, &output);
        demod_reset_on_retune(&demod);
        /* Hop complete: allow demod thread to resume processing */
        s->retune_in_progress.store(0, std::memory_order_release);
        drain_output_on_retune();
    }
    DSD_THREAD_RETURN;
}

/* ---------------- Constellation capture (simple lock-free ring) ---------------- */

static const int kConstMaxPairs = 8192;
static float g_const_xy[kConstMaxPairs * 2];
static volatile int g_const_head = 0; /* pairs written [0..kConstMaxPairs-1], wraps */

/**
 * @brief Clear the constellation ring buffer.
 *
 * Called on retune to prevent stale samples from the previous frequency/SPS
 * from contaminating the constellation display. Without this, the UI shows
 * a mix of old and new constellation points, creating the appearance of
 * degraded SNR even when the DSP is performing correctly.
 */
static void
constellation_ring_clear(void) {
    memset(g_const_xy, 0, sizeof(g_const_xy));
    g_const_head = 0;
}

/* Forward decl for eye-ring append used in demod loop */
static inline void eye_ring_append_i_chan(const float* iq_interleaved, int len_interleaved);

/* Append decimated I/Q samples from lowpassed[] after DSP. */
void
constellation_ring_append(const float* iq, int len, int sps_hint) {
    if (!iq || len < 2) {
        return;
    }
    int N = len >> 1;                                              /* complex samples */
    int stride = (sps_hint >= 1 && sps_hint <= 64) ? sps_hint : 4; /* rough decimation */
    if (stride < 1) {
        stride = 1;
    }
    for (int n = 0; n < N; n += stride) {
        float i = iq[(size_t)(n << 1) + 0];
        float q = iq[(size_t)(n << 1) + 1];
        int h = g_const_head;
        g_const_xy[(size_t)(h << 1) + 0] = i;
        g_const_xy[(size_t)(h << 1) + 1] = q;
        h++;
        if (h >= kConstMaxPairs) {
            h = 0;
        }
        g_const_head = h;
    }
}

extern "C" int
dsd_rtl_stream_constellation_get(float* out_xy, int max_points) {
    if (!out_xy || max_points <= 0) {
        return 0;
    }
    int head = g_const_head; /* snapshot */
    int n = (max_points < kConstMaxPairs) ? max_points : kConstMaxPairs;
    int start = head;
    for (int k = 0; k < n; k++) {
        int idx = (start + k) % kConstMaxPairs;
        out_xy[(size_t)(k << 1) + 0] = g_const_xy[(size_t)(idx << 1) + 0];
        out_xy[(size_t)(k << 1) + 1] = g_const_xy[(size_t)(idx << 1) + 1];
    }
    return n;
}

/* ---------------- Eye diagram capture (I-channel of complex baseband) ---------------- */
static const int kEyeMax = 16384;
static float g_eye_buf[kEyeMax];
static volatile int g_eye_head = 0; /* samples written [0..kEyeMax-1], wraps */

/**
 * @brief Clear the eye diagram ring buffer.
 *
 * Called on retune alongside constellation_ring_clear() to prevent stale
 * samples from contaminating the eye diagram display.
 */
static void
eye_ring_clear(void) {
    memset(g_eye_buf, 0, sizeof(g_eye_buf));
    g_eye_head = 0;
}

static inline void
eye_ring_append_i_chan(const float* iq_interleaved, int len_interleaved) {
    if (!iq_interleaved || len_interleaved < 2) {
        return;
    }
    int N = len_interleaved >> 1; /* complex samples */
    for (int n = 0; n < N; n++) {
        float i = iq_interleaved[(size_t)(n << 1) + 0];
        int h = g_eye_head;
        g_eye_buf[h] = i;
        h++;
        if (h >= kEyeMax) {
            h = 0;
        }
        g_eye_head = h;
    }
}

extern "C" int
dsd_rtl_stream_eye_get(float* out, int max_samples, int* out_sps) {
    if (out_sps) {
        *out_sps = demod.ted_sps;
    }
    if (!out || max_samples <= 0) {
        return 0;
    }
    int head = g_eye_head;
    int n = (max_samples < kEyeMax) ? max_samples : kEyeMax;
    int start = head;
    for (int k = 0; k < n; k++) {
        int idx = (start + k) % kEyeMax;
        out[k] = g_eye_buf[idx];
    }
    return n;
}

/* ---------------- Eye-based SNR estimation (C4FM fallback) ---------------- */
extern "C" double
dsd_rtl_stream_estimate_snr_c4fm_eye(void) {
    enum { MAXS = 4096 };

    static float eb[(size_t)MAXS];
    int sps_fb = 0;
    int nfb = dsd_rtl_stream_eye_get(eb, MAXS, &sps_fb);
    if (nfb <= 100 || sps_fb <= 0) {
        return -100.0;
    }
    int two_sps = 2 * sps_fb;
    int c1 = sps_fb / 2;
    int c2 = (3 * sps_fb) / 2;
    int win = sps_fb / 10;
    if (win < 1) {
        win = 1;
    }
    /* Quartiles over a downsampled set */
    int step_ds = (nfb > 4096) ? (nfb / 4096) : 1;
    int mct = (nfb + step_ds - 1) / step_ds;
    if (mct > 4096) {
        mct = 4096;
    }
    static float qv[4096];
    int vi = 0;
    for (int i = 0; i < nfb && vi < mct; i += step_ds) {
        qv[vi++] = eb[i]; /* normalized float samples [-1, 1] */
    }
    mct = vi;
    if (mct < 8) {
        return -100.0;
    }
    /* Quartiles via nth_element (O(n)) */
    int idx1 = (int)((size_t)mct / 4);
    int idx2 = (int)((size_t)mct / 2);
    int idx3 = (int)((size_t)(3 * (size_t)mct) / 4);
    std::nth_element(qv, qv + idx2, qv + mct);
    float q2 = qv[idx2];
    std::nth_element(qv, qv + idx1, qv + idx2);
    float q1 = qv[idx1];
    std::nth_element(qv + idx2 + 1, qv + idx3, qv + mct);
    float q3 = qv[idx3];
    long long cnt[4] = {0, 0, 0, 0};
    double sum[4] = {0, 0, 0, 0};
    for (int i = 0; i < nfb; i++) {
        int phase = i % two_sps;
        int inwin = (abs(phase - c1) <= win) || (abs(phase - c2) <= win);
        if (!inwin) {
            continue;
        }
        float v = eb[i];
        int b = (v <= q1) ? 0 : (v <= q2) ? 1 : (v <= q3) ? 2 : 3;
        cnt[b]++;
        sum[b] += (double)v;
    }
    long long total = cnt[0] + cnt[1] + cnt[2] + cnt[3];
    if (!(total > 50 && cnt[0] && cnt[1] && cnt[2] && cnt[3])) {
        return -100.0;
    }
    double mu[4];
    for (int b = 0; b < 4; b++) {
        mu[b] = sum[b] / (double)cnt[b];
    }
    double nsum = 0.0;
    for (int i = 0; i < nfb; i++) {
        int phase = i % two_sps;
        int inwin = (abs(phase - c1) <= win) || (abs(phase - c2) <= win);
        if (!inwin) {
            continue;
        }
        float v = eb[i];
        int b = (v <= q1) ? 0 : (v <= q2) ? 1 : (v <= q3) ? 2 : 3;
        double e = (double)v - mu[b];
        nsum += e * e;
    }
    double noise_var = nsum / (double)total;
    if (noise_var <= 1e-9) {
        return -100.0;
    }
    double mu_all = 0.0;
    for (int b = 0; b < 4; b++) {
        mu_all += mu[b] * (double)cnt[b] / (double)total;
    }
    double ssum = 0.0;
    for (int b = 0; b < 4; b++) {
        double d = mu[b] - mu_all;
        ssum += (double)cnt[b] * d * d;
    }
    double sig_var = ssum / (double)total;
    if (sig_var <= 1e-9) {
        return -100.0;
    }
    double bias = dsd_snr_bias_c4fm_db(demod.rate_out, demod.ted_sps, demod.channel_lpf_profile);
    return 10.0 * log10(sig_var / noise_var) - bias;
}

/* ---------------- Constellation-based SNR estimation (QPSK fallback) ---------------- */
extern "C" double
dsd_rtl_stream_estimate_snr_qpsk_const(void) {
    enum { MAXP = 4096 };

    static float xy[(size_t)MAXP * 2];
    int n = dsd_rtl_stream_constellation_get(xy, MAXP);
    if (n <= 64) {
        return -100.0;
    }
    /* Per-axis normalization to mitigate I/Q gain imbalance in raw snapshot */
    double sum_abs_i = 0.0, sum_abs_q = 0.0;
    for (int i = 0; i < n; i++) {
        double I = (double)xy[(size_t)(i << 1) + 0];
        double Q = (double)xy[(size_t)(i << 1) + 1];
        sum_abs_i += fabs(I);
        sum_abs_q += fabs(Q);
    }
    double aI = sum_abs_i / (double)n;
    double aQ = sum_abs_q / (double)n;
    if (!(aI > 1e-9 && aQ > 1e-9)) {
        return -100.0;
    }
    /* Evaluate both axis-aligned and 45°-diagonal targets; choose best. */
    double e2_axis = 0.0;
    for (int i = 0; i < n; i++) {
        double I = (double)xy[(size_t)(i << 1) + 0];
        double Q = (double)xy[(size_t)(i << 1) + 1];
        double ti = (I >= 0.0) ? aI : -aI;
        double tq = (Q >= 0.0) ? aQ : -aQ;
        double ei = I - ti;
        double eq = Q - tq;
        e2_axis += ei * ei + eq * eq;
    }
    double t2_axis = (double)n * (aI * aI + aQ * aQ);
    double aD = 0.5 * (aI + aQ);
    if (aD < 1e-9) {
        aD = 1e-9;
    }
    double e2_diag = 0.0;
    for (int i = 0; i < n; i++) {
        double I = (double)xy[(size_t)(i << 1) + 0];
        double Q = (double)xy[(size_t)(i << 1) + 1];
        double ti = (I >= 0.0) ? aD : -aD;
        double tq = (Q >= 0.0) ? aD : -aD;
        double ei = I - ti;
        double eq = Q - tq;
        e2_diag += ei * ei + eq * eq;
    }
    double t2_diag = (double)n * (2.0 * aD * aD);
    double best_snr = -100.0;
    if (t2_axis > 1e-9 && e2_axis > 0.0) {
        double ratio = (e2_axis <= 1e-12) ? 1e12 : (t2_axis / e2_axis);
        best_snr = 10.0 * log10(ratio);
    }
    if (t2_diag > 1e-9 && e2_diag > 0.0) {
        double ratio_d = (e2_diag <= 1e-12) ? 1e12 : (t2_diag / e2_diag);
        double snr_d = 10.0 * log10(ratio_d);
        if (snr_d > best_snr) {
            best_snr = snr_d;
        }
    }
    double bias = dsd_snr_bias_evm_db(demod.rate_out, demod.ted_sps, demod.channel_lpf_profile);
    return best_snr - bias;
}

/* ---------------- Eye-based SNR estimation (GFSK fallback, 2-level) ---------------- */
extern "C" double
dsd_rtl_stream_estimate_snr_gfsk_eye(void) {
    enum { MAXS = 4096 };

    static float eb[(size_t)MAXS];
    int sps_fb = 0;
    int nfb = dsd_rtl_stream_eye_get(eb, MAXS, &sps_fb);
    if (nfb <= 100 || sps_fb <= 0) {
        return -100.0;
    }
    int two_sps = 2 * sps_fb;
    int c1 = sps_fb / 2;
    int c2 = (3 * sps_fb) / 2;
    int win = sps_fb / 10;
    if (win < 1) {
        win = 1;
    }
    /* Downsample */
    int step_ds = (nfb > 4096) ? (nfb / 4096) : 1;
    int mct = (nfb + step_ds - 1) / step_ds;
    if (mct > 4096) {
        mct = 4096;
    }
    static float qv[4096];
    int vi = 0;
    for (int i = 0; i < nfb && vi < mct; i += step_ds) {
        qv[vi++] = eb[i];
    }
    mct = vi;
    if (mct < 8) {
        return -100.0;
    }
    /* Median via nth_element (O(n)) */
    int idx2 = (int)((size_t)mct / 2);
    std::nth_element(qv, qv + idx2, qv + mct);
    float q2 = qv[idx2]; /* median split */
    double sumL = 0.0, sumH = 0.0;
    int cntL = 0, cntH = 0;
    for (int i = 0; i < nfb; i++) {
        int phase = i % two_sps;
        int inwin = (abs(phase - c1) <= win) || (abs(phase - c2) <= win);
        if (!inwin) {
            continue;
        }
        float v = eb[i];
        if (v <= q2) {
            sumL += v;
            cntL++;
        } else {
            sumH += v;
            cntH++;
        }
    }
    if (cntL == 0 || cntH == 0) {
        return -100.0;
    }
    double muL = sumL / (double)cntL, muH = sumH / (double)cntH;
    int total = cntL + cntH;
    double nsum = 0.0;
    for (int i = 0; i < nfb; i++) {
        int phase = i % two_sps;
        int inwin = (abs(phase - c1) <= win) || (abs(phase - c2) <= win);
        if (!inwin) {
            continue;
        }
        float v = eb[i];
        double mu = (v <= q2) ? muL : muH;
        double e = (double)v - mu;
        nsum += e * e;
    }
    double noise_var = nsum / (double)total;
    if (noise_var <= 1e-9) {
        return -100.0;
    }
    double mu_all = (muL * (double)cntL + muH * (double)cntH) / (double)total;
    double ssum = (double)cntL * (muL - mu_all) * (muL - mu_all) + (double)cntH * (muH - mu_all) * (muH - mu_all);
    double sig_var = ssum / (double)total;
    if (sig_var <= 1e-9) {
        return -100.0;
    }
    double bias = dsd_snr_bias_evm_db(demod.rate_out, demod.ted_sps, demod.channel_lpf_profile);
    return 10.0 * log10(sig_var / noise_var) - bias;
}

/* Auto-PPM status (spectrum-based) is implemented in rtl_metrics.cpp. */
extern std::atomic<int> g_auto_ppm_enabled;
extern std::atomic<int> g_auto_ppm_user_en;
extern std::atomic<int> g_auto_ppm_locked;
extern std::atomic<int> g_auto_ppm_training;
extern std::atomic<int> g_auto_ppm_lock_ppm;
extern std::atomic<double> g_auto_ppm_lock_snr_db;
extern std::atomic<double> g_auto_ppm_lock_df_hz;
extern std::atomic<double> g_auto_ppm_snr_db;
extern std::atomic<double> g_auto_ppm_df_hz;
extern std::atomic<double> g_auto_ppm_est_ppm;
extern std::atomic<int> g_auto_ppm_last_dir;
extern std::atomic<int> g_auto_ppm_cooldown;

/* Spectrum, carrier diagnostics, tuner autogain, and auto-PPM metrics
 * exports are implemented in rtl_metrics.cpp. */

/**
 * @brief Initialize dongle (RTL-SDR source) state with default parameters.
 *
 * @param s Dongle state to initialize.
 */
void
dongle_init(struct dongle_state* s) {
    s->rate = rtl_dsp_bw_hz;
    s->gain = AUTO_GAIN; // tenths of a dB
    s->mute = 0;
    s->direct_sampling = 0;
    s->offset_tuning = 0; //E4000 tuners only
    s->demod_target = &demod;
}

/**
 * @brief Initialize output ring buffer and synchronization primitives.
 *
 * @param s Output state to initialize.
 */
void
output_init(struct output_state* s) {
    s->rate = rtl_dsp_bw_hz;
    dsd_cond_init(&s->ready);
    dsd_cond_init(&s->space);
    dsd_mutex_init(&s->ready_m);
    /* Allocate SPSC ring buffer */
    s->capacity = (size_t)(MAXIMUM_BUF_LENGTH * 8);
    /* Try aligned allocation for better vectorized copies; fall back if unavailable */
    {
        void* mem_ptr = dsd_neo_aligned_malloc(s->capacity * sizeof(float));
        if (!mem_ptr) {
            LOG_ERROR("Failed to allocate output ring buffer (%zu samples).\n", s->capacity);
            /* Propagate by keeping buffer NULL; callers must detect before use */
            return;
        }
        s->buffer = static_cast<float*>(mem_ptr);
    }
    s->head.store(0);
    s->tail.store(0);
    /* Metrics */
    s->write_timeouts.store(0);
    s->read_timeouts.store(0);
}

/**
 * @brief Destroy output ring buffer and synchronization primitives.
 *
 * @param s Output state to clean up.
 */
void
output_cleanup(struct output_state* s) {
    dsd_cond_destroy(&s->ready);
    dsd_cond_destroy(&s->space);
    dsd_mutex_destroy(&s->ready_m);
    if (s->buffer) {
        dsd_neo_aligned_free(s->buffer);
        s->buffer = NULL;
    }
}

/**
 * @brief Initialize controller state (frequency list and hop control).
 *
 * @param s Controller state to initialize.
 */
void
controller_init(struct controller_state* s) {
    s->freqs[0] = 446000000;
    s->freq_len = 0;
    s->edge = 0;
    s->wb_mode = 0;
    dsd_cond_init(&s->hop);
    dsd_mutex_init(&s->hop_m);
    s->manual_retune_pending.store(0);
    s->manual_retune_freq = 0;
    s->cold_start_ready.store(0); /* Demod will wait for controller to signal ready */
    s->retune_in_progress.store(0);
    /* Initialize retune completion synchronization */
    dsd_cond_init(&s->retune_done_cond);
    dsd_mutex_init(&s->retune_done_m);
    s->retune_done_flag.store(0);
    s->retune_request_id.store(0);
    s->retune_complete_id.store(0);
}

/**
 * @brief Destroy controller synchronization primitives.
 *
 * @param s Controller state to clean up.
 */
void
controller_cleanup(struct controller_state* s) {
    dsd_cond_destroy(&s->hop);
    dsd_mutex_destroy(&s->hop_m);
    dsd_cond_destroy(&s->retune_done_cond);
    dsd_mutex_destroy(&s->retune_done_m);
}

/**
 * @brief Handle termination signals by requesting RTL-SDR async cancel and exit.
 *
 * Logs the event and triggers a non-blocking stop of the async capture loop
 * so worker threads can wind down cleanly.
 */
extern "C" void
rtlsdr_sighandler(void) {
    LOG_ERROR("Signal caught, exiting!\n");
    /* Cooperative shutdown and wake any waiters */
    exitflag = 1;
    if (g_stream) {
        g_stream->should_exit.store(1);
    }
    safe_cond_signal(&input_ring.ready, &input_ring.ready_m);
    safe_cond_signal(&controller.hop, &controller.hop_m);
    safe_cond_signal(&demod.ready, &demod.ready_m);
    safe_cond_signal(&output.ready, &output.ready_m);
    rtl_device_stop_async(rtl_device_handle);
}

/**
 * @brief Seed initial device index, center frequency, gain and UDP port.
 *
 * @param opts Decoder options.
 */
static void
setup_initial_freq_and_rate(dsd_opts* opts) {
    if (opts->rtlsdr_center_freq > 0) {
        controller.freqs[controller.freq_len] = opts->rtlsdr_center_freq;
        controller.freq_len++;
    }
    if (opts->rtlsdr_ppm_error != 0) {
        dongle.ppm_error = opts->rtlsdr_ppm_error;
        LOG_INFO("Setting RTL PPM Error Set to %d\n", opts->rtlsdr_ppm_error);
    }
    dongle.dev_index = opts->rtl_dev_index;
    LOG_INFO("Setting DSP baseband to %d Hz\n", rtl_dsp_bw_hz);
    LOG_INFO("Setting RTL Power Squelch Level to %.1f dB\n", pwr_to_dB(opts->rtl_squelch_level));
    if (opts->rtl_udp_port != 0) {
        int p = opts->rtl_udp_port;
        if (p < 0) {
            p = 0;
        }
        if (p > 65535) {
            p = 65535;
        }
        port = (uint16_t)p;
    }
    if (opts->rtl_gain_value > 0) {
        dongle.gain = opts->rtl_gain_value * 10;
    }
}

/**
 * @brief Enqueue a manual retune on the controller thread and return its request ID.
 *
 * Coalesces callers when a retune is already pending (controller has not yet
 * consumed the request) so that completion IDs track the number of retunes
 * actually executed. This prevents synchronous waiters from timing out when
 * multiple retune requests arrive faster than the controller loop can service
 * them.
 *
 * @param target_freq_hz Desired center frequency in Hz.
 * @return Request ID that will be completed once the queued retune finishes.
 */
static uint32_t
schedule_manual_retune(uint32_t target_freq_hz) {
    dsd_mutex_lock(&controller.hop_m);
    uint32_t request_id = controller.retune_request_id.load(std::memory_order_acquire);
    int pending = controller.manual_retune_pending.load(std::memory_order_acquire);
    if (!pending) {
        request_id = controller.retune_request_id.fetch_add(1, std::memory_order_acq_rel) + 1;
        controller.manual_retune_pending.store(1, std::memory_order_release);
    }
    /* Update/override target frequency even when coalescing into an existing pending retune. */
    controller.manual_retune_freq = target_freq_hz;
    dsd_cond_signal(&controller.hop);
    dsd_mutex_unlock(&controller.hop_m);
    return request_id;
}

/**
 * @brief Launch controller/demod threads and start async device capture.
 *
 * Spawns the controller and demodulation workers, begins RTL-SDR async
 * streaming with the configured buffer size, and starts optional UDP
 * control for on-the-fly tuning.
 */
static void
start_threads_and_async(void) {
    dsd_thread_create(&controller.thread, (dsd_thread_fn)controller_thread_fn, (void*)(&controller));
    dsd_thread_create(&demod.thread, (dsd_thread_fn)demod_thread_fn, (void*)(&demod));
    LOG_INFO("Starting RTL async read...\n");
    rtl_device_start_async(rtl_device_handle, (uint32_t)ACTUAL_BUF_LENGTH);
    if (port != 0) {
        g_udp_ctrl = udp_control_start(
            port,
            [](uint32_t new_freq_hz, void* /*user_data*/) {
                /* Marshal onto controller thread: single programming path */
                schedule_manual_retune(new_freq_hz);
            },
            NULL);
    }
}

/* Forward decls for auto-PPM status helpers */
extern "C" int dsd_rtl_stream_auto_ppm_get_status(int* enabled, double* snr_db, double* df_hz, double* est_ppm,
                                                  int* last_dir, int* cooldown, int* locked);
extern "C" int dsd_rtl_stream_auto_ppm_training_active(void);
extern "C" void dsd_rtl_stream_set_auto_ppm(int onoff);
extern "C" int dsd_rtl_stream_get_auto_ppm(void);

/* Option B: Perform a short auto-PPM pre-training window at startup before returning control,
   so trunking/hunt logic begins after a stable PPM lock when possible. */

/**
 * @brief Initialize and open the RTL-SDR streaming pipeline, threads, and buffers.
 *
 * Configures device and demod state, validates options, allocates buffers,
 * programs initial capture settings (including fs/4 shift when appropriate),
 * and starts async capture plus worker threads.
 *
 * @param opts Decoder options used to configure the pipeline.
 * @return 0 on success, negative on error.
 */
extern "C" int
dsd_rtl_stream_open(dsd_opts* opts) {

    if (!opts) {
        LOG_ERROR("RTL stream open: missing opts\n");
        return -1;
    }

    struct {
        int use;
        int cqpsk_enable;
        int fll_enable;
        int ted_enable;
        float ted_gain;
        int ted_force;
    } persist = {};

    rtl_dsp_bw_hz = opts->rtl_dsp_bw_khz * 1000; // base DSP bandwidth in Hz
    /* DMR (GFSK) data decoding is sensitive to overly low DSP basebands; users often (reasonably) set bw≈12 kHz,
       but the resulting low-rate discriminator stream can degrade LRRP payload reliability even if resampled later. */
    if (opts->frame_dmr == 1 && opts->rtl_dsp_bw_khz > 0 && opts->rtl_dsp_bw_khz < 24) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            LOG_WARNING("RTL DSP-BW %dkHz is low for DMR; try 48kHz (or at least 24kHz) for more reliable data/LRRP.\n",
                        opts->rtl_dsp_bw_khz);
        }
    }
    /* Honor the user-requested DSP bandwidth directly for the demodulator base rate so
       half-band decimators/resampler scale with the CLI argument (4/6/8/12/16/24 kHz). */
    int demod_base_rate_hz = rtl_dsp_bw_hz;
    /* Apply CLI volume multiplier (1..3), default to 1 if out of range */
    {
        int vm = opts->rtl_volume_multiplier;
        if (vm < 1 || vm > 3) {
            vm = 1;
        }
        volume_multiplier = (short int)vm;
    }

    dongle_init(&dongle);
    rtl_demod_init_for_mode(&demod, &output, opts, demod_base_rate_hz);
    output_init(&output);
    if (!output.buffer) {
        LOG_ERROR("Output ring buffer allocation failed.\n");
        return -1;
    }
    /* Init input ring */
    {
        void* mem_ptr = dsd_neo_aligned_malloc((size_t)(MAXIMUM_BUF_LENGTH * 8) * sizeof(float));
        if (!mem_ptr) {
            LOG_ERROR("Failed to allocate input ring buffer.\n");
            return -1;
        }
        input_ring.buffer = static_cast<float*>(mem_ptr);
        input_ring.capacity = (size_t)(MAXIMUM_BUF_LENGTH * 8);
        input_ring.head.store(0);
        input_ring.tail.store(0);
        dsd_cond_init(&input_ring.ready);
        dsd_mutex_init(&input_ring.ready_m);
        /* Metrics */
        input_ring.producer_drops.store(0);
        input_ring.read_timeouts.store(0);
    }
    controller_init(&controller);

    /* Read optional environment flags (centralized) */
    rtl_demod_config_from_env_and_opts(&demod, opts);
    rtl_demod_select_defaults_for_mode(&demod, opts, &output);

    /* Reapply preserved DSP toggles when Manual Override is active. */
    if (persist.use) {
        demod.cqpsk_enable = persist.cqpsk_enable ? 1 : 0;
        demod.fll_enabled = persist.fll_enable ? 1 : 0;
        demod.ted_enabled = persist.ted_enable ? 1 : 0;
        if (persist.ted_gain > 0.0f) {
            demod.ted_gain = persist.ted_gain;
        }
        demod.ted_force = persist.ted_force ? 1 : 0;
    }

    /* Default: if user did not specify a manual tuner gain (0=AGC), enable
       supervisory tuner autogain unless explicitly disabled via env. This starts
        in device auto-gain and promotes to nearby manual values when needed. */
    if (opts && opts->rtl_gain_value <= 0) {
        // Supervisory auto gain DISABLED by default (user request).
        // Only enable if explicitly requested via env DSD_NEO_TUNER_AUTOGAIN=1.
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        if (cfg && cfg->tuner_autogain_enable) {
            g_tuner_autogain_on.store(1, std::memory_order_relaxed);
        }
    }

    setup_initial_freq_and_rate(opts);

    if (!output.rate) {
        output.rate = demod.rate_out;
    }

    {
        /* Validate inputs; require at least one frequency and squelch when scanning */
        if (controller.freq_len == 0) {
            LOG_ERROR("Please specify a frequency.\n");
            return -1;
        }
        if (controller.freq_len >= FREQUENCIES_LIMIT) {
            LOG_ERROR("Too many channels, maximum %i.\n", FREQUENCIES_LIMIT);
            return -1;
        }
        if (controller.freq_len > 1 && demod.channel_squelch_level == 0.0f) {
            LOG_ERROR("Please specify a squelch level.  Required for scanning multiple frequencies.\n");
            return -1;
        }
    }

    if (controller.freq_len > 1) {
        demod.terminate_on_squelch = 0;
    }

    ACTUAL_BUF_LENGTH = lcm_post[demod.post_downsample] * DEFAULT_BUF_LENGTH;
    /* Ensure async read uses a valid, explicit buffer length */
    dongle.buf_len = (uint32_t)ACTUAL_BUF_LENGTH;

    if (opts && opts->rtltcp_enabled) {
        int autotune = opts->rtltcp_autotune;
        if (!autotune) {
            const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
            if (cfg && cfg->tcp_autotune_enable) {
                autotune = 1;
            }
        }
        rtl_device_handle = rtl_device_create_tcp(opts->rtltcp_hostname, opts->rtltcp_portno, &input_ring,
                                                  combine_rotate_enabled, autotune);
        if (!rtl_device_handle) {
            LOG_ERROR("Failed to connect rtl_tcp at %s:%d.\n", opts->rtltcp_hostname, opts->rtltcp_portno);
            return -1;
        } else {
            LOG_INFO("Using rtl_tcp source %s:%d.\n", opts->rtltcp_hostname, opts->rtltcp_portno);
            rtl_device_print_offset_capability(rtl_device_handle);
        }
    } else {
        rtl_device_handle = rtl_device_create(dongle.dev_index, &input_ring, combine_rotate_enabled);
        if (!rtl_device_handle) {
            LOG_ERROR("Failed to open rtlsdr device %d.\n", dongle.dev_index);
            return -1;
        } else {
            LOG_INFO("Using RTLSDR Device Index: %d. \n", dongle.dev_index);
            /* Print tuner and expected offset-tuning capability before any attempts */
            rtl_device_print_offset_capability(rtl_device_handle);
        }
    }

    /* Apply bias tee setting before other tuner config (USB via librtlsdr; rtl_tcp via protocol cmd 0x0E) */
    if (opts && opts->rtl_bias_tee) {
        rtl_device_set_bias_tee(rtl_device_handle, 1);
    }

    /* Advanced RTL-SDR driver options via environment */
    {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();

        if (cfg && cfg->rtl_direct_is_set) {
            int mode = cfg->rtl_direct_mode;
            rtl_device_set_direct_sampling(rtl_device_handle, mode);
            dongle.direct_sampling = mode;
        }

        if (cfg && cfg->rtl_offset_tuning_is_set) {
            int on = cfg->rtl_offset_tuning_enable ? 1 : 0;
            rtl_device_set_offset_tuning_enabled(rtl_device_handle, on);
            dongle.offset_tuning = on ? 1 : 0;
        }

        if (cfg && (cfg->rtl_xtal_hz_is_set || cfg->tuner_xtal_hz_is_set)) {
            uint32_t rtl_xtal_hz = cfg->rtl_xtal_hz_is_set ? (uint32_t)cfg->rtl_xtal_hz : 0U;
            uint32_t tuner_xtal_hz = cfg->tuner_xtal_hz_is_set ? (uint32_t)cfg->tuner_xtal_hz : 0U;
            rtl_device_set_xtal_freq(rtl_device_handle, rtl_xtal_hz, tuner_xtal_hz);
        }

        if (cfg && cfg->rtl_testmode_is_set) {
            int on = cfg->rtl_testmode_enable ? 1 : 0;
            rtl_device_set_testmode(rtl_device_handle, on);
        }

        if (cfg && cfg->rtl_if_gains_is_set && cfg->rtl_if_gains[0] != '\0') {
            const char* ig = cfg->rtl_if_gains;
            char buf[1024];
            snprintf(buf, sizeof buf, "%s", ig);
            char* save = NULL;
            for (char* tok = dsd_strtok_r(buf, ",; ", &save); tok; tok = dsd_strtok_r(NULL, ",; ", &save)) {
                int stage = -1;
                double gain_db = 0.0;
                char* colon = strchr(tok, ':');
                if (!colon) {
                    continue;
                }
                *colon = '\0';
                const char* s_stage = tok;
                const char* s_gain = colon + 1;
                stage = atoi(s_stage);
                /* Strip 'dB' suffix if present */
                char gbuf[64];
                snprintf(gbuf, sizeof gbuf, "%s", s_gain);
                size_t gl = strlen(gbuf);
                if (gl >= 2 && (gbuf[gl - 1] == 'B' || gbuf[gl - 1] == 'b')) {
                    gbuf[gl - 1] = '\0';
                    if (gl >= 3 && (gbuf[gl - 2] == 'D' || gbuf[gl - 2] == 'd')) {
                        gbuf[gl - 2] = '\0';
                    }
                }
                gain_db = atof(gbuf);
                int gain_tenth = 0;
                if (strchr(gbuf, '.')) {
                    gain_tenth = (int)lrint(gain_db * 10.0);
                } else {
                    /* Assume already in 0.1 dB if large; else interpret as dB */
                    int gi = atoi(gbuf);
                    gain_tenth = (abs(gi) > 90) ? gi : (gi * 10);
                }
                if (stage >= 0) {
                    rtl_device_set_if_gain(rtl_device_handle, stage, gain_tenth);
                }
            }
        }
    }

    if (demod.deemph) {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        double tau_s = 75e-6; /* default 75 microseconds */
        if (cfg && cfg->deemph_is_set) {
            if (cfg->deemph_mode == DSD_NEO_DEEMPH_OFF) {
                demod.deemph = 0;
            } else if (cfg->deemph_mode == DSD_NEO_DEEMPH_50) {
                tau_s = 50e-6;
            } else if (cfg->deemph_mode == DSD_NEO_DEEMPH_NFM) {
                tau_s = 750e-6;
            } else if (cfg->deemph_mode == DSD_NEO_DEEMPH_75) {
                tau_s = 75e-6;
            }
        }
        if (demod.deemph) {
            double Fs = (double)demod.rate_out;
            if (Fs < 1.0) {
                Fs = 1.0;
            }
            double a = exp(-1.0 / (Fs * tau_s));
            double alpha = 1.0 - a;
            int coef_q15 = (int)lrint(alpha * (double)(1 << 15));
            if (coef_q15 < 1) {
                coef_q15 = 1;
            } else if (coef_q15 > ((1 << 15) - 1)) {
                coef_q15 = ((1 << 15) - 1);
            }
            demod.deemph_a = (float)((double)coef_q15 / (double)(1 << 15));
        }
    }

    /* Configure optional post-demod audio LPF via env DSD_NEO_AUDIO_LPF.
       Values:
       - off or 0: disabled (default)
       - NNNN: cutoff in Hz (approximate), e.g., 3000 or 5000.
       One-pole: y[n] = y[n-1] + alpha * (x[n] - y[n-1]),
       alpha ≈ 1 - exp(-2*pi*fc/Fs) in Q15. */
    {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        demod.audio_lpf_enable = 0;
        demod.audio_lpf_alpha = 0;
        demod.audio_lpf_state = 0;
        if (cfg && cfg->audio_lpf_is_set && !cfg->audio_lpf_disable && cfg->audio_lpf_cutoff_hz > 0) {
            int cutoff_hz = cfg->audio_lpf_cutoff_hz;
            if (cutoff_hz < 100) {
                cutoff_hz = 100; /* guard */
            }
            double Fs = (double)demod.rate_out;
            if (Fs < 1.0) {
                Fs = 1.0;
            }
            double a = 1.0 - exp(-2.0 * kPi * (double)cutoff_hz / Fs);
            if (a < 0.0) {
                a = 0.0;
            }
            if (a > 1.0) {
                a = 1.0;
            }
            demod.audio_lpf_alpha = (float)a;
            demod.audio_lpf_enable = 1;
            const char* approx = dsd_unicode_or_ascii("≈", "~");
            LOG_INFO("Audio LPF enabled: fc%s%d Hz, alpha=%.4f\n", approx, cutoff_hz, demod.audio_lpf_alpha);
        }
    }

    /* Set the tuner gain */
    rtl_device_set_gain(rtl_device_handle, dongle.gain);
    if (dongle.gain == AUTO_GAIN) {
        LOG_INFO("Setting RTL Autogain. \n");
    }

    rtl_device_set_ppm(rtl_device_handle, dongle.ppm_error);

    /* Prepare initial settings; controller thread will program the device. */
    if (controller.freq_len == 0) {
        controller.freqs[controller.freq_len] = 446000000;
        controller.freq_len++;
    }
    optimal_settings(controller.freqs[0], demod.rate_in);
    if (dongle.direct_sampling) {
        rtl_device_set_direct_sampling(rtl_device_handle, 1);
    }
    LOG_INFO("Oversampling input by: %ix.\n", (demod.downsample_passes > 0) ? (1 << demod.downsample_passes) : 1);
    LOG_INFO("Oversampling output by: %ix.\n", demod.post_downsample);
    LOG_INFO("Buffer size: %0.2fms\n", 1000 * 0.5 * (float)ACTUAL_BUF_LENGTH / (float)dongle.rate);
    LOG_INFO("Demod output at %u Hz.\n", (unsigned int)demod.rate_out);

    /* Recompute resampler with the actual demod output rate now known */
    if (demod.resamp_target_hz > 0) {
        int target = demod.resamp_target_hz;
        int inRate = demod.rate_out > 0 ? demod.rate_out : rtl_dsp_bw_hz;
        /* Bypass resampler when input already matches target */
        if (target == inRate) {
            demod.resamp_enabled = 0;
            demod.resamp_L = 1;
            demod.resamp_M = 1;
            LOG_INFO("Resampler bypassed: input rate %d Hz matches target.\n", inRate);
        } else {
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
                LOG_WARNING("Resampler ratio too large (L=%d,M=%d). Disabling resampler.\n", L, M);
                demod.resamp_enabled = 0;
            } else {
                demod.resamp_enabled = 1;
                resamp_design(&demod, L, M);
                LOG_INFO("Rational resampler configured: %d -> %d Hz (L=%d,M=%d).\n", inRate, target, L, M);
            }
        }
    } else {
        demod.resamp_enabled = 0;
    }

    /* With demod.rate_out known and resampler configured, refresh TED SPS unless overridden. */
    rtl_demod_maybe_refresh_ted_sps_after_rate_change(&demod, opts, &output);

    /* Reset endpoint before we start reading from it (mandatory) */
    rtl_device_reset_buffer(rtl_device_handle);

    /* Create or refresh stream internals before launching threads */
    if (g_stream) {
        free(g_stream);
        g_stream = NULL;
    }
    g_stream = (struct RtlSdrInternals*)calloc(1, sizeof(struct RtlSdrInternals));
    if (g_stream) {
        g_stream->device = rtl_device_handle;
        g_stream->dongle = &dongle;
        g_stream->demod = &demod;
        g_stream->output = &output;
        g_stream->controller = &controller;
        g_stream->input_ring = &input_ring;
        g_stream->udp_ctrl_ptr = &g_udp_ctrl;
        g_stream->opts = opts;
        g_stream->cfg = dsd_neo_get_config();
        g_stream->should_exit.store(0);
    }

    /* For rtl_tcp sources, optionally prebuffer before starting demod/controller
       to reduce initial under-runs and jitter on the consumer side. */
    if (opts && opts->rtltcp_enabled) {
        int pre_ms = 1000; /* default deeper prebuffer for rtltcp */
        {
            const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
            if (cfg) {
                pre_ms = cfg->tcp_prebuf_ms;
            }
        }

        /* Compute desired prebuffer in float samples (I+Q), and ensure the
           input ring is large enough so that half the ring equals the
           requested prebuffer. This provides headroom while starting demod. */
        size_t desired_prebuf = (size_t)((double)dongle.rate * 2.0 * ((double)pre_ms / 1000.0));
        if (desired_prebuf < 16384) {
            desired_prebuf = 16384;
        }
        size_t min_capacity = desired_prebuf * 2; /* use <= 50% for prebuffer */
        if (min_capacity > input_ring.capacity) {
            float* nb = (float*)dsd_neo_aligned_malloc(min_capacity * sizeof(float));
            if (nb) {
                if (input_ring.buffer) {
                    dsd_neo_aligned_free(input_ring.buffer);
                }
                input_ring.buffer = nb;
                input_ring.capacity = min_capacity;
                input_ring.head.store(0);
                input_ring.tail.store(0);
                LOG_INFO("rtltcp resized input ring to %zu samples (%.2f MiB) for ~%d ms prebuffer.\n",
                         input_ring.capacity, (double)input_ring.capacity * sizeof(float) / (1024.0 * 1024.0), pre_ms);
            } else {
                LOG_WARNING("rtltcp: allocation for %zu samples (%.2f MiB) failed; using existing ring (%zu).\n",
                            min_capacity, (double)min_capacity * sizeof(float) / (1024.0 * 1024.0),
                            input_ring.capacity);
            }
        }

        size_t target = desired_prebuf;
        /* Guard against unreasonable target */
        if (target < 16384) {
            target = 16384;
        }
        if (target > (input_ring.capacity / 2)) {
            target = input_ring.capacity / 2;
        }

        /* Announce computed prebuffer duration at current sample rate */
        double target_sec = (dongle.rate > 0) ? ((double)target / (2.0 * (double)dongle.rate)) : 0.0;
        LOG_INFO("rtltcp prebuffer target: %zu samples (%.3f s at %u Hz).\n", target, target_sec,
                 (unsigned)dongle.rate);

        /* Begin async capture first, then wait for ring to accumulate */
        LOG_INFO("Starting RTL async read (rtltcp prebuffer %d ms)...\n", pre_ms);
        rtl_device_start_async(rtl_device_handle, (uint32_t)ACTUAL_BUF_LENGTH);

        /* Wait up to ~2 seconds to reach target; exit early if flagged */
        {
            int waited_ms = 0;
            while (!exitflag && input_ring_used(&input_ring) < target && waited_ms < 2000) {
                dsd_sleep_ms(2); /* 2 ms */
                waited_ms += 2;
            }
            LOG_INFO("rtltcp prebuffer filled: %zu/%zu samples in ring.\n", input_ring_used(&input_ring), target);
        }

        /* Launch controller and demod threads after prebuffer */
        dsd_thread_create(&controller.thread, (dsd_thread_fn)controller_thread_fn, (void*)(&controller));
        dsd_thread_create(&demod.thread, (dsd_thread_fn)demod_thread_fn, (void*)(&demod));
        if (port != 0) {
            g_udp_ctrl = udp_control_start(
                port, [](uint32_t new_freq_hz, void* /*user_data*/) { schedule_manual_retune(new_freq_hz); }, NULL);
        }
    } else {
        /* Start controller/demod threads and async (USB path and defaults) */
        start_threads_and_async();
    }

    /* If resampler is enabled, update output.rate for downstream consumers */
    if (demod.resamp_enabled && demod.resamp_target_hz > 0) {
        output.rate = demod.resamp_target_hz;
        LOG_INFO("Output rate set to %d Hz via resampler.\n", output.rate);
    } else {
        output.rate = demod.rate_out;
    }

    /* One-time startup summary of the rate chain */
    {
        unsigned int capture_hz = dongle.rate;
        int base_decim = (demod.downsample_passes > 0) ? (1 << demod.downsample_passes) : 1;
        int post = (demod.post_downsample > 0) ? demod.post_downsample : 1;
        unsigned int demod_hz = (unsigned int)demod.rate_out;
        unsigned int out_hz =
            demod.resamp_enabled && demod.resamp_target_hz > 0 ? (unsigned int)demod.resamp_target_hz : demod_hz;
        if (demod.resamp_enabled) {
            LOG_INFO("Rate chain: capture=%u Hz, base_decim=%d, post=%d -> demod=%u Hz; resampler L/M=%d/%d -> "
                     "output=%u Hz.\n",
                     capture_hz, base_decim, post, demod_hz, demod.resamp_L, demod.resamp_M, out_hz);
        } else {
            LOG_INFO("Rate chain: capture=%u Hz, base_decim=%d, post=%d -> demod=%u Hz; resampler bypassed -> "
                     "output=%u Hz.\n",
                     capture_hz, base_decim, post, demod_hz, out_hz);
        }

        /* Derived SPS for common digital modes at current output rate */
        if (out_hz > 0) {
            int sps_p25p1 = (int)((out_hz + 2400) / 4800);  /* ~10 at 48k */
            int sps_p25p2 = (int)((out_hz + 3000) / 6000);  /* ~8 at 48k */
            int sps_nxdn48 = (int)((out_hz + 1200) / 2400); /* ~20 at 48k */
            const char* approx2 = dsd_unicode_or_ascii("≈", "~");
            LOG_INFO("Derived SPS (@%u Hz): P25P1%s%d, P25P2%s%d, NXDN48%s%d.\n", out_hz, approx2, sps_p25p1, approx2,
                     sps_p25p2, approx2, sps_nxdn48);
            /* Warn if far from canonical 48k-based SPS expectations */
            if ((sps_p25p1 < 8 || sps_p25p1 > 12) || (sps_p25p2 < 6 || sps_p25p2 > 10)
                || (sps_nxdn48 < 16 || sps_nxdn48 > 24)) {
                LOG_WARNING("Output rate %u Hz implies atypical SPS; digital decoders assume ~48k. Consider enabling "
                            "resampler to 48000 Hz.\n",
                            out_hz);
            }
        }
    }

    return 0;
}

/**
 * @brief Stop threads, free resources, and close the RTL-SDR stream.
 *
 * Signals workers to exit, joins threads, destroys device objects and rings,
 * releases LUTs and aligned buffers, and tears down UDP control if enabled.
 */
extern "C" void
dsd_rtl_stream_close(void) {
    LOG_INFO("cleaning up...\n");
    if (g_stream) {
        g_stream->should_exit.store(1);
    }
    LOG_INFO("Output ring: write_timeouts=%llu read_timeouts=%llu\n", (unsigned long long)output.write_timeouts.load(),
             (unsigned long long)output.read_timeouts.load());
    LOG_INFO("Input ring: producer_drops=%llu read_timeouts=%llu\n",
             (unsigned long long)input_ring.producer_drops.load(), (unsigned long long)input_ring.read_timeouts.load());
    if (g_udp_ctrl) {
        udp_control_stop(g_udp_ctrl);
        g_udp_ctrl = NULL;
    }
    /* Request threads to exit and wake any waiters */
    exitflag = 1;
    safe_cond_signal(&input_ring.ready, &input_ring.ready_m);
    safe_cond_signal(&controller.hop, &controller.hop_m);
    rtl_device_stop_async(rtl_device_handle);
    /* Wake any demod waits on both ready and space condition variables */
    safe_cond_signal(&demod.ready, &demod.ready_m);
    safe_cond_signal(&output.space, &output.ready_m);
    dsd_thread_join(demod.thread);
    /* Wake any consumers blocked on output.ready to finish */
    safe_cond_signal(&output.ready, &output.ready_m);
    dsd_thread_join(controller.thread);

    rtl_demod_cleanup(&demod);
    output_cleanup(&output);
    controller_cleanup(&controller);

    /* free input ring */
    if (input_ring.buffer) {
        dsd_neo_aligned_free(input_ring.buffer);
        input_ring.buffer = NULL;
    }

    rtl_device_destroy(rtl_device_handle);
    rtl_device_handle = NULL;

    if (g_stream) {
        free(g_stream);
        g_stream = NULL;
    }
}

/**
 * @brief Soft-stop the RTL stream without setting global exitflag.
 *
 * Requests threads to exit via should_exit, stops async I/O, joins threads,
 * and cleans up resources similarly to dsd_rtl_stream_close(), but does not
 * touch the global exitflag so the application continues running.
 */
extern "C" int
dsd_rtl_stream_soft_stop(void) {
    LOG_INFO("soft stopping...\n");
    if (g_stream) {
        g_stream->should_exit.store(1);
    }
    if (g_udp_ctrl) {
        udp_control_stop(g_udp_ctrl);
        g_udp_ctrl = NULL;
    }
    safe_cond_signal(&input_ring.ready, &input_ring.ready_m);
    safe_cond_signal(&controller.hop, &controller.hop_m);
    rtl_device_stop_async(rtl_device_handle);
    /* Wake any demod waits on both ready and space condition variables */
    safe_cond_signal(&demod.ready, &demod.ready_m);
    safe_cond_signal(&output.space, &output.ready_m);
    dsd_thread_join(demod.thread);
    /* Wake any consumers blocked on output.ready to finish */
    safe_cond_signal(&output.ready, &output.ready_m);
    dsd_thread_join(controller.thread);

    rtl_demod_cleanup(&demod);
    output_cleanup(&output);
    controller_cleanup(&controller);

    if (input_ring.buffer) {
        dsd_neo_aligned_free(input_ring.buffer);
        input_ring.buffer = NULL;
    }
    rtl_device_destroy(rtl_device_handle);
    rtl_device_handle = NULL;
    return 0;
}

/**
 * @brief Batched consumer API: read up to count samples with fewer wakeups/locks.
 * Applies volume scaling.
 *
 * @param out   Destination buffer for audio samples.
 * @param count Maximum number of samples to read.
 * @param opts  Decoder options (must not be NULL; used for runtime PPM changes).
 * @param state Decoder state (unused).
 * @return Number of samples read (>=1), 0 if count==0, or -1 on exit.
 */
extern "C" int
dsd_rtl_stream_read(float* out, size_t count, dsd_opts* opts, dsd_state* state) {
    UNUSED(state);
    if (count == 0) {
        return 0;
    }
    /* If stream is being torn down, abort read promptly. */
    if (!output.buffer || (g_stream && g_stream->should_exit.load())) {
        return -1;
    }
    if (!opts) {
        return -1;
    }

    /* Optional: spectrum-based auto PPM correction (opt-in via DSD_NEO_AUTO_PPM).
       Uses quadratic interpolation of the spectrum peak (industry practice)
        and a simple SNR gate. When SNR >= 6 dB, nudge PPM by +/-1 toward center. */
    do {
        static int init = 0;
        static int enabled = 0;
        /* minimum SNR to trigger (kept for env/opt parsing below) */
        static double snr_thr_db = 6.0;
        static int cooldown = 0; /* simple rate limiter (loops) */
        if (!init) {
            init = 1;
            const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
            enabled = (cfg && cfg->auto_ppm_enable) ? 1 : 0;
            if (cfg) {
                snr_thr_db = cfg->auto_ppm_snr_db;
            }
            if (opts && opts->rtl_auto_ppm_snr_db > 0.0f && opts->rtl_auto_ppm_snr_db <= 60.0f) {
                snr_thr_db = (double)opts->rtl_auto_ppm_snr_db;
            }
        }
        /* Allow CLI/opts to enable even if env unset */
        /* Runtime user override takes precedence; otherwise follow opts */
        {
            int u = g_auto_ppm_user_en.load(std::memory_order_relaxed);
            if (u == 0) {
                enabled = 0;
            } else if (u == 1) {
                enabled = 1;
            } else {
                if (opts) {
                    enabled = (opts->rtl_auto_ppm != 0) ? 1 : 0;
                }
            }
        }
        g_auto_ppm_enabled.store(enabled, std::memory_order_relaxed);
        if (!g_auto_ppm_locked.load(std::memory_order_relaxed) && enabled) {
            /* Initialize training window once on first entry */
            static int train_init = 0;
            static std::chrono::steady_clock::time_point t0;
            if (!train_init) {
                t0 = std::chrono::steady_clock::now();
                train_init = 1;
            }
        }
        if (!enabled) {
            g_auto_ppm_training.store(0, std::memory_order_relaxed);
            break;
        }
        if (g_auto_ppm_locked.load(std::memory_order_relaxed)) {
            g_auto_ppm_training.store(0, std::memory_order_relaxed);
            /* Locked: do not adjust further */
            break;
        }
        if (cooldown > 0) {
            cooldown--;
            g_auto_ppm_cooldown.store(cooldown, std::memory_order_relaxed);
            break;
        }
        /* Snapshot current spectrum via helper */
        static float spec_copy[1024];
        int rate = 0;
        int N = dsd_rtl_stream_spectrum_get(spec_copy, (int)(sizeof(spec_copy) / sizeof(spec_copy[0])), &rate);
        if (N <= 0 || rate <= 0) {
            g_auto_ppm_last_dir.store(0, std::memory_order_relaxed);
            break;
        }
        /* Find strongest bin near DC primarily: widen search to +/- N/4 of center
           to tolerate larger initial frequency errors. */
        int k_center_i = N >> 1;
        int W = N >> 2; /* N/4 */
        if (W < 8) {
            W = 8;
        }
        int i_lo = k_center_i - W;
        int i_hi = k_center_i + W;
        if (i_lo < 2) {
            i_lo = 2;
        }
        if (i_hi > N - 3) {
            i_hi = N - 3;
        }
        int i_max = i_lo;
        float p_max = spec_copy[i_lo];
        for (int i = i_lo + 1; i <= i_hi; i++) {
            if (spec_copy[i] > p_max) {
                p_max = spec_copy[i];
                i_max = i;
            }
        }
        /* Estimate noise floor via median of bins excluding +-2 around peak */
        static float tmp[1024];
        int m = 0;
        for (int i = i_lo; i < i_hi; i++) {
            if (i >= i_max - 2 && i <= i_max + 2) {
                continue;
            }
            tmp[m++] = spec_copy[i];
        }
        if (m < 16) {
            g_auto_ppm_last_dir.store(0, std::memory_order_relaxed);
            break;
        }
        /* Require recent direct demod SNR for gating; also compute spectral SNR */
        int mid = m / 2; /* still compute median to keep spectrum path consistent */
        std::nth_element(tmp, tmp + mid, tmp + m);
        float noise_med_db = tmp[mid];
        float spec_snr_db = p_max - noise_med_db;
        auto nowtp = std::chrono::steady_clock::now();
        long long nowms = std::chrono::duration_cast<std::chrono::milliseconds>(nowtp.time_since_epoch()).count();
        const long long fresh_ms = 800; /* direct SNR must be updated within 0.8 s */
        long long c4_ms = g_snr_c4fm_last_ms.load(std::memory_order_relaxed);
        int c4_src = g_snr_c4fm_src.load(std::memory_order_relaxed);
        long long qp_ms = g_snr_qpsk_last_ms.load(std::memory_order_relaxed);
        int qp_src = g_snr_qpsk_src.load(std::memory_order_relaxed);
        long long gf_ms = g_snr_gfsk_last_ms.load(std::memory_order_relaxed);
        int gf_src = g_snr_gfsk_src.load(std::memory_order_relaxed);
        double d_c4 = g_snr_c4fm_db.load(std::memory_order_relaxed);
        double d_qp = g_snr_qpsk_db.load(std::memory_order_relaxed);
        double d_gf = g_snr_gfsk_db.load(std::memory_order_relaxed);
        bool c4_ok = (c4_src == 1) && (nowms - c4_ms <= fresh_ms);
        bool qp_ok = (qp_src == 1) && (nowms - qp_ms <= fresh_ms);
        bool gf_ok = (gf_src == 1) && (nowms - gf_ms <= fresh_ms);
        bool have_demod = c4_ok || qp_ok || gf_ok; /* currently unused for gating */
        (void)have_demod;
        double gate_snr_db = -100.0;
        if (c4_ok) {
            gate_snr_db = d_c4;
        }
        if (qp_ok && d_qp > gate_snr_db) {
            gate_snr_db = d_qp;
        }
        if (gf_ok && d_gf > gate_snr_db) {
            gate_snr_db = d_gf;
        }
        /* Store spectral SNR for UI */
        g_auto_ppm_snr_db.store(spec_snr_db, std::memory_order_relaxed);
        /* Debounce absolute power (peak dB) to avoid brief spikes */
        static double pwr_thr_db = -80.0;
        static int pwr_thr_inited = 0;
        if (!pwr_thr_inited) {
            const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
            if (cfg) {
                pwr_thr_db = cfg->auto_ppm_pwr_db;
            }
            pwr_thr_inited = 1;
        }
        static int pwr_gate_active = 0;
        static auto pwr_gate_since = std::chrono::steady_clock::now();
        const int pwr_gate_debounce_ms = 2000; /* require 2s continuously above threshold */
        auto now_gate = std::chrono::steady_clock::now();
        bool pwr_over = (p_max >= (float)pwr_thr_db);
        if (pwr_over) {
            if (!pwr_gate_active) {
                pwr_gate_active = 1;
                pwr_gate_since = now_gate;
            }
        } else {
            pwr_gate_active = 0;
        }
        bool pwr_debounced =
            pwr_gate_active
            && (std::chrono::duration_cast<std::chrono::milliseconds>(now_gate - pwr_gate_since).count()
                >= pwr_gate_debounce_ms);
        /* Also require spectral SNR above threshold; reuse parsed env/opt value */
        double spec_thr_db = snr_thr_db;
        if (!pwr_debounced || spec_snr_db < spec_thr_db) {
            g_auto_ppm_training.store(0, std::memory_order_relaxed);
            g_auto_ppm_last_dir.store(0, std::memory_order_relaxed);
            break; /* below thresholds */
        }
        /* DC spur guard: if max is exactly the center bin and looks spur-like, ignore */
        if (i_max == k_center_i) {
            float l = spec_copy[i_max - 1];
            float r = spec_copy[i_max + 1];
            float side_max = (l > r) ? l : r;
            if ((p_max - side_max) > 12.0f) {
                /* sharp isolated spike at DC: likely residual DC spur */
                g_auto_ppm_last_dir.store(0, std::memory_order_relaxed);
                break;
            }
        }
        /* Parabolic interpolation around peak using log-power (dB) */
        int k = i_max;
        if (k <= 1 || k >= N - 2) {
            g_auto_ppm_last_dir.store(0, std::memory_order_relaxed);
            break;
        }
        double p1 = spec_copy[k - 1];
        double p2 = spec_copy[k + 0];
        double p3 = spec_copy[k + 1];
        double denom = (p1 - 2.0 * p2 + p3);
        double delta = 0.0; /* fractional bin offset in [-1,1] */
        if (fabs(denom) > 1e-6) {
            delta = 0.5 * (p1 - p3) / denom;
            if (delta > 1.0) {
                delta = 1.0;
            }
            if (delta < -1.0) {
                delta = -1.0;
            }
        }
        double k_hat = (double)k + delta;
        double k_center = 0.5 * (double)N; /* DC after shift */
        double k_err = k_hat - k_center;
        double bin_hz = (double)rate / (double)N;
        double df_hz = k_err * bin_hz; /* positive: signal to + side */
        g_auto_ppm_df_hz.store(df_hz, std::memory_order_relaxed);
        /* Convert to ppm relative to current tuned hardware center */
        double f0 = (double)dongle.freq;
        if (f0 <= 0.0) {
            g_auto_ppm_last_dir.store(0, std::memory_order_relaxed);
            break;
        }
        double est_ppm = (df_hz * 1e6) / f0;
        g_auto_ppm_est_ppm.store(est_ppm, std::memory_order_relaxed);
        /* Nudge by 1 ppm toward center with persistence, throttle, and direction self-calibration */
        static int dir_run = 0; /* -1,0,+1 (consensus of successive decisions) */
        static int run_len = 0; /* consecutive consistent decisions */
        static auto next_allowed = std::chrono::steady_clock::now();
        static int dir_calibrated = 0;          /* 0=unknown; +/-1 = mapping sign */
        static int awaiting_eval = 0;           /* 1 if last step pending evaluation */
        static int last_dir_applied = 0;        /* +/-1 for last applied change */
        static int last_ppm_after = 0;          /* ppm value after last applied change */
        static int last_step_size = 1;          /* ppm step size used for last change */
        static double prev_abs_df = 1e12;       /* |df| before last applied change */
        static double prev_demod_snr_db = -1e9; /* demod SNR at last step */
        /* Training + lock policy */
        static auto train_start = std::chrono::steady_clock::now();
        static int train_started = 0;
        static int train_steps = 0;
        const int train_max_ms = 15000;             /* train for up to 15 seconds */
        const int train_max_steps = 8;              /* or up to 8 steps */
        const int lock_hold_ms = 3000;              /* lock if stable for 3 seconds */
        static double lock_deadband_hz = 120.0;     /* and within 120 Hz window */
        static double zero_lock_deadband_hz = 60.0; /* tighter window for zero-step lock */
        /* Only allow zero-step lock when estimated offset is very small. This
           prevents premature lock at 0 when a meaningful offset remains. */
        static double zero_lock_max_est_ppm = 0.6; /* require |est_ppm| <= 0.6 */
        /* One-time config overrides for lock thresholds */
        static int zero_thr_inited = 0;
        if (!zero_thr_inited) {
            const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
            if (cfg) {
                zero_lock_max_est_ppm = cfg->auto_ppm_zerolock_ppm;
                zero_lock_deadband_hz = (double)cfg->auto_ppm_zerolock_hz;
            }
            zero_thr_inited = 1;
        }

        const int hold_needed = 4;             /* require 4 consistent decisions */
        const int throttle_ms = 1000;          /* at most 1 adjustment per 1s */
        const double deadband_ppm = 0.8;       /* ignore tiny offsets */
        const int ppm_limit = 200;             /* clamp absolute ppm range */
        const double eval_margin_hz = 120;     /* require ~>1/4-bin improvement to accept direction */
        const double eval_snr_margin_db = 0.5; /* prefer direction that improves demod SNR by >=0.5 dB */

        /* If evaluating last step, prefer demod SNR change; fallback to |df| change */
        if (awaiting_eval) {
            double cur_abs_df = fabs(df_hz);
            auto now = std::chrono::steady_clock::now();
            bool time_ok = (now >= next_allowed); /* wait full throttle interval for stable measurement */
            if (time_ok) {
                bool snr_improved = (gate_snr_db > (prev_demod_snr_db + eval_snr_margin_db));
                bool snr_worsened = (gate_snr_db + eval_snr_margin_db < prev_demod_snr_db);
                bool accept = snr_improved || (!snr_worsened && (cur_abs_df + eval_margin_hz < prev_abs_df));
                bool flip = snr_worsened || (cur_abs_df > prev_abs_df + eval_margin_hz);
                if (accept) {
                    dir_calibrated = last_dir_applied;
                    awaiting_eval = 0;
                } else if (flip) {
                    int target_ppm = last_ppm_after - 2 * last_dir_applied * last_step_size;
                    if (target_ppm > ppm_limit) {
                        target_ppm = ppm_limit;
                    }
                    if (target_ppm < -ppm_limit) {
                        target_ppm = -ppm_limit;
                    }
                    if (target_ppm != opts->rtlsdr_ppm_error) {
                        opts->rtlsdr_ppm_error = target_ppm;
                        if (snr_worsened) {
                            LOG_INFO("AUTO-PPM(spectrum): flip dir (SNR %.1f->%.1f dB). ppm->%d\n", prev_demod_snr_db,
                                     gate_snr_db, target_ppm);
                        } else {
                            LOG_INFO("AUTO-PPM(spectrum): flip dir (|df| %.1f->%.1f Hz). ppm->%d\n", prev_abs_df,
                                     cur_abs_df, target_ppm);
                        }
                    }
                    dir_calibrated = -last_dir_applied;
                    awaiting_eval = 0;
                    next_allowed = now + std::chrono::milliseconds(throttle_ms);
                    g_auto_ppm_cooldown.store(throttle_ms / 10, std::memory_order_relaxed);
                } else {
                    /* Inconclusive: extend wait window a bit */
                    next_allowed = now + std::chrono::milliseconds(250);
                }
            }
        }

        int dir_base = 0;
        if (est_ppm > deadband_ppm) {
            dir_base = +1;
        } else if (est_ppm < -deadband_ppm) {
            dir_base = -1;
        }
        /* Apply mapping sign when calibrated */
        int dir = (dir_calibrated == 0) ? dir_base : (dir_base * dir_calibrated);
        /* Fast catch-up: choose step size based on estimated absolute ppm */
        int step_size = 1;
        double abs_est_ppm = fabs(est_ppm);
        if (abs_est_ppm >= 50.0) {
            step_size = 8;
        } else if (abs_est_ppm >= 25.0) {
            step_size = 4;
        } else if (abs_est_ppm >= 12.0) {
            step_size = 2;
        }
        if (dir == 0) {
            dir_run = 0;
            run_len = 0;
        } else {
            if (dir == dir_run) {
                run_len++;
            } else {
                dir_run = dir;
                run_len = 1;
            }
        }

        g_auto_ppm_last_dir.store(dir, std::memory_order_relaxed);

        auto now = std::chrono::steady_clock::now();
        bool time_ok = (now >= next_allowed);
        int effective_hold = (step_size > 1) ? (hold_needed / 2) : hold_needed;
        if (effective_hold < 1) {
            effective_hold = 1;
        }
        if (!awaiting_eval && dir != 0 && run_len >= effective_hold && time_ok) {
            int new_ppm = opts->rtlsdr_ppm_error + dir * step_size; /* variable step size */
            if (new_ppm > ppm_limit) {
                new_ppm = ppm_limit;
            }
            if (new_ppm < -ppm_limit) {
                new_ppm = -ppm_limit;
            }
            if (new_ppm != opts->rtlsdr_ppm_error) {
                prev_abs_df = fabs(df_hz);
                prev_demod_snr_db = gate_snr_db;
                opts->rtlsdr_ppm_error = new_ppm;
                last_dir_applied = dir;
                last_ppm_after = new_ppm;
                last_step_size = step_size;
                awaiting_eval = 1; /* evaluate on next allowed window */
                next_allowed = now + std::chrono::milliseconds(throttle_ms);
                g_auto_ppm_cooldown.store(throttle_ms / 10, std::memory_order_relaxed);
                LOG_INFO("AUTO-PPM(spectrum): PWR=%.1f dB, SNR=%.1f dB, df=%.1f Hz, dir=%+d, step=%d, ppm->%d\n", p_max,
                         gate_snr_db, df_hz, dir, step_size, new_ppm);
                if (!train_started) {
                    train_started = 1;
                    train_start = now;
                }
                train_steps++;
            }
            run_len = 0; /* require persistence again */
        }

        /* Evaluate lock conditions: duration, step count, or stability window */
        auto now2 = std::chrono::steady_clock::now();
        if (!train_started) {
            /* Start the training window only after gate debounce is satisfied */
            if (pwr_debounced) {
                train_started = 1;
                train_start = now2;
            }
        } else {
            int elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now2 - train_start).count();
            bool time_over = (elapsed_ms >= train_max_ms);
            bool steps_over = (train_steps >= train_max_steps);
            static auto last_change = std::chrono::steady_clock::now();
            if (awaiting_eval == 0 && run_len == 0 && dir == 0) {
                /* if not changing, update stability timer */
                /* we keep last_change unless a step occurs (handled above) */
            } else if (awaiting_eval == 0 && last_dir_applied != 0) {
                last_change = now2;
            }
            int since_change_ms =
                (int)std::chrono::duration_cast<std::chrono::milliseconds>(now2 - last_change).count();
            bool stable_ok = (since_change_ms >= lock_hold_ms && fabs(df_hz) <= lock_deadband_hz);
            /* Allow lock without steps only when df is stably near zero and
               the estimated offset is tiny (avoid false zero locks). */
            bool significant_offset = (fabs(est_ppm) > deadband_ppm);
            bool stable_zero_ok = (train_steps == 0) && stable_ok && (fabs(df_hz) <= zero_lock_deadband_hz)
                                  && (!significant_offset) && (fabs(est_ppm) <= zero_lock_max_est_ppm);
            /* Lock if: lots of steps, or (>=1 step and time/stability satisfied), or (permissible zero-step lock) */
            bool can_lock = steps_over || ((train_steps >= 1) && (time_over || stable_ok)) || stable_zero_ok;
            if (can_lock) {
                g_auto_ppm_locked.store(1, std::memory_order_relaxed);
                g_auto_ppm_training.store(0, std::memory_order_relaxed);
                g_auto_ppm_lock_ppm.store(opts->rtlsdr_ppm_error, std::memory_order_relaxed);
                /* Store peak power at lock for UI */
                g_auto_ppm_lock_snr_db.store(p_max, std::memory_order_relaxed);
                g_auto_ppm_lock_df_hz.store(df_hz, std::memory_order_relaxed);
                LOG_INFO("AUTO-PPM(spectrum): training complete, locked (elapsed=%d ms, steps=%d, |df|=%.1f Hz)\n",
                         elapsed_ms, train_steps, fabs(df_hz));
            }
        }
        if (!g_auto_ppm_locked.load(std::memory_order_relaxed)) {
            g_auto_ppm_training.store(1, std::memory_order_relaxed);
        }
    } while (0);

    /* If PPM Error is Manually Changed, change it here once per batch */
    if (opts->rtlsdr_ppm_error != dongle.ppm_error) {
        dongle.ppm_error = opts->rtlsdr_ppm_error;
        rtl_device_set_ppm(rtl_device_handle, dongle.ppm_error);
    }

    int got = ring_read_batch(&output, out, count);
    if (got <= 0) {
        return -1;
    }
    /* No internal scaling here; callers may apply their own multiplier. */
    return got;
}

/**
 * @brief Return the current output audio sample rate in Hz.
 *
 * @return Output sample rate in Hz.
 */
extern "C" unsigned int
dsd_rtl_stream_output_rate(void) {
    return (unsigned int)output.rate;
}

/* Helper for generic rings to observe RTL stream shutdown without using exitflag */
extern "C" int
dsd_rtl_stream_should_exit(void) {
    return (g_stream && g_stream->should_exit.load()) ? 1 : 0;
}

/**
 * @brief Return smoothed TED residual (EMA of Gardner error). Sign indicates
 * persistent early/late bias; 0 when TED disabled or no bias.
 */
extern "C" int
dsd_rtl_stream_ted_bias(void) {
    return demod.ted_state.e_ema;
}

extern "C" int
dsd_rtl_stream_get_ted_sps(void) {
    return demod.ted_sps;
}

extern "C" void
dsd_rtl_stream_set_ted_sps(int sps) {
    if (sps < 2) {
        sps = 2;
    }
    if (sps > 64) {
        sps = 64;
    }
    /* Only set the override here, NOT ted_sps itself.
     *
     * This fixes a race condition where trunk_tune_to_freq() sets ted_sps
     * before the hardware retune completes, causing the DSP thread to
     * process stale samples (from the old frequency) with the new SPS.
     *
     * By only setting the override, the DSP continues with the current
     * (correct for current signal) SPS until the controller thread applies
     * the hardware retune and calls rtl_demod_maybe_refresh_ted_sps_after_rate_change,
     * which will see the override and apply it at the right time.
     *
     * This keeps timing/carrier configuration changes aligned with the
     * actual frequency change, not applied prematurely.
     *
     * We also set costas_reset_pending to signal that the Costas loop should
     * be reset on the next retune. Some demodulators reset Costas when the
     * symbol rate changes; in dsd-neo the ted_sps may already be updated by
     * other code paths before retune runs.
     */
    /* Debug: log set_ted_sps call */
    {
        if (debug_cqpsk_enabled()) {
            fprintf(stderr, "[SET_TED_SPS] sps=%d current_ted_sps=%d will_set_pending=%d\n", sps, demod.ted_sps,
                    (sps != demod.ted_sps) ? 1 : 0);
        }
    }
    if (sps != demod.ted_sps) {
        demod.costas_reset_pending = 1;
    }
    demod.ted_sps_override = sps;
    if (demod.cqpsk_enable) {
        demod.channel_lpf_profile = DSD_CH_LPF_PROFILE_P25_CQPSK;
    }
}

extern "C" void
dsd_rtl_stream_clear_ted_sps_override(void) {
    demod.ted_sps_override = 0;
}

extern "C" void
dsd_rtl_stream_set_ted_sps_no_override(int sps) {
    if (sps < 2) {
        sps = 2;
    }
    if (sps > 64) {
        sps = 64;
    }
    /* Debug: log set_ted_sps_no_override call */
    {
        if (debug_cqpsk_enabled()) {
            fprintf(stderr, "[SET_TED_SPS_NO_OVERRIDE] sps=%d current_ted_sps=%d will_reset=%d\n", sps, demod.ted_sps,
                    (sps != demod.ted_sps) ? 1 : 0);
        }
    }
    /* Reset Costas loop IMMEDIATELY when SPS changes, not via pending flag.
     *
     * This function is called AFTER rtl_stream_tune() completes (e.g., in trunk_tune_to_cc),
     * so demod_reset_on_retune() has already executed and won't consume a pending flag.
     * We must reset the Costas loop here directly to avoid running with a ~20-25% frequency
     * error (the Costas freq in rad/symbol represents different Hz at different symbol rates).
     *
     */
    if (sps != demod.ted_sps) {
        demod.costas_state.freq = 0.0f;
        demod.costas_state.phase = 0.0f;
        demod.costas_state.error = 0.0f;
    }
    demod.ted_sps = sps;
    /* Does NOT set ted_sps_override, allowing rate-change refresh to
       recalculate SPS later. Use when returning to CC or switching protocols. */

    if (demod.cqpsk_enable) {
        demod.channel_lpf_profile = DSD_CH_LPF_PROFILE_P25_CQPSK;
    }
}

extern "C" void
dsd_rtl_stream_set_ted_gain(float g) {
    if (g < 0.01f) {
        g = 0.01f;
    }
    if (g > 0.5f) {
        g = 0.5f;
    }
    demod.ted_gain = g;
}

extern "C" float
dsd_rtl_stream_get_ted_gain(void) {
    return demod.ted_gain;
}

extern "C" void
dsd_rtl_stream_set_ted_force(int onoff) {
    demod.ted_force = onoff ? 1 : 0;
}

extern "C" int
dsd_rtl_stream_get_ted_force(void) {
    return demod.ted_force ? 1 : 0;
}

/* -------- FM/C4FM amplitude stabilization + DC blocker (runtime) -------- */
extern "C" int
dsd_rtl_stream_get_fm_agc(void) {
    return demod.fm_agc_enable ? 1 : 0;
}

extern "C" void
dsd_rtl_stream_set_fm_agc(int onoff) {
    demod.fm_agc_enable = onoff ? 1 : 0;
}

extern "C" void
dsd_rtl_stream_get_fm_agc_params(float* target_rms, float* min_rms, float* alpha_up, float* alpha_down) {
    if (target_rms) {
        *target_rms = demod.fm_agc_target_rms;
    }
    if (min_rms) {
        *min_rms = demod.fm_agc_min_rms;
    }
    if (alpha_up) {
        *alpha_up = demod.fm_agc_alpha_up;
    }
    if (alpha_down) {
        *alpha_down = demod.fm_agc_alpha_down;
    }
}

extern "C" void
dsd_rtl_stream_set_fm_agc_params(float target_rms, float min_rms, float alpha_up, float alpha_down) {
    if (target_rms >= 0.0f) {
        if (target_rms < 0.05f) {
            target_rms = 0.05f;
        }
        if (target_rms > 2.5f) {
            target_rms = 2.5f;
        }
        demod.fm_agc_target_rms = target_rms;
    }
    if (min_rms >= 0.0f) {
        if (min_rms > 1.0f) {
            min_rms = 1.0f;
        }
        demod.fm_agc_min_rms = min_rms;
    }
    if (alpha_up >= 0.0f) {
        if (alpha_up > 1.0f) {
            alpha_up = 1.0f;
        }
        demod.fm_agc_alpha_up = alpha_up;
    }
    if (alpha_down >= 0.0f) {
        if (alpha_down > 1.0f) {
            alpha_down = 1.0f;
        }
        demod.fm_agc_alpha_down = alpha_down;
    }
}

extern "C" int
dsd_rtl_stream_get_fm_limiter(void) {
    return demod.fm_limiter_enable ? 1 : 0;
}

extern "C" void
dsd_rtl_stream_set_fm_limiter(int onoff) {
    demod.fm_limiter_enable = onoff ? 1 : 0;
}

extern "C" int
dsd_rtl_stream_get_iq_dc(int* out_shift_k) {
    if (out_shift_k) {
        *out_shift_k = demod.iq_dc_shift;
    }
    return demod.iq_dc_block_enable ? 1 : 0;
}

extern "C" void
dsd_rtl_stream_set_iq_dc(int enable, int shift_k) {
    int was = demod.iq_dc_block_enable ? 1 : 0;
    if (enable >= 0) {
        demod.iq_dc_block_enable = enable ? 1 : 0;
    }
    if (shift_k >= 0) {
        if (shift_k < 6) {
            shift_k = 6;
        }
        if (shift_k > 15) {
            shift_k = 15;
        }
        demod.iq_dc_shift = shift_k;
    }
    /* If enabling now, precharge DC estimate to current block mean and retarget AGC
       so there is no apparent level drop. */
    if (!was && demod.iq_dc_block_enable && demod.lowpassed && demod.lp_len >= 2) {
        const int pairs = demod.lp_len >> 1;
        double sumI = 0.0;
        double sumQ = 0.0;
        for (int n = 0; n < pairs; n++) {
            sumI += (double)demod.lowpassed[(size_t)(n << 1) + 0];
            sumQ += (double)demod.lowpassed[(size_t)(n << 1) + 1];
        }
        float meanI = (pairs > 0) ? (float)(sumI / (double)pairs) : 0.0f;
        float meanQ = (pairs > 0) ? (float)(sumQ / (double)pairs) : 0.0f;
        demod.iq_dc_avg_r = meanI;
        demod.iq_dc_avg_i = meanQ;
        /* Estimate RMS after subtraction and retarget AGC gain */
        double acc = 0.0;
        for (int n = 0; n < pairs; n++) {
            double I = (double)demod.lowpassed[(size_t)(n << 1) + 0] - (double)meanI;
            double Q = (double)demod.lowpassed[(size_t)(n << 1) + 1] - (double)meanQ;
            acc += I * I + Q * Q;
        }
        if (pairs > 0) {
            double mean_r2 = acc / (double)pairs;
            double rms = sqrt(mean_r2);
            float target = (demod.fm_agc_target_rms > 0.0f) ? demod.fm_agc_target_rms : 0.30f;
            if (target < 0.05f) {
                target = 0.05f;
            }
            if (target > 2.5f) {
                target = 2.5f;
            }
            double g_raw = (rms > 1e-6) ? ((double)target / rms) : 1.0;
            if (g_raw > 8.0) {
                g_raw = 8.0;
            }
            if (g_raw < 0.125) {
                g_raw = 0.125;
            }
            demod.fm_agc_gain = (float)g_raw;
            demod.fm_agc_ema_rms = rms;
        }
    }
}

/**
 * @brief Set or disable the resampler target rate and reapply capture settings.
 *
 * Marshals onto the controller thread by scheduling a no-op retune to the
 * current frequency, which safely reconfigures the resampler and updates the
 * output rate with proper buffer draining.
 *
 * @param target_hz Target output rate in Hz. Pass 0 to disable resampler.
 */
extern "C" void
dsd_rtl_stream_set_resampler_target(int target_hz) {
    if (target_hz <= 0) {
        demod.resamp_target_hz = 0;
    } else {
        demod.resamp_target_hz = target_hz;
    }
    /* Schedule retune to current center to apply changes on controller thread */
    schedule_manual_retune(dongle.freq);
}

/* Runtime DSP tuning entrypoints (C shim) */

/**
 * @brief P25 Phase 2 error callbacks for runtime helpers.
 * Aggregates recent RS/voice error deltas.
 */
extern "C" void
dsd_rtl_stream_p25p2_err_update(int slot, int facch_ok_delta, int facch_err_delta, int sacch_ok_delta,
                                int sacch_err_delta, int voice_err_delta) {
    (void)slot;
    (void)facch_ok_delta;
    (void)facch_err_delta;
    (void)sacch_ok_delta;
    (void)sacch_err_delta;
    (void)voice_err_delta;
}

extern "C" void
rtl_stream_p25p1_ber_update(int fec_ok_delta, int fec_err_delta) {
    (void)fec_ok_delta;
    (void)fec_err_delta;
}

/* Toggle generic IQ balance prefilter */
extern "C" void
dsd_rtl_stream_toggle_iq_balance(int onoff) {
    demod.iqbal_enable = onoff ? 1 : 0;
}

extern "C" int
dsd_rtl_stream_get_iq_balance(void) {
    return demod.iqbal_enable ? 1 : 0;
}

/* Coarse DSP feature toggles and snapshot */
extern "C" void
rtl_stream_toggle_cqpsk(int onoff) {
    demod.cqpsk_enable = onoff ? 1 : 0;
    if (demod.cqpsk_enable) {
        /* CQPSK Costas/differential stage assumes symbol-rate samples from
           the Gardner TED. Require TED whenever CQPSK is active so the
           pipeline never feeds oversampled I/Q into op25_diff_phasor_cc/op25_costas_loop_cc. */
        demod.ted_enabled = 1;
    }
    /* Switch demod output selector and reset CQPSK differential history. */
    if (demod.cqpsk_enable) {
        extern void qpsk_differential_demod(struct demod_state*);
        demod.mode_demod = &qpsk_differential_demod;
        demod.cqpsk_diff_prev_r = 1.0f;
        demod.cqpsk_diff_prev_j = 0.0f;
    } else {
        extern void dsd_fm_demod(struct demod_state*);
        demod.mode_demod = &dsd_fm_demod;
    }
}

extern "C" void
rtl_stream_toggle_fll(int onoff) {
    demod.fll_enabled = onoff ? 1 : 0;
    if (!demod.fll_enabled) {
        /* Reset FLL state to baseline to avoid carryover */
        fll_init_state(&demod.fll_state);
        demod.fll_freq = 0.0f;
        demod.fll_phase = 0.0f;
        demod.fll_prev_r = 0.0f;
        demod.fll_prev_j = 0.0f;
    }
}

extern "C" void
rtl_stream_toggle_ted(int onoff) {
    if (!onoff && demod.cqpsk_enable) {
        /* Prevent disabling TED while CQPSK path is active: the CQPSK
           Costas/differential stage requires symbol-rate samples from
           the Gardner TED. Ignore the request when CQPSK is enabled. */
        return;
    }

    demod.ted_enabled = onoff ? 1 : 0;
    if (!demod.ted_enabled) {
        /* Reset TED state */
        ted_init_state(&demod.ted_state);
        demod.ted_mu = 0.0f;
    }
}

extern "C" int
rtl_stream_dsp_get(int* cqpsk_enable, int* fll_enable, int* ted_enable) {
    if (cqpsk_enable) {
        *cqpsk_enable = demod.cqpsk_enable ? 1 : 0;
    }
    if (fll_enable) {
        *fll_enable = demod.fll_enabled ? 1 : 0;
    }
    if (ted_enable) {
        *ted_enable = demod.ted_enabled ? 1 : 0;
    }
    return 0;
}

/**
 * @brief Tune RTL-SDR to a new center frequency, updating optimal settings.
 *
 * This function is SYNCHRONOUS: it blocks until the controller thread has
 * completed the hardware retune and all DSP state resets. This ensures that
 * subsequent SPS/Costas configuration calls operate on properly reset state.
 *
 * @param opts      Decoder options.
 * @param frequency Target center frequency in Hz.
 * @return 0 on success, -1 on timeout.
 */
extern "C" int
dsd_rtl_stream_tune(dsd_opts* opts, long int frequency) {
    /* Freeze retunes during auto-PPM training (to allow lock) unless disabled */
    static int freeze_checked = 0;
    static int freeze_on_train = 1; /* default on */
    if (!freeze_checked) {
        freeze_checked = 1;
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        if (cfg) {
            freeze_on_train = cfg->auto_ppm_freeze_enable ? 1 : 0;
        }
    }
    extern int dsd_rtl_stream_auto_ppm_training_active(void);
    if (freeze_on_train && dsd_rtl_stream_auto_ppm_training_active()) {
        LOG_NOTICE("Retune deferred: auto-PPM training active.\n");
        return 0; /* no retune while training */
    }
    if (opts->payload == 1) {
        LOG_INFO("\nTuning to %ld Hz.", frequency);
    }
    dongle.freq = opts->rtlsdr_center_freq = frequency;

    /* Enqueue retune, coalescing with any already-pending request so completion IDs
     * stay aligned with the number of retunes the controller will actually execute. */
    uint32_t my_request_id = schedule_manual_retune((uint32_t)dongle.freq);

    if (opts->payload == 1) {
        LOG_INFO(" (Center Frequency: %u Hz.) \n", dongle.freq);
    }

    /* Wait for controller to complete the retune with a timeout.
     *
     * The timeout (500ms) is generous - typical RTL-SDR retunes complete in
     * 10-50ms. The timeout protects against controller thread deadlock or
     * missed wakeups, allowing the system to continue (with degraded performance)
     * rather than hanging indefinitely.
     *
     * We use a request ID to handle spurious wakeups and ensure we're waiting
     * for OUR retune to complete, not a previous one. */
    int rc = 0;
    dsd_mutex_lock(&controller.retune_done_m);
    while (controller.retune_complete_id.load(std::memory_order_acquire) < my_request_id) {
        int wait_rc = dsd_cond_timedwait(&controller.retune_done_cond, &controller.retune_done_m, 500);
        if (wait_rc != 0) {
            /* Timeout - log warning but continue to avoid deadlock.
             * The retune may still complete; caller should be prepared for
             * slightly degraded initial lock performance. */
            LOG_NOTICE("Retune wait timeout (request %u, complete %u) - continuing.\n", my_request_id,
                       controller.retune_complete_id.load(std::memory_order_relaxed));
            rc = -1;
            break;
        }
        /* Check for shutdown to avoid waiting forever during exit */
        if (exitflag || (g_stream && g_stream->should_exit.load(std::memory_order_relaxed))) {
            rc = -1;
            break;
        }
    }
    dsd_mutex_unlock(&controller.retune_done_m);

    /* Honor drain/clear policy for API-triggered tunes as well */
    drain_output_on_retune();
    return rc;
}

/**
 * @brief Return mean power approximation (RMS^2 proxy) for soft squelch decisions.
 * Returns the post-channel-filter power computed in full_demod().
 *
 * @return Mean power value (approximate RMS squared) after channel filtering.
 */
extern "C" double
dsd_rtl_stream_return_pwr(void) {
    return (double)demod.channel_pwr;
}

/**
 * @brief Set the channel squelch level in the demod state.
 */
extern "C" void
dsd_rtl_stream_set_channel_squelch(float level) {
    demod.channel_squelch_level = level;
}

/**
 * @brief Clear the output ring buffer and wake any waiting producer.
 */
extern "C" void
dsd_rtl_stream_clear_output(void) {
    struct output_state* outp = &output;
    if (g_stream && g_stream->output) {
        outp = g_stream->output;
    }
    /* Clear the entire ring to prevent sample 'lag' */
    ring_clear(outp);
    /* Wake producer waiting for space */
    safe_cond_signal(&outp->space, &outp->ready_m);
}

extern "C" int
dsd_rtl_stream_set_rtltcp_autotune(int onoff) {
    if (!rtl_device_handle) {
        return -1;
    }
    return rtl_device_set_tcp_autotune(rtl_device_handle, onoff ? 1 : 0);
}

extern "C" int
dsd_rtl_stream_get_rtltcp_autotune(void) {
    if (!rtl_device_handle) {
        return 0;
    }
    return rtl_device_get_tcp_autotune(rtl_device_handle);
}
