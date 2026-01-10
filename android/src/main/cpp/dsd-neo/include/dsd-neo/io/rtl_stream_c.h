// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief C shim header for the RTL-SDR orchestrator.
 *
 * Declares a minimal C API mirroring lifecycle, tuning, and I/O operations
 * of the C++ `RtlSdrOrchestrator`, for consumption by C translation units.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <dsd-neo/core/opts_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque stream context */
typedef struct RtlSdrContext RtlSdrContext;

/* Lifecycle */
/**
 * @brief Create a new RTL-SDR stream context from options.
 * @param opts Decoder options snapshot used to configure the stream. Must not be NULL.
 * @param out_ctx [out] On success, receives an opaque context pointer.
 * @return 0 on success; otherwise <0 on error.
 */
int rtl_stream_create(const dsd_opts* opts, RtlSdrContext** out_ctx);
/**
 * @brief Start the stream threads and device I/O.
 * @param ctx Stream context created by rtl_stream_create().
 * @return 0 on success; otherwise <0 on error.
 */
int rtl_stream_start(RtlSdrContext* ctx);
/**
 * @brief Stop the stream and cleanup resources associated with the run.
 * Safe to call multiple times; subsequent calls are no-ops.
 * @param ctx Stream context created by rtl_stream_create().
 * @return 0 on success; otherwise <0 on error.
 */
int rtl_stream_stop(RtlSdrContext* ctx);
/**
 * @brief Gracefully stop the stream without asserting global exit flags.
 *
 * Mirrors rtl_stream_stop() but avoids toggling global shutdown state so the
 * UI can reconfigure and restart streaming without terminating the process.
 *
 * @param ctx Stream context created by rtl_stream_create().
 * @return 0 on success; otherwise <0 on error.
 */
int rtl_stream_soft_stop(RtlSdrContext* ctx);
/**
 * @brief Destroy the stream context and free all associated resources.
 * If the stream is running, it is stopped before destruction.
 * @param ctx Stream context to destroy. May be NULL.
 * @return 0 always.
 */
int rtl_stream_destroy(RtlSdrContext* ctx);

/* Control */
/**
 * @brief Tune to a new center frequency.
 * @param ctx Stream context.
 * @param center_freq_hz New center frequency in Hz.
 * @return 0 on success; otherwise <0 on error.
 */
int rtl_stream_tune(RtlSdrContext* ctx, uint32_t center_freq_hz);

/* I/O */
/**
 * @brief Read up to `count` interleaved audio samples into `out`.
 * @param ctx Stream context.
 * @param out Destination buffer for samples. Must not be NULL.
 * @param count Maximum number of samples to read.
 * @param out_got [out] Set to the number of samples actually read.
 * @return 0 on success; otherwise <0 on error (e.g., shutdown).
 */
int rtl_stream_read(RtlSdrContext* ctx, float* out, size_t count, int* out_got);
/**
 * @brief Get the current output sample rate in Hz.
 * @param ctx Stream context.
 * @return Output sample rate in Hz; returns 0 if `ctx` is invalid.
 */
uint32_t rtl_stream_output_rate(const RtlSdrContext* ctx);

/* Optional helpers to mirror legacy API behavior */
/**
 * @brief Clear the output ring buffer and wake any waiting producer.
 * Mirrors the legacy behavior. The `ctx` parameter is currently ignored.
 * @param ctx Stream context (unused).
 */
void rtl_stream_clear_output(RtlSdrContext* ctx);
/**
 * @brief Return mean power approximation (RMS^2 proxy) for soft squelch.
 * The computation uses a small fixed sample window and mirrors the legacy implementation.
 * @param ctx Stream context (unused).
 * @return Mean power value (approximate RMS squared, normalized to full scale 1.0).
 */
double rtl_stream_return_pwr(const RtlSdrContext* ctx);

/**
 * @brief Set the channel squelch level in the demod state.
 *
 * Call this whenever opts->rtl_squelch_level changes to keep the demod
 * state in sync for channel-based squelching.
 *
 * @param level Linear power threshold (same units as rtl_squelch_level).
 */
void rtl_stream_set_channel_squelch(float level);

/**
 * @brief Enable or disable RTL-SDR bias tee at runtime.
 *
 * Applies to the active device if present. For rtl_tcp sources, forwards the
 * request to the server (protocol cmd 0x0E). For USB sources, requires
 * librtlsdr built with bias tee API support.
 *
 * @param on Non-zero to enable, zero to disable.
 * @return 0 on success; negative on failure or when no device is active.
 */
int rtl_stream_set_bias_tee(int on);

/**
 * @brief Get the currently applied tuner gain.
 *
 * Returns the driver-reported tuner gain in tenths of a dB (e.g., 270 for
 * 27.0 dB) via out_tenth_db, and whether auto-gain is active via out_is_auto
 * (1=auto, 0=manual). Any pointer may be NULL. Returns 0 on success; <0 if
 * the RTL stream/device is not available.
 */
int rtl_stream_get_gain(int* out_tenth_db, int* out_is_auto);
/**
 * @brief Return whether rtl_tcp adaptive buffering/autotune is enabled.
 * @return 1 when enabled; 0 when disabled; negative on error.
 */
int rtl_stream_get_rtltcp_autotune(void);
/**
 * @brief Enable or disable rtl_tcp adaptive buffering/autotune logic.
 * @param onoff Non-zero to enable; zero to disable.
 */
void rtl_stream_set_rtltcp_autotune(int onoff);

/**
 * @brief Get smoothed TED residual (EMA of Gardner error) from demod pipeline.
 * Positive: persistent early (nudge center right). Negative: late (nudge left).
 * Returns 0 when unavailable.
 *
 * @param ctx Stream context (unused).
 * @return Residual value (coarse units, signed integer).
 */
int rtl_stream_ted_bias(const RtlSdrContext* ctx);

/**
 * @brief Get the configured Gardner TED samples-per-symbol.
 *
 * @return Nominal SPS (>=2); 0 when unavailable.
 */
int rtl_stream_get_ted_sps(void);

/**
 * @brief Set the Gardner TED samples-per-symbol.
 *
 * Use when switching between symbol rates (e.g., P25P1 4800 sym/s vs P25P2 6000 sym/s).
 * The TED will reinitialize its internal state (omega bounds, delay line) on SPS change.
 * Also sets an override flag that persists the value across rate-change refreshes.
 *
 * @param sps Nominal samples per symbol (clamped to [2, 64]).
 */
void rtl_stream_set_ted_sps(int sps);

/**
 * @brief Clear the TED SPS override.
 *
 * Call when returning to control channel to allow normal SPS calculation
 * based on opts mode flags. Without clearing, the voice channel SPS would
 * persist incorrectly.
 */
void rtl_stream_clear_ted_sps_override(void);

/**
 * @brief Set the Gardner TED SPS without asserting the override.
 *
 * Sets ted_sps but leaves ted_sps_override unchanged (typically 0 after
 * clearing). Use when returning to CC or switching protocols where the
 * rate-change refresh should be allowed to recalculate SPS later.
 *
 * @param sps Nominal samples per symbol (clamped to [2, 64]).
 */
void rtl_stream_set_ted_sps_no_override(int sps);

/**
 * @brief Set the Gardner TED loop gain (native float).
 *
 * @param gain Loop gain; typical 0.01..0.1, default ~0.05.
 */
void rtl_stream_set_ted_gain(float gain);

/**
 * @brief Get the current Gardner TED loop gain.
 *
 * @return Native float loop gain.
 */
float rtl_stream_get_ted_gain(void);

/**
 * @brief Force-enable the TED even for FM/C4FM paths (0/1).
 *
 * @param onoff Non-zero to force enable; zero to follow mode defaults.
 */
void rtl_stream_set_ted_force(int onoff);

/**
 * @brief Return the TED force flag (0/1).
 *
 * @return 1 when forced on; 0 otherwise.
 */
int rtl_stream_get_ted_force(void);

/**
 * @brief Capture a snapshot of the eye diagram buffer (timing helper).
 *
 * Copies up to `max_samples` real I-channel samples from the decimated complex
 * baseband into `out` and returns the number of samples copied. Also writes
 * the current nominal SPS into out_sps when available.
 *
 * @param out Destination buffer for I-channel samples (must not be NULL).
 * @param max_samples Maximum number of samples to copy.
 * @param out_sps [out] Receives nominal samples-per-symbol (may be NULL).
 * @return Number of samples written; 0 if unavailable.
 */
int rtl_stream_eye_get(float* out, int max_samples, int* out_sps);

/**
 * @brief Get smoothed demod SNR estimate in dB (post-filter, center-of-symbol).
 *
 * Computed on the demod thread for digital modes using I-channel samples near
 * symbol centers and a 4-level clustering heuristic for C4FM/FSK.
 * Returns a negative value when unavailable.
 *
 * @return SNR in dB, or negative when unavailable.
 */
double rtl_stream_get_snr_c4fm(void);
/**
 * @brief Get smoothed CQPSK/LSM demod SNR estimate in dB.
 * @return SNR in dB, or negative when unavailable.
 */
double rtl_stream_get_snr_cqpsk(void);
/**
 * @brief Get smoothed GFSK demod SNR estimate in dB.
 * @return SNR in dB, or negative when unavailable.
 */
double rtl_stream_get_snr_gfsk(void);

/**
 * @brief Get the current C4FM (4-level FSK) SNR estimator bias in dB.
 *
 * This bias accounts for both the statistical estimator bias and the
 * noise bandwidth correction based on current DSP settings (sample rate,
 * samples per symbol, and channel LPF profile).
 *
 * @return Bias value in dB to subtract from raw SNR estimate.
 */
double rtl_stream_get_snr_bias_c4fm(void);

/**
 * @brief Get the current EVM/GFSK/QPSK SNR estimator bias in dB.
 *
 * This bias accounts for both the statistical estimator bias and the
 * noise bandwidth correction based on current DSP settings.
 *
 * @return Bias value in dB to subtract from raw SNR estimate.
 */
double rtl_stream_get_snr_bias_evm(void);

/**
 * @brief Estimate C4FM SNR from the eye buffer as a lightweight fallback.
 *
 * Uses quartile clustering over eye-diagram I-channel samples near symbol
 * centers to approximate signal and noise variances, returning SNR in dB.
 * Returns a negative value (<= -50 dB) when insufficient data is available.
 */
double rtl_stream_estimate_snr_c4fm_eye(void);

/**
 * @brief Estimate QPSK SNR from the constellation snapshot as a fallback.
 *
 * Uses recent equalized I/Q points to estimate amplitude and EVM vs ideal
 * QPSK targets; returns SNR in dB. Returns <= -50 dB when insufficient data.
 */
double rtl_stream_estimate_snr_qpsk_const(void);

/**
 * @brief Estimate GFSK SNR from the eye buffer as a fallback.
 *
 * Uses a two-level (median split) clustering on eye-diagram I-channel
 * samples near symbol centers; returns SNR in dB. Returns <= -50 dB when
 * insufficient data.
 */
double rtl_stream_estimate_snr_gfsk_eye(void);

/**
 * @brief Get supervisory tuner auto-gain enable flag.
 * @return 1 when auto-gain supervisor is enabled; 0 when disabled.
 */
int rtl_stream_get_tuner_autogain(void);
/**
 * @brief Enable or disable supervisory tuner auto-gain.
 * @param onoff Non-zero to enable; zero to disable.
 */
void rtl_stream_set_tuner_autogain(int onoff);

/**
 * @brief Set the C4FM clock assist mode.
 * @param mode 0=disabled, 1=Early-Late, 2=Mueller-Muller; values outside range clamp to 0.
 */
void rtl_stream_set_c4fm_clk(int mode);
/**
 * @brief Get the current C4FM clock assist mode.
 * @return 0 (off), 1 (EL), or 2 (MM).
 */
int rtl_stream_get_c4fm_clk(void);
/**
 * @brief Enable/disable C4FM clock assist while synchronized.
 * @param enable Non-zero to enable; zero to disable.
 */
void rtl_stream_set_c4fm_clk_sync(int enable);
/**
 * @brief Get C4FM clock-assist-while-sync flag.
 * @return 1 when enabled; 0 when disabled.
 */
int rtl_stream_get_c4fm_clk_sync(void);

/**
 * @brief Get spectrum-based auto PPM status and last measurements.
 *
 * @param enabled [out] Current enable flag (0/1); may be NULL.
 * @param snr_db [out] Latest SNR estimate in dB; may be NULL.
 * @param df_hz [out] Latest residual frequency offset in Hz; may be NULL.
 * @param est_ppm [out] Estimated PPM error relative to center; may be NULL.
 * @param last_dir [out] Last applied step direction (-1,0,+1); may be NULL.
 * @param cooldown [out] Remaining cooldown iterations before next step; may be NULL.
 * @param locked [out] 1 if locked; 0 if training/idle; may be NULL.
 * @return 0 on success; negative on error or when unavailable.
 */
int rtl_stream_auto_ppm_get_status(int* enabled, double* snr_db, double* df_hz, double* est_ppm, int* last_dir,
                                   int* cooldown, int* locked);

/**
 * @brief Return 1 when spectrum-based auto-PPM training window is active.
 *
 * @return 1 when enabled, not locked, and currently training; 0 otherwise.
 */
int rtl_stream_auto_ppm_training_active(void);

/**
 * @brief Get locked auto-PPM value and lock-time snapshot, if available.
 *
 * @param ppm [out] Locked PPM value; may be NULL.
 * @param snr_db [out] SNR at lock time in dB; may be NULL.
 * @param df_hz [out] Residual frequency offset at lock time in Hz; may be NULL.
 * @return 0 on success; negative when unavailable.
 */
int rtl_stream_auto_ppm_get_lock(int* ppm, double* snr_db, double* df_hz);
/**
 * @brief Runtime toggle for auto-PPM (0/1).
 *
 * @param onoff Non-zero to enable; zero to disable.
 */
void rtl_stream_set_auto_ppm(int onoff);
/**
 * @brief Return the current runtime auto-PPM toggle value (0/1).
 *
 * @return 1 when auto-PPM is enabled; 0 when disabled.
 */
int rtl_stream_get_auto_ppm(void);

/**
 * @brief Toggle generic IQ balance prefilter (mode-aware image cancel).
 *
 * @param onoff Non-zero to enable; zero to disable.
 */
void rtl_stream_toggle_iq_balance(int onoff);
/** Get generic IQ balance prefilter state; returns 1 if enabled. */
int rtl_stream_get_iq_balance(void);
/**
 * @brief Provide P25P1 FEC OK/ERR deltas to drive BER-adaptive tuning.
 * Call with positive deltas (not totals). No-ops when RTL stream inactive.
 *
 * @param fec_ok_delta Incremental count of successful FEC codewords.
 * @param fec_err_delta Incremental count of failed FEC codewords.
 */
void rtl_stream_p25p1_ber_update(int fec_ok_delta, int fec_err_delta);

/* Coarse DSP feature toggles and snapshot */
/**
 * @brief Toggle CQPSK path pre-processing on/off (0=off, nonzero=on).
 *
 * @param onoff Non-zero to enable; zero to disable.
 */
void rtl_stream_toggle_cqpsk(int onoff);
/**
 * @brief Toggle FLL on/off (0=off, nonzero=on).
 *
 * @param onoff Non-zero to enable; zero to disable.
 */
void rtl_stream_toggle_fll(int onoff);
/**
 * @brief Toggle TED on/off (0=off, nonzero=on).
 *
 * @param onoff Non-zero to enable; zero to disable.
 */
void rtl_stream_toggle_ted(int onoff);
/**
 * @brief Get current coarse DSP feature flags; any pointer may be NULL.
 *
 * @param cqpsk_enable [out] CQPSK enable flag.
 * @param fll_enable [out] FLL enable flag.
 * @param ted_enable [out] TED enable flag.
 * @return 0 on success; negative on error.
 */
int rtl_stream_dsp_get(int* cqpsk_enable, int* fll_enable, int* ted_enable);

/**
 * @brief Set or disable the resampler target rate (applied on controller thread).
 * Pass 0 to disable the resampler; otherwise, pass desired Hz (e.g., 48000).
 *
 * @param target_hz Desired output sample rate in Hz (0 disables the resampler).
 */
void rtl_stream_set_resampler_target(int target_hz);

/**
 * @brief Provide P25 Phase 2 RS/voice error deltas for runtime helpers.
 *
 * Pass positive deltas (not totals). Slot is 0 or 1. Any delta may be 0 when
 * not applicable. No-ops when RTL stream inactive.
 *
 * @param slot Slot index (0 or 1).
 * @param facch_ok_delta Incremental FACCH success count.
 * @param facch_err_delta Incremental FACCH error count.
 * @param sacch_ok_delta Incremental SACCH success count.
 * @param sacch_err_delta Incremental SACCH error count.
 * @param voice_err_delta Incremental voice error count.
 */
void rtl_stream_p25p2_err_update(int slot, int facch_ok_delta, int facch_err_delta, int sacch_ok_delta,
                                 int sacch_err_delta, int voice_err_delta);

/**
 * @brief Capture a snapshot of recent constellation points after DSP.
 *
 * Copies up to `max_points` I/Q pairs into `out_xy` as interleaved floats
 * [I0,Q0,I1,Q1,...] on the normalized float amplitude scale used by the DSP.
 * Returns the number of pairs copied (0 if unavailable).
 *
 * @param out_xy Destination buffer for interleaved I/Q pairs (must not be NULL).
 * @param max_points Maximum number of pairs to write.
 * @return Number of pairs written; 0 if unavailable.
 */
int rtl_stream_constellation_get(float* out_xy, int max_points);

/**
 * @brief Get a snapshot of the current baseband power spectrum (magnitude, dBFS-like).
 *
 * Returns up to max_bins bins in out_db, equally spaced across the complex
 * baseband Nyquist span, with DC-centered ordering (i.e., out_db[0] ~ -Fs/2,
 * mid ~ DC, last ~ +Fs/2). Values are smoothed and approximately in dBFS.
 * Optionally returns the current demod output sample rate via out_rate.
 *
 * @param out_db Destination buffer for spectrum bins (float dB). Must not be NULL.
 * @param max_bins Maximum number of bins to write.
 * @param out_rate Optional pointer to receive current output sample rate in Hz.
 * @return Number of bins written (0 if unavailable).
 */
int rtl_stream_spectrum_get(float* out_db, int max_bins, int* out_rate);

/**
 * @brief Set desired spectrum FFT size (power-of-two, clamped to allowed range).
 *
 * @param n Requested FFT size (power of two within supported bounds).
 * @return 0 on success; negative on error.
 */
int rtl_stream_spectrum_set_size(int n);
/** @brief Get current spectrum FFT size. */
int rtl_stream_spectrum_get_size(void);

/* Carrier/Costas diagnostics and control */
/**
 * @brief Reset Costas loop state for fresh carrier acquisition.
 *
 * Call on channel retunes to clear stale phase/frequency estimates from the
 * previous channel. Without reset, the loop starts with the old frequency
 * offset and must slew to the new carrier, delaying sync acquisition.
 */
void rtl_stream_reset_costas(void);

/** Return current NCO frequency used for carrier rotation (Costas/FLL), in Hz. */
double rtl_stream_get_cfo_hz(void);
/** Return residual CFO estimated from the spectrum peak offset from DC, in Hz. */
double rtl_stream_get_residual_cfo_hz(void);
/** Return 1 when carrier loop appears locked (CQPSK active, CFO/residual small, SNR ok), else 0. */
int rtl_stream_get_carrier_lock(void);
/** Return last average absolute Costas error magnitude (Q14, pi==1<<14). */
int rtl_stream_get_costas_err_q14(void);
/** Return raw NCO frequency control (Q15 cycles per sample). */
int rtl_stream_get_nco_q15(void);
/** Return current demod output sample rate (Hz). */
int rtl_stream_get_demod_rate_hz(void);

/** Return FLL band-edge frequency estimate in Hz (coarse freq offset for CQPSK). */
double rtl_stream_get_fll_band_edge_freq_hz(void);

/* -------- FM/C4FM amplitude stabilization + DC blocker (runtime) -------- */
/** Get FM AGC enable state (1 on, 0 off). */
int rtl_stream_get_fm_agc(void);
/** Enable/disable FM AGC (0 off, nonzero on). */
void rtl_stream_set_fm_agc(int onoff);
/**
 * @brief Get FM AGC parameters (any pointer may be NULL).
 *
 * @param target_rms [out] Target RMS magnitude (normalized float).
 * @param min_rms [out] Minimum RMS to engage AGC (normalized float).
 * @param alpha_up [out] Smoothing when gain increases (0..1).
 * @param alpha_down [out] Smoothing when gain decreases (0..1).
 */
void rtl_stream_get_fm_agc_params(float* target_rms, float* min_rms, float* alpha_up, float* alpha_down);

/**
 * @brief Set FM AGC parameters; pass negative to leave a field unchanged.
 *
 * @param target_rms Target RMS magnitude (normalized); negative to keep existing.
 * @param min_rms Minimum RMS threshold; negative to keep existing.
 * @param alpha_up Smoothing when gain increases (0..1); negative to keep existing.
 * @param alpha_down Smoothing when gain decreases (0..1); negative to keep existing.
 */
void rtl_stream_set_fm_agc_params(float target_rms, float min_rms, float alpha_up, float alpha_down);

/** Get FM constant-envelope limiter state (1 on, 0 off). */
int rtl_stream_get_fm_limiter(void);
/** Enable/disable FM constant-envelope limiter (0 off, nonzero on). */
void rtl_stream_set_fm_limiter(int onoff);

/**
 * @brief Get complex I/Q DC blocker state and shift k.
 *
 * Any pointer may be NULL.
 *
 * @param out_shift_k [out] Current shift exponent k; may be NULL.
 * @return 1 if enabled; 0 otherwise.
 */
int rtl_stream_get_iq_dc(int* out_shift_k);
/**
 * @brief Set DC blocker enable (0/1) and/or shift k (>=6..<=15).
 *
 * @param enable Non-zero to enable; zero to disable.
 * @param shift_k Shift exponent k; pass negative to leave unchanged.
 */
void rtl_stream_set_iq_dc(int enable, int shift_k);

#ifdef __cplusplus
}
#endif
