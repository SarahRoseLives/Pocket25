// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime configuration API and environment documentation.
 *
 * Exposes typed configuration parsed from environment variables and accessors
 * to initialize and retrieve the immutable configuration.
 */

#ifndef DSD_NEO_RUNTIME_CONFIG_H
#define DSD_NEO_RUNTIME_CONFIG_H

/* Include schema types first (before extern "C" for C++ compat) */
#include <dsd-neo/runtime/config_schema.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ensure dsd_opts type is visible to prototypes below */
#include <dsd-neo/core/opts_fwd.h>

/* Forward-declare decoder state for user-config helpers */
#include <dsd-neo/core/state_fwd.h>
#include <stdio.h>

/*
 * Runtime configuration (environment variables)
 *
 * Precedence: CLI/opts > environment > built-in defaults. This module parses
 * environment variables once (during open) and exposes a typed config.
 *
 * Realtime scheduling and CPU affinity
 * - DSD_NEO_RT_SCHED
 *     Enable best-effort realtime scheduling (SCHED_FIFO). Requires CAP_SYS_NICE or root.
 *     Values: "1" to enable, unset/other to disable. Default: disabled.
 * - DSD_NEO_RT_PRIO_USB | DSD_NEO_RT_PRIO_DONGLE | DSD_NEO_RT_PRIO_DEMOD
 *     Optional per-thread priorities (1..99, clamped to system limits). Used only if RT_SCHED=1.
 *     Example: export DSD_NEO_RT_PRIO_DEMOD=85
 * - DSD_NEO_CPU_USB | DSD_NEO_CPU_DONGLE | DSD_NEO_CPU_DEMOD
 *     Optional CPU core pinning for each thread. Integer CPU id (>=0). Example: export DSD_NEO_CPU_DEMOD=2
 *
 * Frontend/decimation/upsampling
 * - DSD_NEO_COMBINE_ROT
 *     Combine 90° IQ rotation with USB byte→float widening in one pass when offset tuning is off.
 *     Values: 1 enable, 0 disable. Default: 1 (enabled).
 * - DSD_NEO_UPSAMPLE_FP
 *     Use fixed-point arithmetic in legacy linear upsampler for lower CPU/divisions.
 *     Values: 1 enable, 0 disable. Default: 1 (enabled).
 *
 * Rational resampler (polyphase upfirdn L/M)
 * - DSD_NEO_RESAMP
 *     Target output sample rate in Hz. Enables L/M resampler when set.
 *     Values: "off" or "0" to disable; integer Hz (e.g., 48000) to enable. Default: 48000 (enabled).
 *
 * Residual CFO frequency-locked loop (FLL)
 * - DSD_NEO_FLL
 *     Enable residual carrier frequency correction.
 *     Values: "1" to enable; "0"/unset/other to disable. Default: disabled.
 * - DSD_NEO_FLL_ALPHA, DSD_NEO_FLL_BETA
 *     Proportional and integral gains (Q15 fixed-point, ~value/32768). Typical small values.
 *     Defaults: ALPHA=100 (~0.003), BETA=10 (~0.0003). May be adjusted for digital modes if not set.
 * - DSD_NEO_FLL_DEADBAND
 *     Ignore small phase errors in the FLL loop to avoid audible low-frequency sweeps in analog FM.
 *     Values: Q14 integer threshold (pi == 1<<14). Example: 60 (~0.36 degrees). Default: 45.
 * - DSD_NEO_FLL_SLEW
 *     Limit per-update NCO frequency change (slew-rate) to prevent rapid ramps.
 *     Values: Q15 integer (2*pi == 1<<15). Example: 32..128. Default: 64.
 *
 * Gardner timing error detector (TED)
 * - DSD_NEO_TED
 *     Enable lightweight fractional-delay timing correction. Generally off for analog FM.
 *     Values: 1 enable, else disabled. Default: 0 (disabled). For certain digital modes, defaults are adjusted
 *     only if envs are not provided (still off unless forced via DSD_NEO_TED=1).
 * - DSD_NEO_TED_SPS
 *     Nominal samples-per-symbol (integer). If unset and a digital mode is active, it is derived from output rate.
 *     Default: 10.
 * - DSD_NEO_TED_GAIN
 *     Small loop gain (Q20). Default: 64; for common digital modes may default to 96 when not provided.
 * - DSD_NEO_TED_FORCE
 *     Force TED to run for FM/C4FM paths where it is normally skipped. Values: 1 enable, else disabled. Default: 0.
 *
 * C4FM clock assist (symbol-domain)
 * - DSD_NEO_C4FM_CLK
 *     Enable a lightweight clock loop on the C4FM (P25p1) symbol path.
 *     Values: "el" for Early-Late, "mm" for Mueller&Mueller, "0"/"off" to disable.
 *     Default: off. When enabled, the loop nudges the integer symbolCenter by ±1
 *     occasionally based on the error sign; it does not perform fractional delay.
 * - DSD_NEO_C4FM_CLK_SYNC
 *     Allow C4FM clock assist to remain active while synchronized (fine-trim).
 *     Values: 1 enable, else disabled. Default: 0 (disabled; assist runs only pre-sync).
 *
 * Audio processing
 * - DSD_NEO_DEEMPH
 *     Post-demod deemphasis time constant. Applies only when the active demod preset enables deemphasis.
 *     Values: "75" (75µs, default), "50" (50µs), "nfm" (~750µs), "off" (disable).
 * - DSD_NEO_AUDIO_LPF
 *     Optional one-pole low-pass filter after demod. Approximate cutoff in Hz.
 *     Values: "off" or "0" to disable; integer (e.g., 3000, 5000) to enable. Default: off.
 *
 * FM/C4FM amplitude stabilization (pre-discriminator)
 * - DSD_NEO_FM_AGC
 *     Enable a constant-envelope limiter/AGC on complex I/Q before FM discrimination. Helps stabilize
 *     RTL-SDR amplitude bounce (e.g., +/-3 dB) that can raise P25 P1 error rates.
 *     Default: off for all modes. Values: 1 enable, 0 disable.
 * - DSD_NEO_FM_AGC_TARGET
 *     Target RMS amplitude of the complex envelope |z| using normalized float samples (~0..1).
 *     Typical 0.2..0.6. Default: 0.30.
 * - DSD_NEO_FM_AGC_MIN
 *     Minimum RMS to engage AGC (normalized); below this, gain is held to avoid boosting noise.
 *     Default: 0.06.
 * - DSD_NEO_FM_AGC_ALPHA_UP, DSD_NEO_FM_AGC_ALPHA_DOWN
 *     Smoothing factors when the computed block gain increases vs decreases, respectively.
 *     Larger values react faster. Defaults: ALPHA_UP=0.25, ALPHA_DOWN=0.75.
 * - DSD_NEO_FM_LIMITER
 *     Enable constant-envelope limiter that normalizes each complex sample to a near-constant
 *     magnitude around the AGC target. Helpful to clamp fast AM ripple. Default: off (try enabling
 *     for P25 P1 if AGC alone is insufficient).
 *
 * Complex DC offset removal (baseband)
 * - DSD_NEO_IQ_DC_BLOCK
 *     Enable a leaky integrator high-pass on I/Q before FM discrimination: dc += (x-dc)>>k; y=x-dc.
 *     Values: 1 enable, 0 disable. Default: off.
 * - DSD_NEO_IQ_DC_SHIFT
 *     k in the above relation (10..14 typical, larger=k -> slower). Default: 11.
 *
 * Channel complex low-pass (RTL baseband)
 * - DSD_NEO_CHANNEL_LPF
 *     Optional complex low-pass on the RTL DSP baseband after half-band/CIC
 *     decimation. Intended to narrow out-of-channel noise when running at
 *     higher baseband rates (e.g., 24 kHz). By default this is enabled only
 *     for analog-like modes at >=20 kHz and disabled for digital voice modes.
 *     Values: 0 to force off; non-zero to force on regardless of mode.
 * Frontend tuning behavior
 * - DSD_NEO_DISABLE_FS4_SHIFT
 *     Disable +fs/4 capture frequency shift when offset_tuning is off. Useful for trunking where exact
 *     LO=center is desired by controller logic. Values: 1 to disable, else enabled. Default: 0 (enabled).
 * - DSD_NEO_OUTPUT_CLEAR_ON_RETUNE
 *     Force clearing the output audio ring on retune/hop. When disabled, audio drains naturally to avoid
 *     cutting off transmissions. Values: 1 to clear, else drain. Default: 0 (drain).
 * - DSD_NEO_RETUNE_DRAIN_MS
 *     Maximum time in milliseconds to wait for output ring to drain on retune/hop when not clearing.
 *     Default: 50ms.
 *
 * TCP audio input
 * - DSD_NEO_TCPIN_BACKOFF_MS
 *     Backoff time (milliseconds) before attempting to reconnect when TCP audio input stalls/disconnects.
 *     Values: integer 50..5000. Default: 300.
 *
 * Symbol window debug/testing
 * - DSD_NEO_WINDOW_FREEZE
 *     Freeze symbol decision window selection and disable auto-centering nudges. Useful for A/B testing.
 *     Values: 1 freeze, else dynamic. Default: 0 (dynamic).
 *
 * Intra-block multithreading
 * - DSD_NEO_MT
 *     Enable a minimal 2-thread worker pool for certain CPU-heavy inner loops.
 *     Values: 1 enable, else disabled. Default: 0 (disabled).
 *
 * Debug/advanced knobs (centralized for maintainability)
 * - DSD_NEO_DEBUG_SYNC, DSD_NEO_DEBUG_CQPSK
 * - DSD_NEO_CQPSK, DSD_NEO_CQPSK_SYNC_INV, DSD_NEO_CQPSK_SYNC_NEG
 * - DSD_NEO_SYNC_WARMSTART
 * - DSD_NEO_FTZ_DAZ
 * - DSD_NEO_NO_BOOTSTRAP
 *
 * TCP/RTL/rigctl knobs
 * - DSD_NEO_TCP_* (autotune, waitall, stats, buffers, timeouts, prebuffer)
 * - DSD_NEO_RIGCTL_RCVTIMEO
 * - DSD_NEO_RTL_* and DSD_NEO_TUNER_* (direct/offset, xtal, testmode, autogain)
 * - DSD_NEO_AUTO_PPM* (spectrum-based PPM correction)
 *
 * Protocol timers/holds
 * - DSD_NEO_P25_* and DSD_NEO_DMR_* (hangtimes, grace windows, holds, watchdog)
 * - DSD_NEO_P25P1_SOFT_ERASURE_THRESH, DSD_NEO_P25P2_SOFT_ERASURE_THRESH
 *
 * Cache/path knobs
 * - DSD_NEO_CACHE_DIR, DSD_NEO_CC_CACHE
 */

typedef enum {
    DSD_NEO_DEEMPH_UNSET = 0,
    DSD_NEO_DEEMPH_OFF,
    DSD_NEO_DEEMPH_50,
    DSD_NEO_DEEMPH_75,
    DSD_NEO_DEEMPH_NFM
} dsdneoDeemphMode;

typedef struct dsdneoRuntimeConfig {
    /* Field order is chosen to minimize padding and keep clang-tidy's
     * clang-analyzer-optin.performance.Padding check quiet. */

    /* 8-byte aligned scalars */

    /* DMR / trunking timers */
    double dmr_hangtime_s;
    double dmr_grant_timeout_s;

    /* P25 timers/holds */
    double p25_hangtime_s;
    double p25_grant_timeout_s;
    double p25_cc_grace_s;
    double p25_vc_grace_s;
    double p25_ring_hold_s;
    double p25_mac_hold_s;
    double p25_voice_hold_s;

    /* P25 follower (UI-exposed) knobs */
    double p25_min_follow_dwell_s;
    double p25_grant_voice_to_s;
    double p25_retune_backoff_s;
    double p25_force_release_extra_s;
    double p25_force_release_margin_s;
    double p25p1_err_hold_pct;
    double p25p1_err_hold_s;

    /* Input processing knobs */
    double input_warn_db;

    /* Supervisory tuner autogain knobs */
    double tuner_autogain_seed_db;
    double tuner_autogain_spec_snr_db;
    double tuner_autogain_inband_ratio;
    double tuner_autogain_up_step_db;

    /* Auto-PPM (spectrum-based) knobs */
    double auto_ppm_snr_db;
    double auto_ppm_pwr_db;
    double auto_ppm_zerolock_ppm;

    /* CQPSK Costas loop (carrier recovery) */
    double costas_loop_bw;
    double costas_damping;

    /* DMR TIII tools (one-shot LCN calculator) */
    long dmr_t3_step_hz;
    long dmr_t3_cc_freq_hz;
    long dmr_t3_cc_lcn;
    long dmr_t3_start_lcn;

    /* Realtime scheduling and CPU affinity */
    int rt_sched_is_set;
    int rt_sched_enable;
    int rt_prio_usb_is_set;
    int rt_prio_usb;
    int rt_prio_dongle_is_set;
    int rt_prio_dongle;
    int rt_prio_demod_is_set;
    int rt_prio_demod;
    int cpu_usb_is_set;
    int cpu_usb;
    int cpu_dongle_is_set;
    int cpu_dongle;
    int cpu_demod_is_set;
    int cpu_demod;

    /* Bootstrap/system toggles */
    int ftz_daz_is_set;
    int ftz_daz_enable;
    int no_bootstrap_is_set;
    int no_bootstrap_enable;

    /* Debug/tuning toggles */
    int debug_sync_is_set;
    int debug_sync_enable;
    int debug_cqpsk_is_set;
    int debug_cqpsk_enable;

    /* CQPSK runtime toggles */
    int cqpsk_is_set;
    int cqpsk_enable;
    int cqpsk_sync_inv_is_set;
    int cqpsk_sync_inv;
    int cqpsk_sync_neg_is_set;
    int cqpsk_sync_neg;

    /* Sync warm-start (kill-switch) */
    int sync_warmstart_is_set;
    int sync_warmstart_enable;

    /* DMR / trunking timers */
    int dmr_hangtime_is_set;
    int dmr_grant_timeout_is_set;

    /* P25 timers/holds */
    int p25_hangtime_is_set;
    int p25_grant_timeout_is_set;
    int p25_cc_grace_is_set;
    int p25_vc_grace_is_set;
    int p25_ring_hold_is_set;
    int p25_mac_hold_is_set;
    int p25_voice_hold_is_set;
    int p25_wd_ms_is_set;
    int p25_wd_ms;

    /* P25 follower (UI-exposed) knobs */
    int p25_min_follow_dwell_is_set;
    int p25_grant_voice_to_is_set;
    int p25_retune_backoff_is_set;
    int p25_force_release_extra_is_set;
    int p25_force_release_margin_is_set;
    int p25p1_err_hold_pct_is_set;
    int p25p1_err_hold_s_is_set;

    /* P25 soft-decision erasure thresholds (0..255) */
    int p25p1_soft_erasure_thresh_is_set;
    int p25p1_soft_erasure_thresh;
    int p25p2_soft_erasure_thresh_is_set;
    int p25p2_soft_erasure_thresh;

    /* Input processing knobs */
    int input_volume_is_set;
    int input_volume_multiplier;
    int input_warn_db_is_set;

    /* DMR TIII tools (one-shot LCN calculator) */
    int dmr_t3_calc_csv_is_set;
    int dmr_t3_step_hz_is_set;
    int dmr_t3_cc_freq_is_set;
    int dmr_t3_cc_lcn_is_set;
    int dmr_t3_start_lcn_is_set;

    /* DMR TIII heuristic fill (opt-in) */
    int dmr_t3_heur_is_set;
    int dmr_t3_heur_enable;

    /* User config discovery */
    int config_path_is_set;

    /* Cache/path knobs */
    int cache_dir_is_set;
    int cc_cache_is_set;
    int cc_cache_enable;

    /* TCP/rigctl knobs */
    int tcp_bufsz_is_set;
    int tcp_bufsz_bytes;
    int tcp_waitall_is_set;
    int tcp_waitall_enable;
    int tcp_autotune_is_set;
    int tcp_autotune_enable;
    int tcp_stats_is_set;
    int tcp_stats_enable;
    int tcp_max_timeouts_is_set;
    int tcp_max_timeouts;
    int tcp_rcvbuf_is_set;
    int tcp_rcvbuf_bytes;
    int tcp_rcvtimeo_is_set;
    int tcp_rcvtimeo_ms;
    int rigctl_rcvtimeo_is_set;
    int rigctl_rcvtimeo_ms;
    int tcp_prebuf_ms_is_set;
    int tcp_prebuf_ms;

    /* RTL device/tuner knobs */
    int rtl_agc_is_set;
    int rtl_agc_enable;
    int rtl_direct_is_set;
    int rtl_direct_mode; /* 0=off, 1=I, 2=Q */
    int rtl_offset_tuning_is_set;
    int rtl_offset_tuning_enable;
    int rtl_xtal_hz_is_set;
    int rtl_xtal_hz;
    int tuner_xtal_hz_is_set;
    int tuner_xtal_hz;
    int rtl_testmode_is_set;
    int rtl_testmode_enable;
    int rtl_if_gains_is_set;
    int tuner_bw_hz_is_set;
    int tuner_bw_hz; /* 0=auto */

    /* Supervisory tuner autogain knobs */
    int tuner_autogain_is_set;
    int tuner_autogain_enable;
    int tuner_autogain_probe_ms_is_set;
    int tuner_autogain_probe_ms;
    int tuner_autogain_seed_db_is_set;
    int tuner_autogain_spec_snr_db_is_set;
    int tuner_autogain_inband_ratio_is_set;
    int tuner_autogain_up_step_db_is_set;
    int tuner_autogain_up_persist_is_set;
    int tuner_autogain_up_persist;

    /* Auto-PPM (spectrum-based) knobs */
    int auto_ppm_is_set;
    int auto_ppm_enable;
    int auto_ppm_snr_db_is_set;
    int auto_ppm_pwr_db_is_set;
    int auto_ppm_zerolock_ppm_is_set;
    int auto_ppm_zerolock_hz_is_set;
    int auto_ppm_zerolock_hz;
    int auto_ppm_freeze_is_set;
    int auto_ppm_freeze_enable;

    /* Combine rotate + widen */
    int combine_rot_is_set;
    int combine_rot;

    /* Legacy upsampler fixed-point toggle */
    int upsample_fp_is_set;
    int upsample_fp;

    /* Rational resampler target */
    int resamp_is_set;    /* env seen */
    int resamp_disable;   /* env explicitly disables */
    int resamp_target_hz; /* >0 when enabled */

    /* Residual CFO FLL - native float parameters (GNU Radio style) */
    int fll_is_set;
    int fll_enable;
    int fll_alpha_is_set;
    float fll_alpha; /* proportional gain (typ 0.001-0.01) */
    int fll_beta_is_set;
    float fll_beta; /* integral gain (typ 0.0001-0.001) */
    int fll_deadband_is_set;
    float fll_deadband; /* minimum error magnitude to update (typ 0.001-0.01) */
    int fll_slew_is_set;
    float fll_slew_max; /* max per-sample freq change (rad/sample) */

    /* CQPSK Costas loop (carrier recovery) */
    int costas_bw_is_set;
    int costas_damping_is_set;

    /* Gardner TED - native float parameters */
    int ted_is_set;
    int ted_enable;
    int ted_gain_is_set;
    float ted_gain; /* timing error gain (typ 0.01-0.1) */
    int ted_force_is_set;
    int ted_force;

    /* C4FM clock assist */
    int c4fm_clk_is_set;      /* env seen */
    int c4fm_clk_mode;        /* 0=off, 1=EL, 2=MM */
    int c4fm_clk_sync_is_set; /* env seen */
    int c4fm_clk_sync;        /* 0=pre-sync only, 1=also while synced */

    /* Deemphasis */
    int deemph_is_set;
    dsdneoDeemphMode deemph_mode;

    /* Post-demod audio LPF */
    int audio_lpf_is_set;
    int audio_lpf_disable;
    int audio_lpf_cutoff_hz; /* >0 when enabled */

    /* Intra-block multithreading */
    int mt_is_set;
    int mt_enable;

    /* Frontend tuning behavior */
    int fs4_shift_disable_is_set;
    int fs4_shift_disable;
    int output_clear_on_retune_is_set;
    int output_clear_on_retune;
    int retune_drain_ms_is_set;
    int retune_drain_ms;

    /* TCP audio input */
    int tcpin_backoff_ms_is_set;
    int tcpin_backoff_ms;

    /* Symbol window debug/testing */
    int window_freeze_is_set;
    int window_freeze;

    /* Optional JSON emitter for P25 PDUs */
    int pdu_json_is_set;
    int pdu_json_enable;

    /* Optional SNR-based digital squelch (dB threshold). When set, frame sync
     * may skip expensive searches if estimated SNR is below this value.
     * Applies to relevant digital modes (e.g., P25 C4FM/CQPSK, GFSK family). */
    int snr_sql_is_set;
    int snr_sql_db; /* integer dB threshold */

    /* FM/C4FM amplitude AGC */
    int fm_agc_is_set;
    int fm_agc_enable;
    int fm_agc_target_is_set;
    float fm_agc_target_rms;
    int fm_agc_min_is_set;
    float fm_agc_min_rms;
    int fm_agc_alpha_up_is_set;
    float fm_agc_alpha_up;
    int fm_agc_alpha_down_is_set;
    float fm_agc_alpha_down;

    /* FM constant-envelope limiter */
    int fm_limiter_is_set;
    int fm_limiter_enable;

    /* Complex DC blocker */
    int iq_dc_block_is_set;
    int iq_dc_block_enable;
    int iq_dc_shift_is_set;
    int iq_dc_shift;

    /* RTL channel complex low-pass (post-HB, complex baseband).
       Allows narrowing noise when running the RTL DSP baseband at higher
       sample rates (e.g., 24 kHz) without forcing it on for all modes. */
    int channel_lpf_is_set; /* env seen */
    int channel_lpf_enable; /* 0=off, 1=on */

    /* Inline strings */
    char dmr_t3_calc_csv[1024];
    char config_path[1024];
    char cache_dir[1024];
    char rtl_if_gains[1024];
}

dsdneoRuntimeConfig;

/* Parse environment once. Safe to call multiple times; last call wins. */
/**
 * @brief Parse environment variables and initialize the runtime configuration.
 *
 * Safe to call multiple times; the most recent call wins.
 *
 * @param opts Decoder options for potential precedence overrides.
 */
void dsd_neo_config_init(const dsd_opts* opts);

/* Get immutable pointer to current runtime config. */
/**
 * @brief Get immutable pointer to the current runtime configuration, or NULL if
 * initialization has not been performed.
 *
 * @return Pointer to config or NULL.
 */
const dsdneoRuntimeConfig* dsd_neo_get_config(void);

/**
 * @brief Apply runtime config values to opts/state.
 *
 * Intended to centralize env-derived operational knobs that are still stored in
 * `dsd_opts` / `dsd_state` fields.
 *
 * @param cfg Runtime config snapshot (may be NULL).
 * @param opts Decoder options to mutate (may be NULL).
 * @param state Decoder state to mutate (may be NULL).
 */
void dsd_apply_runtime_config_to_opts(const dsdneoRuntimeConfig* cfg, dsd_opts* opts, dsd_state* state);

/**
 * @brief Read an environment variable value via runtime wrappers.
 *
 * Intended for UI/debug tooling that needs generic env access without calling
 * `getenv()` directly outside runtime.
 *
 * @param name Environment variable name.
 * @return Pointer to value string (owned by the C library) or NULL if unset.
 */
const char* dsd_neo_env_get(const char* name);

/* Runtime control for C4FM clock assist (0=off, 1=EL, 2=MM) */
/**
 * @brief Set the C4FM clock-assist mode (0=off, 1=EL, 2=MM). Values outside range clamp to 0.
 *
 * @param mode Clock-assist mode (0..2).
 */
void dsd_neo_set_c4fm_clk(int mode);
/** @brief Get the C4FM clock-assist mode (0=off, 1=EL, 2=MM). */
int dsd_neo_get_c4fm_clk(void);
/* Toggle C4FM clock assist while synced (0/1) */
/**
 * @brief Enable or disable C4FM clock assist while synchronized (0/1).
 *
 * @param enable Non-zero to enable; zero to disable.
 */
void dsd_neo_set_c4fm_clk_sync(int enable);
/** @brief Return C4FM clock-assist-while-sync flag (0/1). */
int dsd_neo_get_c4fm_clk_sync(void);

/*
 * User configuration (INI file)
 *
 * Represents persisted user preferences loaded from or written to an INI-style
 * configuration file. This is a narrow subset of dsd_opts/dsd_state focusing
 * on stable, user-facing knobs (input, output, decode mode, trunking).
 */

typedef enum {
    DSDCFG_INPUT_UNSET = 0,
    DSDCFG_INPUT_PULSE,
    DSDCFG_INPUT_RTL,
    DSDCFG_INPUT_RTLTCP,
    DSDCFG_INPUT_FILE,
    DSDCFG_INPUT_TCP,
    DSDCFG_INPUT_UDP
} dsdneoUserInputSource;

typedef enum { DSDCFG_OUTPUT_UNSET = 0, DSDCFG_OUTPUT_PULSE, DSDCFG_OUTPUT_NULL } dsdneoUserOutputBackend;

typedef enum {
    DSDCFG_MODE_UNSET = 0,
    DSDCFG_MODE_AUTO,
    DSDCFG_MODE_P25P1,
    DSDCFG_MODE_P25P2,
    DSDCFG_MODE_DMR,
    DSDCFG_MODE_NXDN48,
    DSDCFG_MODE_NXDN96,
    DSDCFG_MODE_X2TDMA,
    DSDCFG_MODE_YSF,
    DSDCFG_MODE_DSTAR,
    DSDCFG_MODE_EDACS_PV,
    DSDCFG_MODE_DPMR,
    DSDCFG_MODE_M17,
    DSDCFG_MODE_TDMA,
    DSDCFG_MODE_ANALOG
} dsdneoUserDecodeMode;

typedef enum {
    DSDCFG_DEMOD_UNSET = 0,
    DSDCFG_DEMOD_AUTO,
    DSDCFG_DEMOD_C4FM,
    DSDCFG_DEMOD_GFSK,
    DSDCFG_DEMOD_QPSK
} dsdneoUserDemodPath;

typedef struct dsdneoUserConfig {
    int version; /* schema version, currently 1 */

    /* [input] */
    int has_input;
    dsdneoUserInputSource input_source;
    char pulse_input[256];
    int rtl_device;
    char rtl_freq[64];
    int rtl_gain;
    int rtl_ppm;
    int rtl_bw_khz;
    int rtl_sql;
    int rtl_volume;
    int rtl_auto_ppm; /* bool */
    char rtltcp_host[128];
    int rtltcp_port;
    char file_path[1024];
    int file_sample_rate;
    char tcp_host[128];
    int tcp_port;
    char udp_addr[64];
    int udp_port;

    /* [output] */
    int has_output;
    dsdneoUserOutputBackend output_backend;
    char pulse_output[256];
    int ncurses_ui; /* bool */

    /* [mode] */
    int has_mode;
    dsdneoUserDecodeMode decode_mode;
    int has_demod;
    dsdneoUserDemodPath demod_path;

    /* [trunking] */
    int has_trunking;
    int trunk_enabled;
    char trunk_chan_csv[1024];
    char trunk_group_csv[1024];
    int trunk_use_allow_list;
    int trunk_tune_group_calls;
    int trunk_tune_private_calls;
    int trunk_tune_data_calls;
    int trunk_tune_enc_calls;

    /* [logging] */
    int has_logging;
    char event_log[1024];

    /* [recording] */
    int has_recording;
    int per_call_wav; /* bool */
    char per_call_wav_dir[512];
    char static_wav_path[1024];
    char raw_wav_path[1024];

    /* [dsp] */
    int has_dsp;
    int iq_balance;  /* bool */
    int iq_dc_block; /* bool */
} dsdneoUserConfig;

/**
 * @brief Resolve the platform-specific default config path (no I/O).
 *
 * Returns a pointer to an internal static buffer, or NULL when no reasonable
 * default can be determined.
 *
 * @return Pointer to default path string or NULL when unavailable.
 */
const char* dsd_user_config_default_path(void);

/**
 * @brief Load a user config from the given path.
 *
 * On error (missing/unreadable file or parse error), cfg is zeroed.
 *
 * @param path Path to the INI file.
 * @param cfg [out] Destination user config.
 * @return 0 on success; non-zero on error.
 */
int dsd_user_config_load(const char* path, dsdneoUserConfig* cfg);

/**
 * @brief Atomically write cfg to the given path (for interactive save).
 *
 * @param path Destination path for the INI file.
 * @param cfg User config to persist.
 * @return 0 on success; non-zero on error.
 */
int dsd_user_config_save_atomic(const char* path, const dsdneoUserConfig* cfg);

/**
 * @brief Apply config-derived defaults to opts/state before env + CLI precedence.
 *
 * @param cfg User config to apply.
 * @param opts Decoder options to mutate.
 * @param state Decoder state to mutate.
 */
void dsd_apply_user_config_to_opts(const dsdneoUserConfig* cfg, dsd_opts* opts, dsd_state* state);

/**
 * @brief Snapshot current opts/state into a user config (for save/print).
 *
 * @param opts Decoder options to read.
 * @param state Decoder state to read.
 * @param cfg [out] Destination user config snapshot.
 */
void dsd_snapshot_opts_to_user_config(const dsd_opts* opts, const dsd_state* state, dsdneoUserConfig* cfg);

/**
 * @brief Render a user config as INI to the given stream (stdout/stderr/file).
 *
 * @param cfg User config to render.
 * @param stream Output stream (stdout/stderr/file).
 */
void dsd_user_config_render_ini(const dsdneoUserConfig* cfg, FILE* stream);

/**
 * @brief Render a commented config template with all options and defaults.
 *
 * Generates a fully-commented INI file showing all available configuration
 * keys with their descriptions, types, and default values.
 *
 * @param stream Output stream (stdout/file).
 */
void dsd_user_config_render_template(FILE* stream);

/**
 * @brief Expand shell-like variables in a path string.
 *
 * Expands:
 *   ~       -> $HOME or platform home directory
 *   $VAR    -> environment variable VAR
 *   ${VAR}  -> environment variable VAR (braced form)
 *
 * Missing variables expand to empty string (no error).
 *
 * @param input Input path with possible variables.
 * @param output Output buffer for expanded path.
 * @param output_size Size of output buffer.
 * @return 0 on success; -1 on truncation or error.
 */
int dsd_config_expand_path(const char* input, char* output, size_t output_size);

/**
 * @brief Load a user config with optional profile overlay.
 *
 * Loads the base configuration from the INI file. If profile_name is non-NULL,
 * the named [profile.NAME] section is applied on top of the base config.
 *
 * Profile sections use dotted key syntax: section.key = value
 *
 * @param path Path to INI file.
 * @param profile_name Profile name (NULL for base config only).
 * @param cfg [out] Destination user config.
 * @return 0 on success; non-zero on error.
 */
int dsd_user_config_load_profile(const char* path, const char* profile_name, dsdneoUserConfig* cfg);

/**
 * @brief List available profile names in a config file.
 *
 * Scans the INI file for [profile.NAME] sections and returns the names.
 *
 * @param path Path to INI file.
 * @param names Output array of profile name pointers (caller provides).
 * @param names_buf Buffer to store profile name strings.
 * @param names_buf_size Size of names buffer.
 * @param max_names Maximum number of names to return.
 * @return Number of profiles found, or -1 on error.
 */
int dsd_user_config_list_profiles(const char* path, const char** names, char* names_buf, size_t names_buf_size,
                                  int max_names);

/**
 * @brief Validate a config file and collect diagnostics.
 *
 * Parses the config file and checks for:
 *   - Unknown keys (warning)
 *   - Type mismatches (error)
 *   - Value range violations (warning)
 *   - Deprecated key usage (info)
 *
 * @param path Path to INI file.
 * @param diags [out] Diagnostic results (caller frees via dsd_user_config_diags_free).
 * @return 0 if no errors; non-zero if errors present.
 */
int dsd_user_config_validate(const char* path, dsdcfg_diagnostics_t* diags);

/**
 * @brief Free diagnostic results from validation.
 *
 * @param diags Diagnostics structure to free.
 */
void dsd_user_config_diags_free(dsdcfg_diagnostics_t* diags);

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_RUNTIME_CONFIG_H */
