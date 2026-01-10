// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief AVX2+FMA implementations of SIMD FIR filter functions.
 *
 * Compiled with -mavx2 -mfma (GCC/Clang) or /arch:AVX2 (MSVC).
 * Processes 8 floats at a time using 256-bit YMM registers.
 * Uses FMA (fused multiply-add) for efficiency.
 */

#include <cstring>
#include <immintrin.h> /* AVX2 + FMA */
#include <vector>

/* Thread-local scratch buffers to avoid per-call allocation */
static thread_local std::vector<float> tls_scratch_iq;
static thread_local std::vector<float> tls_scratch_real;

/**
 * AVX2+FMA complex symmetric FIR filter (no decimation).
 * Processes 4 complex samples (8 floats) at a time.
 * Uses pre-concatenated scratch buffer to eliminate branching in hot loop.
 */
extern "C" void
simd_fir_complex_apply_avx2(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
                            int taps_len) {
    if (taps_len < 3 || (taps_len & 1) == 0 || in_len < 2) {
        return;
    }

    const int N = in_len >> 1; /* complex samples */
    const int hist_len = taps_len - 1;
    const int center = (taps_len - 1) >> 1;
    const int total_len = hist_len + N;
    const int pad = center + 4;                    /* for n+3+d lookups */
    const int scratch_len = (total_len + pad) * 2; /* *2 for complex (I, Q) */

    /* Resize thread-local buffer if needed (amortized O(1)) */
    if (tls_scratch_iq.size() < (size_t)scratch_len) {
        tls_scratch_iq.resize((size_t)scratch_len);
    }
    float* scratch = tls_scratch_iq.data();

    /* Copy history (interleave from split buffers) */
    for (int k = 0; k < hist_len; k++) {
        const size_t kk = (size_t)k;
        scratch[2 * kk] = hist_i[k];
        scratch[2 * kk + 1] = hist_q[k];
    }

    /* Copy input (already interleaved) */
    std::memcpy(scratch + (size_t)hist_len * 2, in, (size_t)N * 2 * sizeof(float));

    /* Tail padding to preserve lastI/lastQ behavior */
    float lastI = (N > 0) ? in[(N - 1) << 1] : 0.0f;
    float lastQ = (N > 0) ? in[((N - 1) << 1) + 1] : 0.0f;
    for (int k = 0; k < pad; k++) {
        const size_t kk = (size_t)hist_len + (size_t)N + (size_t)k;
        scratch[2 * kk] = lastI;
        scratch[2 * kk + 1] = lastQ;
    }

    /* Branch-free sample access (index is always valid due to padding) */
    auto get_iq = [&](int idx, float& xi, float& xq) {
        const size_t ii = (size_t)idx;
        xi = scratch[2 * ii];
        xq = scratch[2 * ii + 1];
    };

    /* Process 4 complex samples at a time (8 floats) */
    int n = 0;
    for (; n + 3 < N; n += 4) {
        __m256 acc = _mm256_setzero_ps(); /* [I0, Q0, I1, Q1, I2, Q2, I3, Q3] */

        /* Center tap */
        float cc = taps[center];
        __m256 tap_c = _mm256_set1_ps(cc);

        float ci0, cq0, ci1, cq1, ci2, cq2, ci3, cq3;
        get_iq(hist_len + n, ci0, cq0);
        get_iq(hist_len + n + 1, ci1, cq1);
        get_iq(hist_len + n + 2, ci2, cq2);
        get_iq(hist_len + n + 3, ci3, cq3);
        __m256 center_val = _mm256_set_ps(cq3, ci3, cq2, ci2, cq1, ci1, cq0, ci0);
        acc = _mm256_fmadd_ps(tap_c, center_val, acc);

        /* Symmetric pairs */
        for (int k = 0; k < center; k++) {
            float ce = taps[k];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - k;
            __m256 tap_e = _mm256_set1_ps(ce);

            float xmI0, xmQ0, xpI0, xpQ0;
            float xmI1, xmQ1, xpI1, xpQ1;
            float xmI2, xmQ2, xpI2, xpQ2;
            float xmI3, xmQ3, xpI3, xpQ3;

            get_iq(hist_len + n - d, xmI0, xmQ0);
            get_iq(hist_len + n + d, xpI0, xpQ0);
            get_iq(hist_len + n + 1 - d, xmI1, xmQ1);
            get_iq(hist_len + n + 1 + d, xpI1, xpQ1);
            get_iq(hist_len + n + 2 - d, xmI2, xmQ2);
            get_iq(hist_len + n + 2 + d, xpI2, xpQ2);
            get_iq(hist_len + n + 3 - d, xmI3, xmQ3);
            get_iq(hist_len + n + 3 + d, xpI3, xpQ3);

            __m256 sum_m = _mm256_set_ps(xmQ3, xmI3, xmQ2, xmI2, xmQ1, xmI1, xmQ0, xmI0);
            __m256 sum_p = _mm256_set_ps(xpQ3, xpI3, xpQ2, xpI2, xpQ1, xpI1, xpQ0, xpI0);
            __m256 sum = _mm256_add_ps(sum_m, sum_p);
            acc = _mm256_fmadd_ps(tap_e, sum, acc);
        }

        _mm256_storeu_ps(out + (n << 1), acc);
    }

    /* Scalar epilogue for remaining samples */
    for (; n < N; n++) {
        int center_idx = hist_len + n;
        float accI = 0.0f;
        float accQ = 0.0f;

        float ci, cq;
        get_iq(center_idx, ci, cq);
        float cc = taps[center];
        accI += cc * ci;
        accQ += cc * cq;

        for (int k = 0; k < center; k++) {
            float ce = taps[k];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - k;
            float xmI, xmQ, xpI, xpQ;
            get_iq(center_idx - d, xmI, xmQ);
            get_iq(center_idx + d, xpI, xpQ);
            accI += ce * (xmI + xpI);
            accQ += ce * (xmQ + xpQ);
        }

        out[n << 1] = accI;
        out[(n << 1) + 1] = accQ;
    }

    /* Update history: read from original `in` buffer, not scratch */
    if (N >= hist_len) {
        for (int k = 0; k < hist_len; k++) {
            int rel = N - hist_len + k;
            hist_i[k] = in[rel << 1];
            hist_q[k] = in[(rel << 1) + 1];
        }
    } else {
        int need = hist_len - N;
        if (need > 0) {
            std::memmove(hist_i, hist_i + (hist_len - need), (size_t)need * sizeof(float));
            std::memmove(hist_q, hist_q + (hist_len - need), (size_t)need * sizeof(float));
        }
        for (int k = 0; k < N; k++) {
            hist_i[need + k] = in[k << 1];
            hist_q[need + k] = in[(k << 1) + 1];
        }
    }

    _mm256_zeroupper(); /* Avoid AVX-SSE transition penalty */
}

/**
 * AVX2+FMA complex half-band decimator by 2.
 * Processes 4 output samples (8 floats) at a time.
 * Uses pre-concatenated scratch buffer to eliminate branching in hot loop.
 */
extern "C" int
simd_hb_decim2_complex_avx2(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
                            int taps_len) {
    if (taps_len < 3 || (taps_len & 1) == 0) {
        return 0;
    }

    int ch_len = in_len >> 1;
    int out_ch_len = ch_len >> 1;
    if (out_ch_len <= 0) {
        return 0;
    }

    const int center = (taps_len - 1) >> 1;
    const int left_len = taps_len - 1;
    const int total_len = left_len + ch_len;
    const int pad = center + 1;                    /* stride-2 centers, 4-output vectorization */
    const int scratch_len = (total_len + pad) * 2; /* *2 for complex (I, Q) */

    /* Resize thread-local buffer if needed (amortized O(1)) */
    if (tls_scratch_iq.size() < (size_t)scratch_len) {
        tls_scratch_iq.resize((size_t)scratch_len);
    }
    float* scratch = tls_scratch_iq.data();

    /* Copy history (interleave from split buffers) */
    for (int k = 0; k < left_len; k++) {
        const size_t kk = (size_t)k;
        scratch[2 * kk] = hist_i[k];
        scratch[2 * kk + 1] = hist_q[k];
    }

    /* Copy input (already interleaved) */
    std::memcpy(scratch + (size_t)left_len * 2, in, (size_t)ch_len * 2 * sizeof(float));

    /* Tail padding to preserve lastI/lastQ behavior */
    float lastI = (ch_len > 0) ? in[in_len - 2] : 0.0f;
    float lastQ = (ch_len > 0) ? in[in_len - 1] : 0.0f;
    for (int k = 0; k < pad; k++) {
        const size_t kk = (size_t)left_len + (size_t)ch_len + (size_t)k;
        scratch[2 * kk] = lastI;
        scratch[2 * kk + 1] = lastQ;
    }

    /* Branch-free sample access (index is always valid due to padding) */
    auto get_iq = [&](int idx, float& xi, float& xq) {
        const size_t ii = (size_t)idx;
        xi = scratch[2 * ii];
        xq = scratch[2 * ii + 1];
    };

    /* Process 4 output samples at a time */
    int n = 0;
    for (; n + 3 < out_ch_len; n += 4) {
        __m256 acc = _mm256_setzero_ps();

        float cc = taps[center];
        __m256 tap_c = _mm256_set1_ps(cc);

        /* Center indices for 4 outputs */
        int ci0 = left_len + (n << 1);
        int ci1 = left_len + ((n + 1) << 1);
        int ci2 = left_len + ((n + 2) << 1);
        int ci3 = left_len + ((n + 3) << 1);

        float c0i, c0q, c1i, c1q, c2i, c2q, c3i, c3q;
        get_iq(ci0, c0i, c0q);
        get_iq(ci1, c1i, c1q);
        get_iq(ci2, c2i, c2q);
        get_iq(ci3, c3i, c3q);
        __m256 center_val = _mm256_set_ps(c3q, c3i, c2q, c2i, c1q, c1i, c0q, c0i);
        acc = _mm256_fmadd_ps(tap_c, center_val, acc);

        /* Half-band: only even tap indices */
        for (int e = 0; e < center; e += 2) {
            float ce = taps[e];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - e;
            __m256 tap_e = _mm256_set1_ps(ce);

            float xmI0, xmQ0, xpI0, xpQ0;
            float xmI1, xmQ1, xpI1, xpQ1;
            float xmI2, xmQ2, xpI2, xpQ2;
            float xmI3, xmQ3, xpI3, xpQ3;

            get_iq(ci0 - d, xmI0, xmQ0);
            get_iq(ci0 + d, xpI0, xpQ0);
            get_iq(ci1 - d, xmI1, xmQ1);
            get_iq(ci1 + d, xpI1, xpQ1);
            get_iq(ci2 - d, xmI2, xmQ2);
            get_iq(ci2 + d, xpI2, xpQ2);
            get_iq(ci3 - d, xmI3, xmQ3);
            get_iq(ci3 + d, xpI3, xpQ3);

            __m256 sum_m = _mm256_set_ps(xmQ3, xmI3, xmQ2, xmI2, xmQ1, xmI1, xmQ0, xmI0);
            __m256 sum_p = _mm256_set_ps(xpQ3, xpI3, xpQ2, xpI2, xpQ1, xpI1, xpQ0, xpI0);
            __m256 sum = _mm256_add_ps(sum_m, sum_p);
            acc = _mm256_fmadd_ps(tap_e, sum, acc);
        }

        _mm256_storeu_ps(out + (n << 1), acc);
    }

    /* Scalar epilogue */
    for (; n < out_ch_len; n++) {
        int center_idx = left_len + (n << 1);
        float accI = 0.0f;
        float accQ = 0.0f;

        float ci, cq;
        get_iq(center_idx, ci, cq);
        accI += taps[center] * ci;
        accQ += taps[center] * cq;

        for (int e = 0; e < center; e += 2) {
            float ce = taps[e];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - e;
            float xmI, xmQ, xpI, xpQ;
            get_iq(center_idx - d, xmI, xmQ);
            get_iq(center_idx + d, xpI, xpQ);
            accI += ce * (xmI + xpI);
            accQ += ce * (xmQ + xpQ);
        }

        out[n << 1] = accI;
        out[(n << 1) + 1] = accQ;
    }

    /* Update history: read from original `in` buffer, not scratch */
    if (ch_len >= left_len) {
        int start = ch_len - left_len;
        for (int k = 0; k < left_len; k++) {
            int rel = start + k;
            hist_i[k] = in[rel << 1];
            hist_q[k] = in[(rel << 1) + 1];
        }
    } else {
        for (int k = 0; k < left_len; k++) {
            if (k < ch_len) {
                hist_i[k] = in[k << 1];
                hist_q[k] = in[(k << 1) + 1];
            } else {
                hist_i[k] = 0.0f;
                hist_q[k] = 0.0f;
            }
        }
    }

    _mm256_zeroupper();
    return out_ch_len << 1;
}

/**
 * AVX2+FMA real half-band decimator by 2.
 * Processes 8 output samples at a time.
 * Uses pre-concatenated scratch buffer to eliminate branching in hot loop.
 */
extern "C" int
simd_hb_decim2_real_avx2(const float* in, int in_len, float* out, float* hist, const float* taps, int taps_len) {
    if (taps_len < 3 || (taps_len & 1) == 0) {
        return 0;
    }

    const int hist_len = taps_len - 1;
    const int center = (taps_len - 1) >> 1;
    int out_len = in_len >> 1;
    if (out_len <= 0) {
        return 0;
    }

    const int total_len = hist_len + in_len;
    const int pad = center + 1; /* stride-2 centers, 8-output vectorization */
    const int scratch_len = total_len + pad;

    /* Resize thread-local buffer if needed (amortized O(1)) */
    if (tls_scratch_real.size() < (size_t)scratch_len) {
        tls_scratch_real.resize((size_t)scratch_len);
    }
    float* scratch = tls_scratch_real.data();

    /* Copy history */
    std::memcpy(scratch, hist, (size_t)hist_len * sizeof(float));

    /* Copy input */
    std::memcpy(scratch + hist_len, in, (size_t)in_len * sizeof(float));

    /* Tail padding to preserve last sample behavior */
    float last = (in_len > 0) ? in[in_len - 1] : 0.0f;
    for (int k = 0; k < pad; k++) {
        scratch[hist_len + in_len + k] = last;
    }

    /* Branch-free sample access (index is always valid due to padding) */
    auto get_sample = [&](int idx) -> float { return scratch[idx]; };

    /* Process 8 output samples at a time */
    int n = 0;
    for (; n + 7 < out_len; n += 8) {
        __m256 acc = _mm256_setzero_ps();

        float cc = taps[center];
        __m256 tap_c = _mm256_set1_ps(cc);

        /* Center indices for 8 outputs */
        int ci0 = hist_len + (n << 1);
        int ci1 = hist_len + ((n + 1) << 1);
        int ci2 = hist_len + ((n + 2) << 1);
        int ci3 = hist_len + ((n + 3) << 1);
        int ci4 = hist_len + ((n + 4) << 1);
        int ci5 = hist_len + ((n + 5) << 1);
        int ci6 = hist_len + ((n + 6) << 1);
        int ci7 = hist_len + ((n + 7) << 1);

        __m256 center_val = _mm256_set_ps(get_sample(ci7), get_sample(ci6), get_sample(ci5), get_sample(ci4),
                                          get_sample(ci3), get_sample(ci2), get_sample(ci1), get_sample(ci0));
        acc = _mm256_fmadd_ps(tap_c, center_val, acc);

        /* Half-band: only even indices */
        for (int e = 0; e < center; e += 2) {
            float ce = taps[e];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - e;
            __m256 tap_e = _mm256_set1_ps(ce);

            __m256 sum_m =
                _mm256_set_ps(get_sample(ci7 - d), get_sample(ci6 - d), get_sample(ci5 - d), get_sample(ci4 - d),
                              get_sample(ci3 - d), get_sample(ci2 - d), get_sample(ci1 - d), get_sample(ci0 - d));
            __m256 sum_p =
                _mm256_set_ps(get_sample(ci7 + d), get_sample(ci6 + d), get_sample(ci5 + d), get_sample(ci4 + d),
                              get_sample(ci3 + d), get_sample(ci2 + d), get_sample(ci1 + d), get_sample(ci0 + d));
            __m256 sum = _mm256_add_ps(sum_m, sum_p);
            acc = _mm256_fmadd_ps(tap_e, sum, acc);
        }

        _mm256_storeu_ps(out + n, acc);
    }

    /* Scalar epilogue */
    for (; n < out_len; n++) {
        int center_idx = hist_len + (n << 1);
        float acc = 0.0f;

        acc += taps[center] * get_sample(center_idx);

        for (int e = 0; e < center; e += 2) {
            float ce = taps[e];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - e;
            acc += ce * (get_sample(center_idx - d) + get_sample(center_idx + d));
        }

        out[n] = acc;
    }

    /* Update history: read from original `in` buffer, not scratch */
    if (in_len >= hist_len) {
        std::memcpy(hist, in + (in_len - hist_len), (size_t)hist_len * sizeof(float));
    } else {
        int need = hist_len - in_len;
        if (need > 0) {
            std::memmove(hist, hist + in_len, (size_t)need * sizeof(float));
        }
        std::memcpy(hist + need, in, (size_t)in_len * sizeof(float));
    }

    _mm256_zeroupper();
    return out_len;
}
