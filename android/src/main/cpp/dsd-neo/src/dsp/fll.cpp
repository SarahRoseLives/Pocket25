// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Frequency-Locked Loop (FLL) helpers for residual carrier correction.
 *
 * Provides NCO-based mixing and loop update routines for FM demodulation.
 * Uses high-quality sin/cos from the math library for NCO rotation.
*/

#include <complex>
#include <dsd-neo/dsp/fll.h>
#include <dsd-neo/dsp/math_utils.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const float kTwoPiF = 6.28318530717958647692f;

static inline float
clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline float
wrap_phase(float p) {
    while (p > kTwoPiF) {
        p -= kTwoPiF;
    }
    while (p < -kTwoPiF) {
        p += kTwoPiF;
    }
    return p;
}

/* Very small integrator leakage to avoid long-term windup/drift.
 * Chosen as 1/4096 per update (>>12), small enough to be inaudible
 * for FM and gentle for digital modes while providing slow decay. */

/**
 * @brief Initialize FLL state with default values.
 *
 * @param state FLL state to initialize.
 */
void
fll_init_state(fll_state_t* state) {
    state->freq = 0.0f;
    state->phase = 0.0f;
    state->prev_r = 0.0f;
    state->prev_j = 0.0f;
    state->integrator = 0.0f;
    state->prev_hist_len = 0;
    for (int i = 0; i < 64; i++) {
        state->prev_hist_r[i] = 0.0f;
        state->prev_hist_j[i] = 0.0f;
    }
}

/**
 * @brief Mix I/Q by an NCO and advance phase by freq per sample (GNU Radio style).
 *
 * Phase and frequency are in radians. Phase wraps at ±2π.
 * Uses incremental NCO (phasor rotation) to avoid per-sample sinf/cosf calls.
 *
 * @param config FLL configuration.
 * @param state  FLL state (updates phase).
 * @param x      Input/output interleaved I/Q buffer (modified in-place).
 * @param N      Length of buffer in samples (must be even).
 */
void
fll_mix_and_update(const fll_config_t* config, fll_state_t* state, float* x, int N) {
    if (!config->enabled) {
        return;
    }

    float phase = state->phase;
    const float freq = state->freq;

    /* Initial NCO phasor */
    float nco_r = cosf(phase);
    float nco_i = sinf(phase);

    /* Step phasor (computed once) */
    float step_r = cosf(freq);
    float step_i = sinf(freq);

    int sample_count = 0;
    for (int i = 0; i + 1 < N; i += 2) {
        float xr = x[i];
        float xj = x[i + 1];

        /* Rotation: y = x * conj(nco) for down-mixing */
        float yr = xr * nco_r + xj * nco_i;
        float yj = xj * nco_r - xr * nco_i;
        x[i] = yr;
        x[i + 1] = yj;

        /* Advance NCO: nco *= step (complex multiply) */
        float new_r = nco_r * step_r - nco_i * step_i;
        float new_i = nco_r * step_i + nco_i * step_r;
        nco_r = new_r;
        nco_i = new_i;

        /* Periodic renormalization to prevent drift (every 64 complex samples) */
        if (++sample_count == 64) {
            float mag = sqrtf(nco_r * nco_r + nco_i * nco_i);
            if (mag > 0.0f) {
                nco_r /= mag;
                nco_i /= mag;
            }
            sample_count = 0;
        }
    }

    /* Recover phase from final phasor for state persistence */
    state->phase = wrap_phase(atan2f(nco_i, nco_r));
}

/**
 * @brief Estimate frequency error and update FLL control (GNU Radio-style native float PI).
 *
 * Uses a phase-difference discriminator to compute average error.
 * Applies proportional and integral actions to adjust the NCO frequency.
 *
 * @param config FLL configuration (native float gains).
 * @param state  FLL state (updates freq and integrator).
 * @param x      Input interleaved I/Q buffer.
 * @param N      Length of buffer in samples (must be even).
 */
void
fll_update_error(const fll_config_t* config, fll_state_t* state, const float* x, int N) {
    if (!config->enabled) {
        return;
    }

    const float alpha = config->alpha;
    const float beta = config->beta;
    float prev_r = state->prev_r;
    float prev_j = state->prev_j;
    double err_acc = 0.0; /* accumulate in double for stability */
    int count = 0;

    for (int i = 0; i + 1 < N; i += 2) {
        float r = x[i];
        float j = x[i + 1];
        if (i > 0 || (prev_r != 0.0f || prev_j != 0.0f)) {
            /* phase delta */
            double re = (double)r * (double)prev_r + (double)j * (double)prev_j;
            double im = (double)j * (double)prev_r - (double)r * (double)prev_j;
            double e = atan2(im, re); /* radians */
            err_acc += e;
            count++;
        }
        prev_r = r;
        prev_j = j;
    }

    state->prev_r = prev_r;
    state->prev_j = prev_j;

    if (count == 0) {
        return;
    }

    float err_rad = (float)(err_acc / (double)count); /* radians */

    /* Integrator leakage: small exponential decay to avoid long-term drift.
     * Decay factor = 1 - 1/4096 ≈ 0.99976 per update. */
    const float kIntLeakFactor = 1.0f - (1.0f / 4096.0f);
    /* Frequency clamp in rad/sample: ~±0.8 rad/sample (generous for digital modes) */
    const float kFreqClamp = 0.8f;

    float i_base = state->integrator * kIntLeakFactor;
    i_base = clampf(i_base, -kFreqClamp, kFreqClamp);

    /* Deadband: ignore tiny phase errors to avoid audible low-frequency ramps.
       Keep leaked integrator so it slowly returns toward zero. */
    if (fabsf(err_rad) < config->deadband) {
        state->integrator = i_base;
        return;
    }

    /* True PI controller: u = Kp*e + I, where I accumulates beta*e */
    float p = alpha * err_rad;
    float i_term = beta * err_rad;

    /* Integrator update with anti-windup bound */
    float i_next = clampf(i_base + i_term, -kFreqClamp, kFreqClamp);

    /* Positional controller output */
    float u = p + i_next;

    /* Apply slew limit to change in frequency per update */
    float df = u - state->freq;
    df = clampf(df, -config->slew_max, config->slew_max);
    float f_new = clampf(state->freq + df, -kFreqClamp, kFreqClamp);

    state->freq = f_new;
    state->integrator = i_next;
}
