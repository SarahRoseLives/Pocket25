// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief DSP demodulation pipeline public API.
 *
 * Declares decimation, discrimination, deemphasis, DC block, audio filtering,
 * and the full pipeline entrypoint implemented in `src/dsp/demod_pipeline.cpp`.
 */

#ifndef DSP_DEMOD_PIPELINE_H
#define DSP_DEMOD_PIPELINE_H

#include <stdint.h>

#include <dsd-neo/platform/threading.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of demod_state structure */
struct demod_state;

/**
 * Boxcar low-pass and decimate by step (no wraparound).
 * Length must be a multiple of step.
 *
 * @param signal2 In/out buffer of samples.
 * @param len     Length of input buffer.
 * @param step    Decimation factor.
 * @return New length after decimation.
 */
int low_pass_simple(float* signal2, int len, int step);

/**
 * Simple square window FIR on real samples with decimation to rate_out2.
 *
 * @param s Demodulator state (uses result buffer and decimation state).
 */
void low_pass_real(struct demod_state* s);

/**
 * Perform FM discriminator on interleaved low-passed I/Q to produce audio PCM.
 * Uses the active discriminator configured in fm->discriminator.
 *
 * @param fm Demodulator state (uses lowpassed as input, writes to result).
 * @note Renamed to avoid collision with codec2's fm_demod symbol on Windows.
 */
void dsd_fm_demod(struct demod_state* fm);

/**
 * Pass-through demodulator: copies low-passed samples to output unchanged.
 *
 * @param fm Demodulator state (copies lowpassed to result).
 */
void raw_demod(struct demod_state* fm);

/**
 * Differential QPSK demodulator for CQPSK/LSM.
 *
 * Computes the phase difference between consecutive complex samples
 * (arg(z_n * conj(z_{n-1}))) using a fast atan2 approximation and writes
 * the resulting Q14-scaled symbols to the real result buffer. Maintains
 * history across blocks via demod_state.
 *
 * @param fm Demodulator state (reads interleaved I/Q in lowpassed, writes phase deltas to result).
 */
void qpsk_differential_demod(struct demod_state* fm);

/**
 * Apply post-demod deemphasis IIR filter with Q15 coefficient.
 *
 * @param fm Demodulator state (reads/writes result, updates deemph_avg).
 */
void deemph_filter(struct demod_state* fm);

/**
 * Apply a simple DC blocking (leaky integrator high-pass) filter to audio.
 *
 * @param fm Demodulator state (reads/writes result, updates dc_avg).
 */
void dc_block_filter(struct demod_state* fm);

/**
 * Apply a simple one-pole low-pass filter to audio.
 *
 * @param fm Demodulator state (reads/writes result, updates audio_lpf_state).
 */
void audio_lpf_filter(struct demod_state* fm);

/**
 * Calculate mean power (squared RMS) with DC bias removed.
 *
 * @param samples Input samples buffer.
 * @param len     Number of samples.
 * @param step    Step size for sampling.
 * @return Mean power (squared RMS) with DC bias removed.
 */
float mean_power(float* samples, int len, int step);

/**
 * Full demodulation pipeline for one block.
 * Applies decimation via half-band cascade, optional FLL and timing
 * correction, followed by the configured discriminator and post-processing.
 *
 * @param d Demodulator state (consumes lowpassed, produces result).
 */
void full_demod(struct demod_state* d);

#ifdef __cplusplus
}
#endif

#endif /* DSP_DEMOD_PIPELINE_H */
