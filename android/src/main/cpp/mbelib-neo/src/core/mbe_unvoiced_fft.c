// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 *
 * FFT-based unvoiced synthesis implementation.
 * Implements JMBE Algorithms #117-126 for high-quality unvoiced audio.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mbe_compiler.h"
#include "mbe_unvoiced_fft.h"
#include "pffft.h"

/* LCG integer constants matching the float #defines in the header */
#define MBE_LCG_A_INT 171u
#define MBE_LCG_B_INT 11213u
#define MBE_LCG_M_INT 53125u

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* SIMD headers for vectorized paths */
#if defined(MBELIB_ENABLE_SIMD)
#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif
#endif

/**
 * @brief 211-element synthesis window (indices -105 to +105).
 *
 * Trapezoidal window with linear ramps at edges and flat region in center.
 * Matches JMBE specification for WOLA synthesis.
 */
static const float Ws_synthesis[211] = {
    /* Indices -105 to -56: linear ramp up from 0.0 to ~0.98 */
    0.000f, 0.020f, 0.040f, 0.060f, 0.080f, 0.100f, 0.120f, 0.140f, 0.160f, 0.180f, 0.200f, 0.220f, 0.240f, 0.260f,
    0.280f, 0.300f, 0.320f, 0.340f, 0.360f, 0.380f, 0.400f, 0.420f, 0.440f, 0.460f, 0.480f, 0.500f, 0.520f, 0.540f,
    0.560f, 0.580f, 0.600f, 0.620f, 0.640f, 0.660f, 0.680f, 0.700f, 0.720f, 0.740f, 0.760f, 0.780f, 0.800f, 0.820f,
    0.840f, 0.860f, 0.880f, 0.900f, 0.920f, 0.940f, 0.960f, 0.980f,
    /* Indices -55 to +55: flat region at 1.0 (111 values) */
    1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f,
    1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f,
    1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f,
    1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f,
    1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f,
    1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f,
    1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f,
    1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f,
    /* Indices +56 to +105: linear ramp down from ~0.98 to 0.0 */
    0.980f, 0.960f, 0.940f, 0.920f, 0.900f, 0.880f, 0.860f, 0.840f, 0.820f, 0.800f, 0.780f, 0.760f, 0.740f, 0.720f,
    0.700f, 0.680f, 0.660f, 0.640f, 0.620f, 0.600f, 0.580f, 0.560f, 0.540f, 0.520f, 0.500f, 0.480f, 0.460f, 0.440f,
    0.420f, 0.400f, 0.380f, 0.360f, 0.340f, 0.320f, 0.300f, 0.280f, 0.260f, 0.240f, 0.220f, 0.200f, 0.180f, 0.160f,
    0.140f, 0.120f, 0.100f, 0.080f, 0.060f, 0.040f, 0.020f, 0.000f};

/**
 * @brief Fast inline window lookup for hot paths.
 *
 * Directly indexes the window table without function call overhead.
 * Caller must ensure n is in [-105, +105] for valid results.
 */
static inline float
mbe_synthesisWindow_fast(int n) {
    return Ws_synthesis[n + 105];
}

/* Frame length for WOLA (always 160 samples) */
#define MBE_FRAME_LEN 160

/**
 * @brief FFT plan structure wrapping PFFFT setup and scratch buffers.
 *
 * The scratch buffers are allocated once per thread and reused across frames
 * to reduce stack traffic and improve cache locality.
 *
 * PFFFT real transform output format for N=256:
 *   output[0] = DC component (bin 0 real)
 *   output[1] = Nyquist component (bin 128 real)
 *   output[2..N-1] = bins 1 to N/2-1 as interleaved (re, im) pairs
 *
 * Float arrays are aligned to cache line boundaries (64 bytes) to improve
 * SIMD load/store performance and avoid false sharing.
 */
struct mbe_fft_plan {
    /* Per-bin scaling factors - cache line aligned for SIMD access */
    MBE_ALIGNAS(MBE_CACHE_LINE_SIZE) float dftBinScalor[MBE_FFT_SIZE / 2 + 1];

    /* Band edge indices (int arrays don't benefit much from alignment) */
    int a_min[57]; /**< Band lower bin edges */

    PFFFT_Setup* setup; /**< PFFFT setup for N=256 real transform */

    /* PFFFT-aligned scratch buffers for unvoiced synthesis (reused across frames) */
    float* Uw;     /**< Windowed noise buffer (256 floats, aligned) */
    float* Uw_fft; /**< FFT output (256 floats, aligned) */

    /* Precomputed WOLA weights for n=0..159 (avoids per-sample window lookups)
     * Cache line aligned for efficient SIMD loads in hot WOLA combine loop */
    MBE_ALIGNAS(MBE_CACHE_LINE_SIZE) float wola_w_prev[MBE_FRAME_LEN];    /**< w(n) for previous frame */
    MBE_ALIGNAS(MBE_CACHE_LINE_SIZE) float wola_w_curr[MBE_FRAME_LEN];    /**< w(n-160) for current frame */
    MBE_ALIGNAS(MBE_CACHE_LINE_SIZE) float wola_w_prev_sq[MBE_FRAME_LEN]; /**< w_prev^2 */
    MBE_ALIGNAS(MBE_CACHE_LINE_SIZE) float wola_w_curr_sq[MBE_FRAME_LEN]; /**< w_curr^2 */
    MBE_ALIGNAS(MBE_CACHE_LINE_SIZE) float wola_denom[MBE_FRAME_LEN];     /**< w_prev^2 + w_curr^2 */

    float* Uw_out; /**< IFFT output buffer (256 floats, aligned) */
    float* work;   /**< PFFFT work buffer (256 floats, aligned) */

    int b_max[57]; /**< Band upper bin edges */

    /* Index arrays for WOLA (int arrays benefit less from alignment) */
    int wola_prev_idx[MBE_FRAME_LEN]; /**< n + 128 */
    int wola_curr_idx[MBE_FRAME_LEN]; /**< n + 128 - 160 = n - 32 */
};

mbe_fft_plan*
mbe_fft_plan_alloc(void) {
    /* Allocate plan with 64-byte alignment to satisfy MBE_ALIGNAS(MBE_CACHE_LINE_SIZE)
     * requirements on struct members. PFFFT's aligned allocator uses 64-byte alignment. */
    mbe_fft_plan* plan = (mbe_fft_plan*)pffft_aligned_malloc(sizeof(mbe_fft_plan));
    if (!plan) {
        return NULL;
    }

    /* Initialize pointers to NULL for safe cleanup */
    plan->setup = NULL;
    plan->Uw = NULL;
    plan->Uw_fft = NULL;
    plan->Uw_out = NULL;
    plan->work = NULL;

    /* Create PFFFT setup for 256-point real transform */
    plan->setup = pffft_new_setup(MBE_FFT_SIZE, PFFFT_REAL);
    if (!plan->setup) {
        pffft_aligned_free(plan);
        return NULL;
    }

    /* Allocate SIMD-aligned buffers for PFFFT */
    plan->Uw = (float*)pffft_aligned_malloc(MBE_FFT_SIZE * sizeof(float));
    plan->Uw_fft = (float*)pffft_aligned_malloc(MBE_FFT_SIZE * sizeof(float));
    plan->Uw_out = (float*)pffft_aligned_malloc(MBE_FFT_SIZE * sizeof(float));
    plan->work = (float*)pffft_aligned_malloc(MBE_FFT_SIZE * sizeof(float));

    if (!plan->Uw || !plan->Uw_fft || !plan->Uw_out || !plan->work) {
        mbe_fft_plan_free(plan);
        return NULL;
    }

    /* Precompute WOLA weights for n=0..159 (N=160) */
    for (int n = 0; n < MBE_FRAME_LEN; n++) {
        /* Window positions relative to frame center */
        float w_prev = mbe_synthesisWindow(n);                 /* w(n) for previous frame */
        float w_curr = mbe_synthesisWindow(n - MBE_FRAME_LEN); /* w(n-160) for current frame */

        plan->wola_w_prev[n] = w_prev;
        plan->wola_w_curr[n] = w_curr;
        plan->wola_w_prev_sq[n] = w_prev * w_prev;
        plan->wola_w_curr_sq[n] = w_curr * w_curr;
        plan->wola_denom[n] = plan->wola_w_prev_sq[n] + plan->wola_w_curr_sq[n];

        /* Precompute buffer indices */
        plan->wola_prev_idx[n] = n + 128;
        plan->wola_curr_idx[n] = n + 128 - MBE_FRAME_LEN; /* n - 32 for N=160 */
    }

    return plan;
}

void
mbe_fft_plan_free(mbe_fft_plan* plan) {
    if (plan) {
        if (plan->setup) {
            pffft_destroy_setup(plan->setup);
        }
        if (plan->Uw) {
            pffft_aligned_free(plan->Uw);
        }
        if (plan->Uw_fft) {
            pffft_aligned_free(plan->Uw_fft);
        }
        if (plan->Uw_out) {
            pffft_aligned_free(plan->Uw_out);
        }
        if (plan->work) {
            pffft_aligned_free(plan->work);
        }
        pffft_aligned_free(plan);
    }
}

float
mbe_synthesisWindow(int n) {
    if (n < -105 || n > 105) {
        return 0.0f;
    }
    return Ws_synthesis[n + 105];
}

void
mbe_generate_noise_lcg(float* restrict buffer, int count, float* restrict seed) {
    if (MBE_UNLIKELY(!buffer || !seed)) {
        return;
    }

    /* Use integer arithmetic for the LCG to avoid expensive fmodf() calls.
     * The state is always in [0, 53124] which is exactly representable in float.
     * This optimization replaces per-sample fmodf() with integer modulo. */
    uint32_t state = (uint32_t)(*seed) % 53125u;
    for (int i = 0; i < count; i++) {
        /* Write current state to buffer BEFORE updating (preserves JMBE sequence) */
        buffer[i] = (float)state;
        state = (171u * state + 11213u) % 53125u;
    }
    *seed = (float)state;
}

void
mbe_generate_noise_with_overlap(float* restrict buffer, float* restrict seed, float* restrict overlap) {
    if (MBE_UNLIKELY(!buffer || !seed || !overlap)) {
        return;
    }

    /* Copy overlap from previous frame (first 96 samples) */
    memcpy(buffer, overlap, MBE_NOISE_OVERLAP * sizeof(float));

    /* Generate 160 new samples (256 - 96 = 160) */
    mbe_generate_noise_lcg(buffer + MBE_NOISE_OVERLAP, MBE_FFT_SIZE - MBE_NOISE_OVERLAP, seed);

    /* Save new overlap for next frame (last 96 samples) */
    memcpy(overlap, buffer + (MBE_FFT_SIZE - MBE_NOISE_OVERLAP), MBE_NOISE_OVERLAP * sizeof(float));
}

void
mbe_wola_combine(float* restrict output, const float* restrict prevUw, const float* restrict currUw, int N) {
    if (MBE_UNLIKELY(!output || !prevUw || !currUw)) {
        return;
    }

    for (int n = 0; n < N; n++) {
        /* Window positions relative to frame center
         * Indices may be outside [-105, +105], use bounds-checked version */
        float w_prev = mbe_synthesisWindow(n);     /* w(n) for previous frame */
        float w_curr = mbe_synthesisWindow(n - N); /* w(n-160) for current frame */

        /* Extract samples from buffers at correct positions
         * prevUw is centered at sample 128, currUw is also centered at 128
         * For output sample n:
         *   - prev contribution: prevUw[n + 128] if in range
         *   - curr contribution: currUw[n - 32] if in range (shifted by N-128=32)
         */
        float prev_sample = 0.0f;
        float curr_sample = 0.0f;

        int prev_idx = n + 128;
        if (MBE_LIKELY(prev_idx >= 0 && prev_idx < MBE_FFT_SIZE)) {
            prev_sample = prevUw[prev_idx];
        }

        int curr_idx = n + 128 - N; /* n - 32 for N=160 */
        if (MBE_LIKELY(curr_idx >= 0 && curr_idx < MBE_FFT_SIZE)) {
            curr_sample = currUw[curr_idx];
        }

        /* Normalized weighted overlap-add */
        float w_prev_sq = w_prev * w_prev;
        float w_curr_sq = w_curr * w_curr;
        float denom = w_prev_sq + w_curr_sq;

        if (MBE_LIKELY(denom > 1e-10f)) {
            output[n] += ((w_prev * prev_sample) + (w_curr * curr_sample)) / denom;
        }
    }
}

/**
 * @brief Optimized WOLA combine using precomputed weights.
 *
 * Uses the plan's precomputed window weights and denominator values to avoid
 * per-sample window lookups and redundant squaring operations. Includes SIMD
 * paths for SSE2 and NEON when MBELIB_ENABLE_SIMD is defined.
 *
 * @param output Output buffer of 160 samples.
 * @param prevUw Previous frame's inverse FFT output (256 samples).
 * @param currUw Current frame's inverse FFT output (256 samples).
 * @param plan FFT plan containing precomputed WOLA weights.
 */
static void
mbe_wola_combine_fast(float* restrict output, const float* restrict prevUw, const float* restrict currUw,
                      const mbe_fft_plan* restrict plan) {
    if (MBE_UNLIKELY(!output || !prevUw || !currUw || !plan)) {
        return;
    }

    const float* w_prev = plan->wola_w_prev;
    const float* w_curr = plan->wola_w_curr;
    const float* denom = plan->wola_denom;
    const int* prev_idx = plan->wola_prev_idx;
    const int* curr_idx = plan->wola_curr_idx;

    /* Buffer index bounds:
     * - prev_idx[n] = n + 128, valid when < MBE_FFT_SIZE (256), so n < 128
     * - curr_idx[n] = n - 32, valid when >= 0, so n >= 32
     * Therefore:
     *   - n=0..31: curr_idx invalid (< 0), prev_idx valid
     *   - n=32..127: both indices valid (SIMD safe)
     *   - n=128..159: prev_idx invalid (>= 256), curr_idx valid
     */

#if defined(MBELIB_ENABLE_SIMD)
#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || defined(__x86_64__)
    /* SSE2 path: process 4 samples at a time where both indices are valid */

    /* Process samples n=0..31 with scalar (curr_idx < 0) */
    for (int n = 0; n < 32; n++) {
        float prev_sample = prevUw[prev_idx[n]];
        float curr_sample = 0.0f; /* curr_idx[n] < 0 for n < 32 */
        float d = denom[n];
        if (MBE_LIKELY(d > 1e-10f)) {
            output[n] += ((w_prev[n] * prev_sample) + (w_curr[n] * curr_sample)) / d;
        }
    }

    /* Process samples n=32..127 with SIMD (both indices valid) */
    const __m128 threshold = _mm_set1_ps(1e-10f);
    for (int n = 32; n < 128; n += 4) {
        /* Load precomputed weights */
        __m128 vWprev = _mm_loadu_ps(&w_prev[n]);
        __m128 vWcurr = _mm_loadu_ps(&w_curr[n]);
        __m128 vDenom = _mm_loadu_ps(&denom[n]);

        /* Gather samples (all indices valid in this range) */
        __m128 vPrevSamp =
            _mm_set_ps(prevUw[prev_idx[n + 3]], prevUw[prev_idx[n + 2]], prevUw[prev_idx[n + 1]], prevUw[prev_idx[n]]);
        __m128 vCurrSamp =
            _mm_set_ps(currUw[curr_idx[n + 3]], currUw[curr_idx[n + 2]], currUw[curr_idx[n + 1]], currUw[curr_idx[n]]);

        /* Compute weighted samples */
        __m128 vWeightedPrev = _mm_mul_ps(vWprev, vPrevSamp);
        __m128 vWeightedCurr = _mm_mul_ps(vWcurr, vCurrSamp);
        __m128 vSum = _mm_add_ps(vWeightedPrev, vWeightedCurr);

        /* Divide by denominator (skip division for small denominators) */
        __m128 vMask = _mm_cmpgt_ps(vDenom, threshold);
        __m128 vResult = _mm_div_ps(vSum, vDenom);
        vResult = _mm_and_ps(vResult, vMask);

        /* Add to output */
        __m128 vOut = _mm_loadu_ps(&output[n]);
        vOut = _mm_add_ps(vOut, vResult);
        _mm_storeu_ps(&output[n], vOut);
    }

    /* Process samples n=128..159 with scalar (prev_idx >= MBE_FFT_SIZE) */
    for (int n = 128; n < MBE_FRAME_LEN; n++) {
        float prev_sample = 0.0f; /* prev_idx[n] >= 256 for n >= 128 */
        float curr_sample = currUw[curr_idx[n]];
        float d = denom[n];
        if (MBE_LIKELY(d > 1e-10f)) {
            output[n] += ((w_prev[n] * prev_sample) + (w_curr[n] * curr_sample)) / d;
        }
    }
    return;

#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
    /* NEON path: process 4 samples at a time where both indices are valid */

    /* Process samples n=0..31 with scalar (curr_idx < 0) */
    for (int n = 0; n < 32; n++) {
        float prev_sample = prevUw[prev_idx[n]];
        float curr_sample = 0.0f; /* curr_idx[n] < 0 for n < 32 */
        float d = denom[n];
        if (MBE_LIKELY(d > 1e-10f)) {
            output[n] += ((w_prev[n] * prev_sample) + (w_curr[n] * curr_sample)) / d;
        }
    }

    /* Process samples n=32..127 with SIMD (both indices valid) */
    const float32x4_t threshold = vdupq_n_f32(1e-10f);
    for (int n = 32; n < 128; n += 4) {
        /* Load precomputed weights */
        float32x4_t vWprev = vld1q_f32(&w_prev[n]);
        float32x4_t vWcurr = vld1q_f32(&w_curr[n]);
        float32x4_t vDenom = vld1q_f32(&denom[n]);

        /* Gather samples (all indices valid in this range) */
        float prev_samples[4] = {prevUw[prev_idx[n]], prevUw[prev_idx[n + 1]], prevUw[prev_idx[n + 2]],
                                 prevUw[prev_idx[n + 3]]};
        float curr_samples[4] = {currUw[curr_idx[n]], currUw[curr_idx[n + 1]], currUw[curr_idx[n + 2]],
                                 currUw[curr_idx[n + 3]]};
        float32x4_t vPrevSamp = vld1q_f32(prev_samples);
        float32x4_t vCurrSamp = vld1q_f32(curr_samples);

        /* Compute weighted samples */
        float32x4_t vWeightedPrev = vmulq_f32(vWprev, vPrevSamp);
        float32x4_t vWeightedCurr = vmulq_f32(vWcurr, vCurrSamp);
        float32x4_t vSum = vaddq_f32(vWeightedPrev, vWeightedCurr);

        /* Divide by denominator (NEON doesn't have direct divide, use reciprocal estimate) */
        float32x4_t vRecip = vrecpeq_f32(vDenom);
        vRecip = vmulq_f32(vRecip, vrecpsq_f32(vDenom, vRecip)); /* Newton-Raphson refinement */
        float32x4_t vResult = vmulq_f32(vSum, vRecip);

        /* Mask out results where denom <= threshold */
        uint32x4_t vMask = vcgtq_f32(vDenom, threshold);
        vResult = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(vResult), vMask));

        /* Add to output */
        float32x4_t vOut = vld1q_f32(&output[n]);
        vOut = vaddq_f32(vOut, vResult);
        vst1q_f32(&output[n], vOut);
    }

    /* Process samples n=128..159 with scalar (prev_idx >= MBE_FFT_SIZE) */
    for (int n = 128; n < MBE_FRAME_LEN; n++) {
        float prev_sample = 0.0f; /* prev_idx[n] >= 256 for n >= 128 */
        float curr_sample = currUw[curr_idx[n]];
        float d = denom[n];
        if (MBE_LIKELY(d > 1e-10f)) {
            output[n] += ((w_prev[n] * prev_sample) + (w_curr[n] * curr_sample)) / d;
        }
    }
    return;
#endif
#endif

    /* Scalar fallback with precomputed weights */
    for (int n = 0; n < MBE_FRAME_LEN; n++) {
        float prev_sample = 0.0f;
        float curr_sample = 0.0f;

        int pidx = prev_idx[n];
        if (MBE_LIKELY(pidx >= 0 && pidx < MBE_FFT_SIZE)) {
            prev_sample = prevUw[pidx];
        }

        int cidx = curr_idx[n];
        if (MBE_LIKELY(cidx >= 0 && cidx < MBE_FFT_SIZE)) {
            curr_sample = currUw[cidx];
        }

        float d = denom[n];
        if (MBE_LIKELY(d > 1e-10f)) {
            output[n] += ((w_prev[n] * prev_sample) + (w_curr[n] * curr_sample)) / d;
        }
    }
}

/**
 * @brief PFFFT bin accessor helpers.
 *
 * PFFFT ordered real transform output format for N=256:
 *   fft[0] = DC component (bin 0 real)
 *   fft[1] = Nyquist component (bin 128 real)
 *   fft[2k], fft[2k+1] = bin k (re, im) for k = 1..127
 */

/* Get real part of bin (0 <= bin <= 128) */
static inline float
pffft_bin_re(const float* fft, int bin) {
    if (bin == 0) {
        return fft[0];
    }
    if (bin == MBE_FFT_SIZE / 2) {
        return fft[1];
    }
    return fft[2u * (size_t)bin];
}

/* Get imaginary part of bin (0 <= bin <= 128) */
static inline float
pffft_bin_im(const float* fft, int bin) {
    if (bin == 0 || bin == MBE_FFT_SIZE / 2) {
        return 0.0f;
    }
    return fft[2u * (size_t)bin + 1u];
}

/* Set bin value (0 <= bin <= 128) */
static inline void
pffft_bin_set(float* fft, int bin, float re, float im) {
    if (bin == 0) {
        fft[0] = re;
    } else if (bin == MBE_FFT_SIZE / 2) {
        fft[1] = re;
    } else {
        size_t offset = 2u * (size_t)bin;
        fft[offset] = re;
        fft[offset + 1u] = im;
    }
}

/* Scale bin by factor (0 <= bin <= 128) */
static inline void
pffft_bin_scale(float* fft, int bin, float scale) {
    if (bin == 0) {
        fft[0] *= scale;
    } else if (bin == MBE_FFT_SIZE / 2) {
        fft[1] *= scale;
    } else {
        size_t offset = 2u * (size_t)bin;
        fft[offset] *= scale;
        fft[offset + 1u] *= scale;
    }
}

/**
 * @brief Compute sum of magnitude squared for a range of FFT bins.
 *
 * Calculates sum(re[i]^2 + im[i]^2) for bins from start to end (exclusive).
 * Handles PFFFT's interleaved format where DC is at [0], Nyquist at [1],
 * and bins 1-127 are at [2k, 2k+1].
 *
 * Uses SIMD when MBELIB_ENABLE_SIMD is defined and the range is suitable.
 *
 * @param fft PFFFT ordered output array (256 floats).
 * @param start First bin index (inclusive, 0 to 128).
 * @param end Last bin index (exclusive, 0 to 129).
 * @return Sum of magnitude squared values.
 */
static float
mbe_magnitude_squared_sum(const float* restrict fft, int start, int end) {
    float sum = 0.0f;

    if (end <= start) {
        return 0.0f;
    }

    /* Handle bin 0 (DC) separately if in range */
    if (start == 0) {
        sum += fft[0] * fft[0]; /* DC has no imaginary part */
        start = 1;
    }

    /* Handle bin 128 (Nyquist) separately if in range */
    int nyquist_bin = MBE_FFT_SIZE / 2; /* 128 */
    int include_nyquist = (end > nyquist_bin);
    if (include_nyquist) {
        end = nyquist_bin; /* Process bins 1-127 with SIMD, Nyquist separately */
    }

    /* Process bins 1 to end-1 (these are at positions fft[2]..fft[2*(end-1)+1]) */
    int interior_count = end - start;
    if (interior_count > 0) {
        /* Interior bins are stored as interleaved (re,im) pairs at fft[2*start] */
        const float* ptr = &fft[2u * (size_t)start];

#if defined(MBELIB_ENABLE_SIMD)
#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || defined(__x86_64__)
        /* SSE2 path: process 4 bins (8 floats) at a time */
        if (interior_count >= 4) {
            __m128 vsum = _mm_setzero_ps();
            int i;
            for (i = 0; i + 4 <= interior_count; i += 4) {
                /* Load 8 floats: [r0,i0,r1,i1] and [r2,i2,r3,i3] */
                __m128 v0 = _mm_loadu_ps(ptr);
                __m128 v1 = _mm_loadu_ps(ptr + 4);
                ptr += 8;

                /* Compute squares and accumulate */
                __m128 sq0 = _mm_mul_ps(v0, v0);
                __m128 sq1 = _mm_mul_ps(v1, v1);
                vsum = _mm_add_ps(vsum, sq0);
                vsum = _mm_add_ps(vsum, sq1);
            }

            /* Horizontal sum of vsum */
            __m128 shuf = _mm_shuffle_ps(vsum, vsum, _MM_SHUFFLE(2, 3, 0, 1));
            __m128 sums = _mm_add_ps(vsum, shuf);
            shuf = _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(0, 1, 2, 3));
            sums = _mm_add_ps(sums, shuf);
            sum += _mm_cvtss_f32(sums);

            interior_count -= i;
        }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
        /* NEON path: process 4 bins at a time */
        if (interior_count >= 4) {
            float32x4_t vsum = vdupq_n_f32(0.0f);
            int i;
            for (i = 0; i + 4 <= interior_count; i += 4) {
                /* Load 8 floats */
                float32x4_t v0 = vld1q_f32(ptr);
                float32x4_t v1 = vld1q_f32(ptr + 4);
                ptr += 8;

                /* Compute squares and accumulate */
                vsum = vmlaq_f32(vsum, v0, v0);
                vsum = vmlaq_f32(vsum, v1, v1);
            }

            /* Horizontal sum */
            float32x2_t vsum2 = vadd_f32(vget_low_f32(vsum), vget_high_f32(vsum));
            sum += vget_lane_f32(vpadd_f32(vsum2, vsum2), 0);

            interior_count -= i;
        }
#endif
#endif

        /* Scalar fallback for remaining interior bins */
        for (int i = 0; i < interior_count; i++) {
            float re = ptr[0];
            float im = ptr[1];
            sum += (re * re) + (im * im);
            ptr += 2;
        }
    }

    /* Add Nyquist bin if it was in range */
    if (include_nyquist) {
        sum += fft[1] * fft[1]; /* Nyquist has no imaginary part */
    }

    return sum;
}

void
mbe_synthesizeUnvoicedFFTWithNoise(float* restrict output, mbe_parms* restrict cur_mp, mbe_parms* restrict prev_mp,
                                   mbe_fft_plan* restrict plan, const float* restrict noise_buffer) {
    if (MBE_UNLIKELY(!output || !cur_mp || !prev_mp || !plan || !noise_buffer)) {
        return;
    }

    /* Use plan's scratch buffers */
    float* Uw = plan->Uw;
    float* Uw_fft = plan->Uw_fft;
    float* dftBinScalor = plan->dftBinScalor;
    float* Uw_out = plan->Uw_out;
    int* a_min = plan->a_min;
    int* b_max = plan->b_max;

    int L = cur_mp->L;
    float w0 = cur_mp->w0;

    /* Initialize scalors to zero (voiced bands stay zeroed) */
    memset(dftBinScalor, 0, (MBE_FFT_SIZE / 2 + 1) * sizeof(float));

    /* Copy pre-generated noise buffer and apply synthesis window (Algorithm #118 prep)
     * Window is centered at sample 128, indices go from -128 to +127
     * Use fast inline lookup - all indices are in valid range [-105, 105] after offset
     */
    for (int i = 0; i < MBE_FFT_SIZE; i++) {
        int win_idx = i - 128;
        /* Indices outside [-105, +105] get zero from the window table edges */
        float w = (win_idx >= -105 && win_idx <= 105) ? mbe_synthesisWindow_fast(win_idx) : 0.0f;
        Uw[i] = noise_buffer[i] * w;
    }

    /* Algorithm #118: 256-point real FFT using PFFFT */
    pffft_transform_ordered(plan->setup, Uw, Uw_fft, plan->work, PFFFT_FORWARD);

    /* Algorithms #122-123: Calculate frequency band edges for each harmonic */
    float multiplier = MBE_256_OVER_2PI * w0;

    for (int l = 1; l <= L; l++) {
        a_min[l] = (int)ceilf((l - 0.5f) * multiplier);
        b_max[l] = (int)ceilf((l + 0.5f) * multiplier);
        /* Clamp to valid bin range */
        if (a_min[l] < 0) {
            a_min[l] = 0;
        }
        if (b_max[l] > MBE_FFT_SIZE / 2) {
            b_max[l] = MBE_FFT_SIZE / 2;
        }
    }

    /* Algorithm #120: Calculate band-level scaling for unvoiced bands
     * Uses SIMD-optimized magnitude accumulation when available */
    for (int l = 1; l <= L; l++) {
        if (cur_mp->Vl[l] == 0) { /* Unvoiced band */
            int bin_count = b_max[l] - a_min[l];
            float numerator = mbe_magnitude_squared_sum(Uw_fft, a_min[l], b_max[l]);

            if (bin_count > 0 && numerator > 1e-10f) {
                float denominator = (float)bin_count;
                float scalor = MBE_UNVOICED_SCALE_COEFF * cur_mp->Ml[l] / sqrtf(numerator / denominator);

                /* Apply scaling factor to all bins in this band */
                for (int bin = a_min[l]; bin < b_max[l]; bin++) {
                    dftBinScalor[bin] = scalor;
                }
            }
        }
        /* Voiced bands: scalor remains 0, effectively zeroing those bins */
    }

    /* Algorithms #119, #120, #124: Apply scaling to FFT bins
     * Scale each bin using PFFFT's interleaved format.
     * Voiced bins get scaled by 0 (zeroed), unvoiced bins get proper scaling.
     */
    for (int bin = 0; bin <= MBE_FFT_SIZE / 2; bin++) {
        pffft_bin_scale(Uw_fft, bin, dftBinScalor[bin]);
    }

    /* Algorithm #125: Inverse FFT using PFFFT */
    pffft_transform_ordered(plan->setup, Uw_fft, Uw_out, plan->work, PFFFT_BACKWARD);

    /* Normalize IFFT output (PFFFT doesn't normalize: BACKWARD(FORWARD(x)) = N*x)
     * Uses SIMD when available for faster scaling */
    {
        const float scale = 1.0f / (float)MBE_FFT_SIZE;
        int i = 0;
#if defined(MBELIB_ENABLE_SIMD)
#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || defined(__x86_64__)
        __m128 vscale = _mm_set1_ps(scale);
        for (; i + 4 <= MBE_FFT_SIZE; i += 4) {
            __m128 v = _mm_loadu_ps(&Uw_out[i]);
            v = _mm_mul_ps(v, vscale);
            _mm_storeu_ps(&Uw_out[i], v);
        }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
        float32x4_t vscale = vdupq_n_f32(scale);
        for (; i + 4 <= MBE_FFT_SIZE; i += 4) {
            float32x4_t v = vld1q_f32(&Uw_out[i]);
            v = vmulq_f32(v, vscale);
            vst1q_f32(&Uw_out[i], v);
        }
#endif
#endif
        /* Scalar fallback for remaining samples */
        for (; i < MBE_FFT_SIZE; i++) {
            Uw_out[i] *= scale;
        }
    }

    /* Algorithm #126: WOLA combine with previous frame (using precomputed weights) */
    mbe_wola_combine_fast(output, prev_mp->previousUw, Uw_out, plan);

    /* Save current output for next frame's WOLA */
    memcpy(cur_mp->previousUw, Uw_out, MBE_FFT_SIZE * sizeof(float));
}

void
mbe_synthesizeUnvoicedFFT(float* restrict output, mbe_parms* restrict cur_mp, mbe_parms* restrict prev_mp,
                          mbe_fft_plan* restrict plan) {
    if (MBE_UNLIKELY(!output || !cur_mp || !prev_mp || !plan)) {
        return;
    }

    /* Algorithm #117: Generate 256 white noise samples with LCG and overlap */
    float noise_buffer[MBE_FFT_SIZE];
    mbe_generate_noise_with_overlap(noise_buffer, &cur_mp->noiseSeed, cur_mp->noiseOverlap);

    /* Use the shared implementation */
    mbe_synthesizeUnvoicedFFTWithNoise(output, cur_mp, prev_mp, plan, noise_buffer);
}
