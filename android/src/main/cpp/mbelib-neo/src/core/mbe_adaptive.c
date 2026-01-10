// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 *
 * Adaptive smoothing implementation.
 * Implements JMBE Algorithms #111-116 for error-based audio quality improvement.
 */

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "mbe_adaptive.h"
#include "mbe_compiler.h"
#include "mbe_math.h"
#include "mbelib-neo/mbelib.h"

/* Thread-local storage for comfort noise RNG to avoid cross-thread interference.
 * JMBE uses per-synthesizer Random instances; we use thread-local state instead. */
static MBE_THREAD_LOCAL uint32_t mbe_comfort_noise_seed = 0x12345678u;

/**
 * @brief Check if adaptive smoothing is required based on error rates.
 *
 * Smoothing is required when error rate exceeds 1.25% or total errors exceed 4.
 *
 * @param mp Parameter set to check.
 * @return Non-zero if smoothing should be applied.
 */
int
mbe_requiresAdaptiveSmoothing(const mbe_parms* mp) {
    if (MBE_UNLIKELY(!mp)) {
        return 0;
    }
    return (mp->errorRate > MBE_ERROR_THRESHOLD_ENTRY) || (mp->errorCountTotal > 4);
}

/**
 * @brief Check if frame should be muted due to excessive errors.
 *
 * Uses the codec-specific muting threshold stored in mp->mutingThreshold.
 * IMBE uses 8.75% (0.0875), AMBE uses 9.6% (0.096).
 *
 * @param mp Parameter set to check.
 * @return Non-zero if frame should be muted.
 */
int
mbe_requiresMuting(const mbe_parms* mp) {
    if (MBE_UNLIKELY(!mp)) {
        return 0;
    }
    return mp->errorRate > mp->mutingThreshold;
}

/**
 * @brief Check if max repeat threshold has been exceeded.
 *
 * @param mp Parameter set to check.
 * @return Non-zero if repeatCount >= MBE_MAX_FRAME_REPEATS.
 */
int
mbe_isMaxFrameRepeat(const mbe_parms* mp) {
    if (MBE_UNLIKELY(!mp)) {
        return 0;
    }
    return mp->repeatCount >= MBE_MAX_FRAME_REPEATS;
}

/**
 * @brief Generate comfort noise for muted frames (float version).
 *
 * Generates low-level Gaussian white noise to fill gaps during frame muting.
 * Uses Box-Muller transform for JMBE-compatible Gaussian distribution.
 *
 * @param aout_buf Output buffer of 160 float samples.
 */
void
mbe_synthesizeComfortNoisef(float* aout_buf) {
    if (MBE_UNLIKELY(!aout_buf)) {
        return;
    }

    /* JMBE-compatible Gaussian noise using Box-Muller transform
     * JMBE uses Java's Random.nextGaussian() with gain of 0.003 */
    const float gain = 0.003f * 32767.0f; /* ~98.3 peak amplitude */

    for (int i = 0; i < 160; i += 2) {
        /* Generate two uniform random numbers in (0, 1) using thread-local xorshift32 */
        uint32_t x = mbe_comfort_noise_seed;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        mbe_comfort_noise_seed = x ? x : 0x6d25357bu;
        float u1 = ((float)(mbe_comfort_noise_seed & 0xFFFFFF) + 1.0f) / 16777217.0f; /* (0, 1) to avoid log(0) */

        x = mbe_comfort_noise_seed;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        mbe_comfort_noise_seed = x ? x : 0x6d25357bu;
        float u2 = (float)(mbe_comfort_noise_seed & 0xFFFFFF) / 16777216.0f; /* [0, 1) */

        /* Box-Muller transform: convert uniform to Gaussian N(0,1) */
        float r = sqrtf(-2.0f * logf(u1));
        float theta = 2.0f * (float)M_PI * u2;
        float s, c;
        mbe_sincosf(theta, &s, &c);
        float z0 = r * c;
        float z1 = r * s;

        /* Scale and store */
        aout_buf[i] = z0 * gain;
        if (i + 1 < 160) {
            aout_buf[i + 1] = z1 * gain;
        }
    }
}

/**
 * @brief Generate comfort noise for muted frames (16-bit version).
 *
 * @param aout_buf Output buffer of 160 16-bit samples.
 */
void
mbe_synthesizeComfortNoise(short* aout_buf) {
    if (MBE_UNLIKELY(!aout_buf)) {
        return;
    }

    float float_buf[160];
    mbe_synthesizeComfortNoisef(float_buf);

    /* Convert to 16-bit with clipping */
    for (int i = 0; i < 160; i++) {
        float sample = float_buf[i];
        if (sample > 32760.0f) {
            sample = 32760.0f;
        } else if (sample < -32760.0f) {
            sample = -32760.0f;
        }
        aout_buf[i] = (short)sample;
    }
}

/**
 * @brief Apply adaptive smoothing to parameters based on error rates.
 *
 * Implements JMBE Algorithms #111-116:
 * - Algorithm #111: Local energy tracking with IIR smoothing
 * - Algorithm #112: Adaptive threshold calculation
 * - Algorithm #113: Apply threshold to voicing decisions
 * - Algorithm #114: Calculate amplitude measure
 * - Algorithm #115: Calculate amplitude threshold
 * - Algorithm #116: Scale enhanced spectral amplitudes
 *
 * @param cur_mp Current frame parameters (modified in-place).
 * @param prev_mp Previous frame parameters (for local energy).
 */
void
mbe_applyAdaptiveSmoothing(mbe_parms* cur_mp, const mbe_parms* prev_mp) {
    if (MBE_UNLIKELY(!cur_mp || !prev_mp)) {
        return;
    }

    float* M = cur_mp->Ml;
    int* V = cur_mp->Vl;
    int L = cur_mp->L;
    float errorRate = cur_mp->errorRate;
    int errorTotal = cur_mp->errorCountTotal;
    int errorCount4 = cur_mp->errorCount4;

    /* Algorithm #111: Calculate local energy with IIR smoothing */
    float RM0 = 0.0f;
    for (int l = 1; l <= L; l++) {
        RM0 += M[l] * M[l];
    }

    float prevEnergy = prev_mp->localEnergy;
    if (prevEnergy < MBE_MIN_LOCAL_ENERGY) {
        prevEnergy = MBE_DEFAULT_LOCAL_ENERGY;
    }

    cur_mp->localEnergy = MBE_ENERGY_SMOOTH_ALPHA * prevEnergy + MBE_ENERGY_SMOOTH_BETA * RM0;
    if (cur_mp->localEnergy < MBE_MIN_LOCAL_ENERGY) {
        cur_mp->localEnergy = MBE_MIN_LOCAL_ENERGY;
    }

    /* Check if smoothing is required - usually not needed during clean reception */
    if (MBE_LIKELY(errorRate <= MBE_ERROR_THRESHOLD_ENTRY && errorTotal <= 4)) {
        cur_mp->amplitudeThreshold = MBE_DEFAULT_AMPLITUDE_THRESHOLD;
        return; /* No smoothing needed */
    }

    /* Algorithm #112: Calculate adaptive threshold VM */
    float VM;
    if (errorRate <= MBE_ERROR_THRESHOLD_LOW && errorTotal <= 4) {
        VM = FLT_MAX; /* No smoothing at very low error rates */
    } else {
        /* x^(3/8) = (x^(1/8))^3, where x^(1/8) = sqrtf(sqrtf(sqrtf(x)))
         * Faster than powf() and maintains adequate precision for adaptive smoothing */
        float x8 = sqrtf(sqrtf(sqrtf(cur_mp->localEnergy)));
        float energy = x8 * x8 * x8;
        if (errorRate <= MBE_ERROR_THRESHOLD_ENTRY && errorCount4 == 0) {
            /* Formula 1: exponential decay based on error rate */
            VM = (MBE_ADAPTIVE_GAIN * energy) / expf(MBE_ADAPTIVE_EXPONENT * errorRate);
        } else {
            /* Formula 2: simple scaling for higher error conditions */
            VM = MBE_ADAPTIVE_ALT * energy;
        }
    }

    /* Algorithm #113: Apply threshold to voicing decisions */
    for (int l = 1; l <= L; l++) {
        if (M[l] > VM) {
            V[l] = 1; /* Force voiced when amplitude exceeds threshold */
        }
    }

    /* Algorithm #114: Calculate amplitude measure */
    float Am = 0.0f;
    for (int l = 1; l <= L; l++) {
        Am += M[l];
    }

    /* Algorithm #115: Calculate amplitude threshold */
    int Tm;
    int prevThreshold = prev_mp->amplitudeThreshold;
    if (prevThreshold <= 0) {
        prevThreshold = MBE_DEFAULT_AMPLITUDE_THRESHOLD;
    }

    if (errorRate <= MBE_ERROR_THRESHOLD_LOW && errorTotal <= 6) {
        Tm = MBE_DEFAULT_AMPLITUDE_THRESHOLD;
    } else {
        Tm = MBE_AMPLITUDE_BASE - (MBE_AMPLITUDE_PENALTY_PER_ERROR * errorTotal) + prevThreshold;
        if (Tm < 0) {
            Tm = 0;
        }
    }
    cur_mp->amplitudeThreshold = Tm;

    /* Algorithm #116: Scale enhanced spectral amplitudes if exceeded */
    if (Am > (float)Tm && Am > 0.0f) {
        float scale = (float)Tm / Am;
        for (int l = 1; l <= L; l++) {
            M[l] *= scale;
        }
    }
}
