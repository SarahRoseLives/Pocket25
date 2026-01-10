// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Polyphase rational resampler implementation (L/M) using float samples.
 *
 * Simplified scalar upfirdn implementation used by the FM audio path.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/dsp/demod_state.h>
#include <dsd-neo/dsp/resampler.h>
#include <dsd-neo/runtime/mem.h>

#if defined(__GNUC__) || defined(__clang__)
#define DSD_NEO_PRAGMA(x) _Pragma(#x)
#define DSD_NEO_IVDEP     DSD_NEO_PRAGMA(GCC ivdep)
#else
#define DSD_NEO_IVDEP
#endif

#ifndef DSD_NEO_ALIGN
#define DSD_NEO_ALIGN 64
#endif

#if defined(__GNUC__) || defined(__clang__)
#define DSD_NEO_RESTRICT __restrict__
#else
#define DSD_NEO_RESTRICT
#endif

static const double kPi = 3.14159265358979323846;

template <typename T>
static inline T*
assume_aligned_ptr(T* p, size_t /*align_unused*/) {
#if defined(__GNUC__) || defined(__clang__)
    return (T*)__builtin_assume_aligned(p, 64);
#else
    return p;
#endif
}

template <typename T>
static inline const T*
assume_aligned_ptr(const T* p, size_t /*align_unused*/) {
#if defined(__GNUC__) || defined(__clang__)
    return (const T*)__builtin_assume_aligned(p, 64);
#else
    return p;
#endif
}

static inline double
dsd_neo_sinc(double x) {
    if (x == 0.0) {
        return 1.0;
    }
    return sin(kPi * x) / (kPi * x);
}

void
resamp_design(struct demod_state* s, int L, int M) {
    int taps_per_phase = 16; /* K */
    if (taps_per_phase < 8) {
        taps_per_phase = 8;
    }
    int total_taps = taps_per_phase * L;
    if (total_taps < L) {
        total_taps = L;
    }

    double fc = 0.45 / (double)((L > M) ? L : M);
    int N = total_taps;
    int mid = (N - 1) / 2;

    if (s->resamp_taps) {
        dsd_neo_aligned_free(s->resamp_taps);
        s->resamp_taps = NULL;
    }
    if (s->resamp_hist) {
        dsd_neo_aligned_free(s->resamp_hist);
        s->resamp_hist = NULL;
    }
    {
        void* mem_ptr = dsd_neo_aligned_malloc((size_t)N * sizeof(float));
        s->resamp_taps = (float*)mem_ptr;
    }
    {
        void* mem_ptr = dsd_neo_aligned_malloc((size_t)taps_per_phase * sizeof(float));
        s->resamp_hist = (float*)mem_ptr;
    }
    if (!s->resamp_taps || !s->resamp_hist) {
        if (s->resamp_taps) {
            free(s->resamp_taps);
            s->resamp_taps = NULL;
        }
        if (s->resamp_hist) {
            free(s->resamp_hist);
            s->resamp_hist = NULL;
        }
        s->resamp_enabled = 0;
        return;
    }
    memset(s->resamp_hist, 0, (size_t)taps_per_phase * sizeof(float));
    s->resamp_hist_head = 0;

    double gain = 0.0;
    for (int n = 0; n < N; n++) {
        int m = n - mid;
        double w = 0.54 - 0.46 * cos(2.0 * kPi * (double)n / (double)(N - 1));
        double h = 2.0 * fc * dsd_neo_sinc(2.0 * fc * (double)m);
        gain += h * w;
    }
    if (gain == 0.0) {
        gain = 1.0;
    }

    const double phase_gain_comp = (double)L;
    for (int n = 0; n < N; n++) {
        int m = n - mid;
        double w = 0.54 - 0.46 * cos(2.0 * kPi * (double)n / (double)(N - 1));
        double h = 2.0 * fc * dsd_neo_sinc(2.0 * fc * (double)m);
        double t = (h * w / gain) * phase_gain_comp;
        s->resamp_taps[n] = (float)t;
    }

    s->resamp_L = L;
    s->resamp_M = M;
    s->resamp_phase = 0;
    s->resamp_taps_len = N;
    s->resamp_taps_per_phase = taps_per_phase;
    s->resamp_enabled = 1;
}

int
resamp_process_block(struct demod_state* s, const float* DSD_NEO_RESTRICT in, int in_len, float* DSD_NEO_RESTRICT out) {
    if (!s->resamp_enabled || !s->resamp_taps || !s->resamp_hist) {
        if (out != in) {
            memcpy(out, in, (size_t)in_len * sizeof(float));
        }
        return in_len;
    }
    const int L = s->resamp_L;
    const int M = s->resamp_M;
    const int K = s->resamp_taps_per_phase;
    const float* DSD_NEO_RESTRICT taps_al = assume_aligned_ptr(s->resamp_taps, DSD_NEO_ALIGN);
    int phase = s->resamp_phase;
    int head = s->resamp_hist_head;
    int out_len = 0;
    const float* DSD_NEO_RESTRICT in_al = assume_aligned_ptr(in, DSD_NEO_ALIGN);
    float* DSD_NEO_RESTRICT out_al = assume_aligned_ptr(out, DSD_NEO_ALIGN);
    float* DSD_NEO_RESTRICT hist = assume_aligned_ptr(s->resamp_hist, DSD_NEO_ALIGN);

    for (int n = 0; n < in_len; n++) {
        hist[head] = in_al[n];
        head++;
        if (head == K) {
            head = 0;
        }
        int local_phase = phase;
        while (local_phase < L) {
            float acc = 0.0f;
            const float* DSD_NEO_RESTRICT tk = taps_al + local_phase;
            int idx = head - 1;
            if (idx < 0) {
                idx += K;
            }
            for (int k = 0; k < K; k++) {
                acc += hist[idx] * tk[0];
                tk += L;
                idx--;
                if (idx < 0) {
                    idx += K;
                }
            }
            out_al[out_len++] = acc;
            local_phase += M;
        }
        phase = local_phase - L;
    }

    s->resamp_phase = phase;
    s->resamp_hist_head = head;
    return out_len;
}
