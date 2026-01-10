// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief RTL-SDR demodulation configuration helpers.
 *
 * Provides a small surface for configuring the demodulation state and
 * related runtime DSP settings used by the RTL-SDR stream pipeline.
 * Exposes only pointer types so callers avoid heavy struct includes.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

struct demod_state;
struct output_state;

/**
 * Initialize the demodulator state for the requested mode (digital,
 * analog, or RO2) and attach its output ring target.
 *
 * @param demod            Demodulator state to initialize.
 * @param output           Output ring state used as the demod target.
 * @param opts             Decoder options (mode flags).
 * @param rtl_dsp_bw_hz    DSP baseband bandwidth in Hz.
 */
void rtl_demod_init_for_mode(struct demod_state* demod, struct output_state* output, const dsd_opts* opts,
                             int rtl_dsp_bw_hz);

/**
 * Apply environment- and options-driven DSP configuration to the
 * demodulator (resampler target, FLL/TED and CQPSK path toggles,
 * FM AGC, etc.).
 *
 * @param demod Demodulator state.
 * @param opts  Decoder options (CLI/runtime flags).
 */
void rtl_demod_config_from_env_and_opts(struct demod_state* demod, dsd_opts* opts);

/**
 * Apply sensible defaults for digital vs analog modes when env/CLI
 * overrides are not present (TED/FLL defaults, TED SPS, etc.).
 *
 * @param demod  Demodulator state.
 * @param opts   Decoder options (mode flags).
 * @param output Output state used to infer effective sample rate.
 */
void rtl_demod_select_defaults_for_mode(struct demod_state* demod, dsd_opts* opts, const struct output_state* output);

/**
 * Recompute resampler configuration when the demod output rate changes,
 * updating output.rate accordingly.
 *
 * @param demod         Demodulator state.
 * @param output        Output state to update.
 * @param rtl_dsp_bw_hz DSP baseband bandwidth in Hz (fallback when rate_out is unset).
 */
void rtl_demod_maybe_update_resampler_after_rate_change(struct demod_state* demod, struct output_state* output,
                                                        int rtl_dsp_bw_hz);

/**
 * Refresh TED SPS after rate changes unless explicitly overridden by
 * runtime configuration.
 *
 * @param demod  Demodulator state.
 * @param opts   Decoder options (mode flags).
 * @param output Output state (current sink rate).
 */
void rtl_demod_maybe_refresh_ted_sps_after_rate_change(struct demod_state* demod, const dsd_opts* opts,
                                                       const struct output_state* output);

/**
 * Release resources owned by the demodulator state.
 *
 * @param demod Demodulator state to clean up.
 */
void rtl_demod_cleanup(struct demod_state* demod);

#ifdef __cplusplus
}
#endif
