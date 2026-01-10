// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief ARM64 NEON implementations of SIMD FIR filter functions.
 *
 * NEON is always available on AArch64. Processes 4 floats at a time using
 * 128-bit vector registers. Uses NEON FMA intrinsics for efficiency.
 */

#include <arm_neon.h>
#include <cstring>

/**
 * NEON complex symmetric FIR filter (no decimation).
 * Processes 2 complex samples (4 floats) at a time.
 */
extern "C" void
simd_fir_complex_apply_neon(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
                            int taps_len) {
    if (taps_len < 3 || (taps_len & 1) == 0 || in_len < 2) {
        return;
    }

    const int N = in_len >> 1; /* complex samples */
    const int hist_len = taps_len - 1;
    const int center = (taps_len - 1) >> 1;

    float lastI = (N > 0) ? in[(N - 1) << 1] : 0.0f;
    float lastQ = (N > 0) ? in[((N - 1) << 1) + 1] : 0.0f;

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
        float32x4_t acc = vdupq_n_f32(0.0f);

        /* Center tap */
        float cc = taps[center];
        float32x4_t tap_c = vdupq_n_f32(cc);

        float ci0, cq0, ci1, cq1;
        get_iq(hist_len + n, ci0, cq0);
        get_iq(hist_len + n + 1, ci1, cq1);
        float center_arr[4] = {ci0, cq0, ci1, cq1};
        float32x4_t center_val = vld1q_f32(center_arr);
        acc = vfmaq_f32(acc, tap_c, center_val);

        /* Symmetric pairs */
        for (int k = 0; k < center; k++) {
            float ce = taps[k];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - k;
            float32x4_t tap_e = vdupq_n_f32(ce);

            float xmI0, xmQ0, xpI0, xpQ0;
            get_iq(hist_len + n - d, xmI0, xmQ0);
            get_iq(hist_len + n + d, xpI0, xpQ0);

            float xmI1, xmQ1, xpI1, xpQ1;
            get_iq(hist_len + n + 1 - d, xmI1, xmQ1);
            get_iq(hist_len + n + 1 + d, xpI1, xpQ1);

            float sum_m_arr[4] = {xmI0, xmQ0, xmI1, xmQ1};
            float sum_p_arr[4] = {xpI0, xpQ0, xpI1, xpQ1};
            float32x4_t sum_m = vld1q_f32(sum_m_arr);
            float32x4_t sum_p = vld1q_f32(sum_p_arr);
            float32x4_t sum = vaddq_f32(sum_m, sum_p);
            acc = vfmaq_f32(acc, tap_e, sum);
        }

        vst1q_f32(out + (n << 1), acc);
    }

    /* Scalar epilogue */
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
 * NEON complex half-band decimator by 2.
 * Processes 2 output samples (4 floats) at a time.
 */
extern "C" int
simd_hb_decim2_complex_neon(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
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
        float32x4_t acc = vdupq_n_f32(0.0f);

        float cc = taps[center];
        float32x4_t tap_c = vdupq_n_f32(cc);

        int center_idx0 = left_len + (n << 1);
        int center_idx1 = left_len + ((n + 1) << 1);

        float ci0, cq0, ci1, cq1;
        get_iq(center_idx0, ci0, cq0);
        get_iq(center_idx1, ci1, cq1);
        float center_arr[4] = {ci0, cq0, ci1, cq1};
        float32x4_t center_val = vld1q_f32(center_arr);
        acc = vfmaq_f32(acc, tap_c, center_val);

        /* Half-band: only even tap indices */
        for (int e = 0; e < center; e += 2) {
            float ce = taps[e];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - e;
            float32x4_t tap_e = vdupq_n_f32(ce);

            float xmI0, xmQ0, xpI0, xpQ0;
            get_iq(center_idx0 - d, xmI0, xmQ0);
            get_iq(center_idx0 + d, xpI0, xpQ0);

            float xmI1, xmQ1, xpI1, xpQ1;
            get_iq(center_idx1 - d, xmI1, xmQ1);
            get_iq(center_idx1 + d, xpI1, xpQ1);

            float sum_m_arr[4] = {xmI0, xmQ0, xmI1, xmQ1};
            float sum_p_arr[4] = {xpI0, xpQ0, xpI1, xpQ1};
            float32x4_t sum_m = vld1q_f32(sum_m_arr);
            float32x4_t sum_p = vld1q_f32(sum_p_arr);
            float32x4_t sum = vaddq_f32(sum_m, sum_p);
            acc = vfmaq_f32(acc, tap_e, sum);
        }

        vst1q_f32(out + (n << 1), acc);
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
 * NEON real half-band decimator by 2.
 * Processes 4 output samples at a time.
 */
extern "C" int
simd_hb_decim2_real_neon(const float* in, int in_len, float* out, float* hist, const float* taps, int taps_len) {
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
        float32x4_t acc = vdupq_n_f32(0.0f);

        float cc = taps[center];
        float32x4_t tap_c = vdupq_n_f32(cc);

        int ci0 = hist_len + (n << 1);
        int ci1 = hist_len + ((n + 1) << 1);
        int ci2 = hist_len + ((n + 2) << 1);
        int ci3 = hist_len + ((n + 3) << 1);

        float center_arr[4] = {get_sample(ci0), get_sample(ci1), get_sample(ci2), get_sample(ci3)};
        float32x4_t center_val = vld1q_f32(center_arr);
        acc = vfmaq_f32(acc, tap_c, center_val);

        /* Half-band: only even indices */
        for (int e = 0; e < center; e += 2) {
            float ce = taps[e];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - e;
            float32x4_t tap_e = vdupq_n_f32(ce);

            float sum_m_arr[4] = {get_sample(ci0 - d), get_sample(ci1 - d), get_sample(ci2 - d), get_sample(ci3 - d)};
            float sum_p_arr[4] = {get_sample(ci0 + d), get_sample(ci1 + d), get_sample(ci2 + d), get_sample(ci3 + d)};
            float32x4_t sum_m = vld1q_f32(sum_m_arr);
            float32x4_t sum_p = vld1q_f32(sum_p_arr);
            float32x4_t sum = vaddq_f32(sum_m, sum_p);
            acc = vfmaq_f32(acc, tap_e, sum);
        }

        vst1q_f32(out + n, acc);
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
