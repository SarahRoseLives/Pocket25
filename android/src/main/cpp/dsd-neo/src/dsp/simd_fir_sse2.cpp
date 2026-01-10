// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief SSE2 implementations of SIMD FIR filter functions.
 *
 * Compiled with -msse2 flag. Processes 4 floats at a time using 128-bit XMM
 * registers. Uses scalar epilogue for remaining samples.
 */

#include <cstring>
#include <emmintrin.h> /* SSE2 */

/**
 * SSE2 complex symmetric FIR filter (no decimation).
 * Processes 2 complex samples (4 floats) at a time.
 */
extern "C" void
simd_fir_complex_apply_sse2(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
                            int taps_len) {
    if (taps_len < 3 || (taps_len & 1) == 0 || in_len < 2) {
        return;
    }

    const int N = in_len >> 1; /* complex samples */
    const int hist_len = taps_len - 1;
    const int center = (taps_len - 1) >> 1;

    float lastI = (N > 0) ? in[(N - 1) << 1] : 0.0f;
    float lastQ = (N > 0) ? in[((N - 1) << 1) + 1] : 0.0f;

    /* Lambda to fetch sample from history or input */
    auto get_iq = [&](int src_idx, float& xi, float& xq) {
        if (src_idx < hist_len) {
            xi = hist_i[src_idx];
            xq = hist_q[src_idx];
        } else {
            int rel = src_idx - hist_len;
            if (rel < N) {
                xi = in[rel << 1];
                xq = in[(rel << 1) + 1];
            } else {
                xi = lastI;
                xq = lastQ;
            }
        }
    };

    /* Process 2 complex samples at a time (4 floats) */
    int n = 0;
    for (; n + 1 < N; n += 2) {
        __m128 acc = _mm_setzero_ps(); /* [I0, Q0, I1, Q1] */

        /* Center tap for both samples */
        float cc = taps[center];
        __m128 tap_c = _mm_set1_ps(cc);

        float ci0, cq0, ci1, cq1;
        get_iq(hist_len + n, ci0, cq0);
        get_iq(hist_len + n + 1, ci1, cq1);
        __m128 center_val = _mm_set_ps(cq1, ci1, cq0, ci0);
        acc = _mm_add_ps(acc, _mm_mul_ps(tap_c, center_val));

        /* Symmetric pairs */
        for (int k = 0; k < center; k++) {
            float ce = taps[k];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - k;
            __m128 tap_e = _mm_set1_ps(ce);

            /* Sample n: center_idx = hist_len + n */
            float xmI0, xmQ0, xpI0, xpQ0;
            get_iq(hist_len + n - d, xmI0, xmQ0);
            get_iq(hist_len + n + d, xpI0, xpQ0);

            /* Sample n+1: center_idx = hist_len + n + 1 */
            float xmI1, xmQ1, xpI1, xpQ1;
            get_iq(hist_len + n + 1 - d, xmI1, xmQ1);
            get_iq(hist_len + n + 1 + d, xpI1, xpQ1);

            __m128 sum_m = _mm_set_ps(xmQ1, xmI1, xmQ0, xmI0);
            __m128 sum_p = _mm_set_ps(xpQ1, xpI1, xpQ0, xpI0);
            __m128 sum = _mm_add_ps(sum_m, sum_p);
            acc = _mm_add_ps(acc, _mm_mul_ps(tap_e, sum));
        }

        _mm_storeu_ps(out + (n << 1), acc);
    }

    /* Scalar epilogue for remaining sample */
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

    /* Update history */
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
}

/**
 * SSE2 complex half-band decimator by 2.
 * Processes 2 output samples (4 floats) at a time.
 */
extern "C" int
simd_hb_decim2_complex_sse2(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
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
    float lastI = (ch_len > 0) ? in[in_len - 2] : 0.0f;
    float lastQ = (ch_len > 0) ? in[in_len - 1] : 0.0f;

    auto get_iq = [&](int src_idx, float& xi, float& xq) {
        if (src_idx < left_len) {
            xi = hist_i[src_idx];
            xq = hist_q[src_idx];
        } else {
            int rel = src_idx - left_len;
            if (rel < ch_len) {
                xi = in[rel << 1];
                xq = in[(rel << 1) + 1];
            } else {
                xi = lastI;
                xq = lastQ;
            }
        }
    };

    /* Process 2 output samples at a time */
    int n = 0;
    for (; n + 1 < out_ch_len; n += 2) {
        __m128 acc = _mm_setzero_ps();

        /* Center tap */
        float cc = taps[center];
        __m128 tap_c = _mm_set1_ps(cc);

        /* Output n: input index = 2*n, center_idx = left_len + 2*n */
        /* Output n+1: input index = 2*(n+1), center_idx = left_len + 2*(n+1) */
        int center_idx0 = left_len + (n << 1);
        int center_idx1 = left_len + ((n + 1) << 1);

        float ci0, cq0, ci1, cq1;
        get_iq(center_idx0, ci0, cq0);
        get_iq(center_idx1, ci1, cq1);
        __m128 center_val = _mm_set_ps(cq1, ci1, cq0, ci0);
        acc = _mm_add_ps(acc, _mm_mul_ps(tap_c, center_val));

        /* Half-band: only even tap indices are non-zero */
        for (int e = 0; e < center; e += 2) {
            float ce = taps[e];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - e;
            __m128 tap_e = _mm_set1_ps(ce);

            float xmI0, xmQ0, xpI0, xpQ0;
            get_iq(center_idx0 - d, xmI0, xmQ0);
            get_iq(center_idx0 + d, xpI0, xpQ0);

            float xmI1, xmQ1, xpI1, xpQ1;
            get_iq(center_idx1 - d, xmI1, xmQ1);
            get_iq(center_idx1 + d, xpI1, xpQ1);

            __m128 sum_m = _mm_set_ps(xmQ1, xmI1, xmQ0, xmI0);
            __m128 sum_p = _mm_set_ps(xpQ1, xpI1, xpQ0, xpI0);
            __m128 sum = _mm_add_ps(sum_m, sum_p);
            acc = _mm_add_ps(acc, _mm_mul_ps(tap_e, sum));
        }

        _mm_storeu_ps(out + (n << 1), acc);
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

    /* Update history */
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

    return out_ch_len << 1;
}

/**
 * SSE2 real half-band decimator by 2.
 * Processes 4 output samples at a time.
 */
extern "C" int
simd_hb_decim2_real_sse2(const float* in, int in_len, float* out, float* hist, const float* taps, int taps_len) {
    if (taps_len < 3 || (taps_len & 1) == 0) {
        return 0;
    }

    const int hist_len = taps_len - 1;
    const int center = (taps_len - 1) >> 1;
    int out_len = in_len >> 1;
    if (out_len <= 0) {
        return 0;
    }

    float last = (in_len > 0) ? in[in_len - 1] : 0.0f;

    auto get_sample = [&](int src_idx) -> float {
        if (src_idx < hist_len) {
            return hist[src_idx];
        } else {
            int rel = src_idx - hist_len;
            return (rel < in_len) ? in[rel] : last;
        }
    };

    /* Process 4 output samples at a time */
    int n = 0;
    for (; n + 3 < out_len; n += 4) {
        __m128 acc = _mm_setzero_ps();

        /* Center tap */
        float cc = taps[center];
        __m128 tap_c = _mm_set1_ps(cc);

        /* Center indices for 4 outputs */
        int ci0 = hist_len + (n << 1);
        int ci1 = hist_len + ((n + 1) << 1);
        int ci2 = hist_len + ((n + 2) << 1);
        int ci3 = hist_len + ((n + 3) << 1);

        __m128 center_val = _mm_set_ps(get_sample(ci3), get_sample(ci2), get_sample(ci1), get_sample(ci0));
        acc = _mm_add_ps(acc, _mm_mul_ps(tap_c, center_val));

        /* Half-band: only even indices are non-zero */
        for (int e = 0; e < center; e += 2) {
            float ce = taps[e];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - e;
            __m128 tap_e = _mm_set1_ps(ce);

            __m128 sum_m =
                _mm_set_ps(get_sample(ci3 - d), get_sample(ci2 - d), get_sample(ci1 - d), get_sample(ci0 - d));
            __m128 sum_p =
                _mm_set_ps(get_sample(ci3 + d), get_sample(ci2 + d), get_sample(ci1 + d), get_sample(ci0 + d));
            __m128 sum = _mm_add_ps(sum_m, sum_p);
            acc = _mm_add_ps(acc, _mm_mul_ps(tap_e, sum));
        }

        _mm_storeu_ps(out + n, acc);
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

    /* Update history */
    if (in_len >= hist_len) {
        std::memcpy(hist, in + (in_len - hist_len), (size_t)hist_len * sizeof(float));
    } else {
        int need = hist_len - in_len;
        if (need > 0) {
            std::memmove(hist, hist + in_len, (size_t)need * sizeof(float));
        }
        std::memcpy(hist + need, in, (size_t)in_len * sizeof(float));
    }

    return out_len;
}
