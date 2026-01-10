// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Public API for Frequency-Locked Loop (FLL) utilities.
 *
 * Provides state and configuration structures and routines to perform
 * NCO-based mixing and frequency-error control suitable for FM demodulation.
 */

#ifndef DSP_FLL_H
#define DSP_FLL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FLL Configuration structure (GNU Radio-style native float) */
typedef struct {
    int enabled;
    float alpha;    /* proportional gain (native float, ~0.002..0.02) */
    float beta;     /* integral gain (native float, ~0.0002..0.002) */
    float deadband; /* ignore small phase errors |err| <= deadband (radians, ~0.01) */
    float slew_max; /* max |delta freq| per update (rad/sample, ~0.005) */
} fll_config_t;

/* FLL State structure - native float (GNU Radio-style) */
typedef struct {
    float freq;  /* NCO frequency increment (rad/sample) */
    float phase; /* NCO phase accumulator (radians, wraps at +/-2*pi) */
    float prev_r;
    float prev_j;
    float integrator; /* PI integrator state (native float), bounded for anti-windup */
    /* Small history of trailing complex samples for symbol-spaced updates. */
    float prev_hist_r[64];
    float prev_hist_j[64];
    int prev_hist_len; /* number of valid samples in prev_hist_* (0..64) */
} fll_state_t;

/**
 * @brief Initialize FLL state with default values.
 *
 * @param state FLL state to initialize.
 */
void fll_init_state(fll_state_t* state);

/**
 * @brief Mix I/Q by an NCO and advance phase by freq_q15 per sample.
 *
 * Phase and frequency are Q15 where a full turn (2*pi) maps to 1<<15.
 * Uses high-quality sin/cos from the math library for rotation.
 *
 * @param config FLL configuration.
 * @param state  FLL state (updates phase_q15).
 * @param x      Input/output interleaved I/Q buffer (modified in-place).
 * @param N      Length of buffer in samples (must be even).
 */
void fll_mix_and_update(const fll_config_t* config, fll_state_t* state, float* x, int N);

/**
 * @brief Estimate frequency error and update FLL control (PI in Q15).
 *
 * Uses a phase-difference discriminator to compute average error.
 * Applies proportional and integral actions to adjust the NCO frequency.
 *
 * @param config FLL configuration.
 * @param state  FLL state (updates freq_q15 and may advance phase_q15).
 * @param x      Input interleaved I/Q buffer.
 * @param N      Length of buffer in samples (must be even).
 */
void fll_update_error(const fll_config_t* config, fll_state_t* state, const float* x, int N);

#ifdef __cplusplus
}
#endif

#endif /* DSP_FLL_H */
