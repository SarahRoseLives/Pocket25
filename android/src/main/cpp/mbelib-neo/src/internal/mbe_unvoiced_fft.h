// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 *
 * Internal header for FFT-based unvoiced synthesis.
 * Implements JMBE Algorithms #117-126 for high-quality unvoiced audio.
 */

#ifndef MBEINT_MBE_UNVOICED_FFT_H
#define MBEINT_MBE_UNVOICED_FFT_H

#include "mbelib-neo/mbelib.h"

/* FFT size for unvoiced synthesis */
#define MBE_FFT_SIZE             256

/* Scaling coefficient for unvoiced synthesis (Algorithm #120) */
#define MBE_UNVOICED_SCALE_COEFF 146.17696f

/* 256 / (2 * PI) constant for bin frequency calculation */
#define MBE_256_OVER_2PI         (256.0f / (2.0f * 3.14159265358979323846f))

/* LCG constants for JMBE-compatible noise generation */
#define MBE_LCG_A                171.0f
#define MBE_LCG_B                11213.0f
#define MBE_LCG_M                53125.0f

/* Default LCG seed - matches JMBE's MBENoiseSequenceGenerator initial value */
#define MBE_LCG_DEFAULT_SEED     3147.0f

/* Noise buffer overlap size for continuity between frames */
#define MBE_NOISE_OVERLAP        96

/**
 * @brief Opaque FFT plan handle for unvoiced synthesis.
 */
typedef struct mbe_fft_plan mbe_fft_plan;

/**
 * @brief Allocate an FFT plan for unvoiced synthesis.
 *
 * Creates a reusable plan for 256-point real FFT operations.
 * Call mbe_fft_plan_free() when done.
 *
 * @return Allocated plan, or NULL on failure.
 */
mbe_fft_plan* mbe_fft_plan_alloc(void);

/**
 * @brief Free an FFT plan.
 *
 * @param plan Plan to free.
 */
void mbe_fft_plan_free(mbe_fft_plan* plan);

/**
 * @brief Generate LCG noise samples for unvoiced synthesis.
 *
 * Implements JMBE-compatible Linear Congruential Generator.
 * Uses x' = (171*x + 11213) mod 53125.
 *
 * @param buffer Output buffer for noise samples.
 * @param count Number of samples to generate.
 * @param seed In/out LCG state.
 */
void mbe_generate_noise_lcg(float* buffer, int count, float* seed);

/**
 * @brief Generate noise with overlap from previous frame.
 *
 * Copies overlap samples from previous frame, generates new samples,
 * and saves new overlap for next frame.
 *
 * @param buffer Output buffer of 256 samples.
 * @param seed In/out LCG state.
 * @param overlap In/out 96-sample overlap buffer.
 */
void mbe_generate_noise_with_overlap(float* buffer, float* seed, float* overlap);

/**
 * @brief Synthesize unvoiced speech using FFT method.
 *
 * Implements JMBE Algorithms #117-126:
 * - Generate noise buffer (with overlap continuity)
 * - Apply synthesis window
 * - FFT -> scale unvoiced bins -> IFFT
 * - WOLA combine with previous frame
 *
 * @param output Output buffer of 160 samples (added to existing content).
 * @param cur_mp Current frame parameters.
 * @param prev_mp Previous frame parameters (provides previousUw for WOLA).
 * @param plan FFT plan (reusable).
 */
void mbe_synthesizeUnvoicedFFT(float* output, mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_fft_plan* plan);

/**
 * @brief Synthesize unvoiced speech using a pre-generated noise buffer.
 *
 * Same as mbe_synthesizeUnvoicedFFT but uses the provided noise buffer
 * instead of generating new noise. This allows sharing the noise buffer
 * with voiced phase calculation for JMBE compatibility.
 *
 * @param output Output buffer of 160 samples (added to existing content).
 * @param cur_mp Current frame parameters.
 * @param prev_mp Previous frame parameters (provides previousUw for WOLA).
 * @param plan FFT plan (reusable).
 * @param noise_buffer Pre-generated 256-sample noise buffer.
 */
void mbe_synthesizeUnvoicedFFTWithNoise(float* output, mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_fft_plan* plan,
                                        const float* noise_buffer);

/**
 * @brief Get the 211-element synthesis window value.
 *
 * The window is defined for indices -105 to +105.
 * Returns 0 for out-of-range indices.
 *
 * @param n Sample index relative to center.
 * @return Window value at index n.
 */
float mbe_synthesisWindow(int n);

/**
 * @brief Apply WOLA (Weighted Overlap-Add) to combine frames.
 *
 * Implements Algorithm #126 to smoothly combine previous and current
 * frame outputs.
 *
 * @param output Output buffer of N samples.
 * @param prevUw Previous frame's inverse FFT output (256 samples).
 * @param currUw Current frame's inverse FFT output (256 samples).
 * @param N Frame length (typically 160).
 */
void mbe_wola_combine(float* output, const float* prevUw, const float* currUw, int N);

#endif /* MBEINT_MBE_UNVOICED_FFT_H */
