// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Timing Error Detector (TED) interface: Gardner TED and fractional
 * delay timing correction for symbol synchronization in digital demodulation modes.
 */

#ifndef DSP_TED_H
#define DSP_TED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TED Configuration structure (GNU Radio-style native float) */
typedef struct {
    int enabled;
    int force; /* allow forcing TED even for FM/C4FM paths */
    int sps;   /* nominal samples per symbol. At 48 kHz: P25P1=10, P25P2=8, NXDN=20 */
    /* OP25-compatible Gardner parameters (from p25_demodulator.py) */
    float gain_mu;    /* mu loop gain, default 0.025 (OP25 default) */
    float gain_omega; /* omega loop gain, default 0.1 * gain_mu^2 */
    float omega_rel;  /* relative omega limit, default 0.002 (±0.2%) */
} ted_config_t;

/* Delay line size for MMSE interpolation (matches OP25's NUM_COMPLEX) */
#define TED_DL_SIZE 100

/* TED State structure (native float for precision) - OP25 compatible */
typedef struct {
    float mu;        /* fractional sample phase [0.0, 1.0) */
    float omega;     /* current symbol period estimate (samples per symbol) */
    float omega_mid; /* nominal omega center */
    float omega_min; /* minimum omega (omega_mid * (1 - omega_rel)) */
    float omega_max; /* maximum omega (omega_mid * (1 + omega_rel)) */
    float omega_rel; /* relative omega limit for clipping (OP25: 0.002 = ±0.2%) */
    /* Last symbol sample for OP25 Gardner error computation */
    float last_r;
    float last_j;
    /* Smoothed Gardner error residual (EMA). Sign indicates persistent
       early/late bias; magnitude is relative (normalized by power). */
    float e_ema;
    /* Lock detector accumulator (Yair Linn method, like OP25) */
    float lock_accum;
    int lock_count;
    /* Event counter for lock statistics (reset on unmute transition, like OP25) */
    int event_count;
    /* Circular delay line for MMSE interpolation (OP25-style) */
    float dl[TED_DL_SIZE * 2 * 2]; /* interleaved I/Q, doubled for wrap-free access */
    int dl_index;                  /* current write position */
    int twice_sps;                 /* delay line wrap point: max(2*ceil(omega_max), ceil(omega_max/2)+9) */
    int sps;                       /* last initialized samples-per-symbol */
} ted_state_t;

/**
 * @brief Initialize TED state with default values.
 *
 * @param state TED state to initialize.
 */
void ted_init_state(ted_state_t* state);

/**
 * @brief Soft reset TED state, preserving mu and omega for phase continuity.
 *
 * Use this on frequency retunes where the transmitter symbol clock is consistent.
 * Avoids non-deterministic re-acquisition that occurs when mu resets to 0.
 *
 * @param state TED state to soft-reset.
 */
void ted_soft_reset(ted_state_t* state);

/**
 * @brief OP25-compatible Gardner timing recovery with decimation to symbol rate.
 *
 * Uses 8-tap MMSE polyphase interpolation (matching GNU Radio's interpolator)
 * with OP25's Gardner algorithm for symbol timing recovery. This implementation
 * decimates from sample rate to symbol rate, outputting one complex sample per
 * symbol.
 *
 * Key features:
 *   - 8-tap MMSE polyphase interpolation with linear coefficient interpolation
 *   - Circular delay line with doubled storage for wrap-free access
 *   - OP25's Gardner error formula: (last - current) * mid
 *   - Dual-loop update: both omega (symbol period) and mu (phase)
 *   - Lock detector based on Yair Linn's research
 *
 * @param config TED configuration (gain_mu, gain_omega, omega_rel, sps).
 * @param state  TED state (mu, omega, last_sample, delay_line).
 * @param x      Input/output interleaved I/Q buffer. On output, contains
 *               symbol-rate samples.
 * @param N      Pointer to buffer length (interleaved floats). Updated with
 *               output length (will be smaller due to decimation).
 * @param y      Work buffer for symbol-rate output (must be at least size N).
 * @note This function decimates to symbol rate and is designed for CQPSK paths.
 *       Do not use for FM/C4FM paths that expect sample-rate data downstream.
 */
void gardner_timing_adjust(const ted_config_t* config, ted_state_t* state, float* x, int* N, float* y);

/**
 * @brief Legacy non-decimating Gardner timing correction (Farrow-based).
 *
 * Uses a cubic Farrow (cubic convolution) fractional-delay interpolator around
 * the nominal samples-per-symbol to reduce timing error while keeping the
 * output at (approximately) the input sample rate. Intended for FM/C4FM paths
 * that expect sample-rate complex baseband downstream.
 *
 * @param config TED configuration (uses enabled, force, gain_mu, sps).
 * @param state  TED state (uses mu and e_ema for residual reporting).
 * @param x      Input/output interleaved I/Q buffer (modified in-place).
 * @param N      Pointer to buffer length (must be even; may be reduced slightly).
 * @param y      Work buffer for timing-adjusted I/Q (must be at least size N).
 * @note Skips processing when samples-per-symbol is large unless forced.
 */
void gardner_timing_adjust_farrow(const ted_config_t* config, ted_state_t* state, float* x, int* N, float* y);

/**
 * @brief Return the current smoothed TED residual (EMA of Gardner error).
 *
 * Positive values indicate a persistent "sample early" bias (center → right),
 * negative values indicate "sample late" (center → left). Zero means no bias
 * or TED disabled. Returns float for full precision.
 */
static inline float
ted_residual(const ted_state_t* s) {
    return s ? s->e_ema : 0.0f;
}

/**
 * @brief Return the lock detector accumulator value (Yair Linn method).
 *
 * Positive values indicate good lock (symbol energy >> mid-symbol energy).
 * Negative values indicate poor lock or unlocked state.
 * Threshold of ~0.5 * lock_count is a reasonable lock indicator.
 *
 * @return Lock accumulator value, or 0 if state is NULL.
 */
static inline float
ted_lock_accum(const ted_state_t* s) {
    return s ? s->lock_accum : 0.0f;
}

/**
 * @brief Check if TED is locked (simplified threshold check).
 *
 * Uses Yair Linn's method: compares eye-center vs mid-symbol energy.
 * Returns true if the normalized lock metric exceeds a threshold.
 *
 * @param s TED state.
 * @param threshold Lock threshold (default ~0.4 is reasonable).
 * @return Non-zero if locked, zero otherwise.
 */
static inline int
ted_is_locked(const ted_state_t* s, float threshold) {
    if (!s || s->lock_count <= 0) {
        return 0;
    }
    float normalized = s->lock_accum / (float)s->lock_count;
    return normalized > threshold ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* DSP_TED_H */
