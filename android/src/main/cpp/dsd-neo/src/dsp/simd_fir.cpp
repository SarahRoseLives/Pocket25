// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief SIMD FIR dispatch + scalar fallbacks + CPU detection.
 *
 * Implements runtime dispatch for FIR filter functions. Scalar implementations
 * serve as reference and fallback. CPU detection selects best available SIMD.
 */

#include <dsd-neo/dsp/simd_fir.h>

#include <atomic>
#include <cstring>

/* Platform-specific CPU feature detection */
#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>

/* Use inline assembly for _xgetbv to avoid target-specific option issues */
static inline unsigned long long
dsd_xgetbv(unsigned int xcr) {
    unsigned int eax, edx;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(xcr));
    return ((unsigned long long)edx << 32) | eax;
}
#endif
#endif

/* Forward declarations for SIMD specializations (defined in arch-specific TUs) */
#if defined(__x86_64__) || defined(_M_X64)
extern "C" void simd_fir_complex_apply_sse2(const float* in, int in_len, float* out, float* hist_i, float* hist_q,
                                            const float* taps, int taps_len);
extern "C" int simd_hb_decim2_complex_sse2(const float* in, int in_len, float* out, float* hist_i, float* hist_q,
                                           const float* taps, int taps_len);
extern "C" int simd_hb_decim2_real_sse2(const float* in, int in_len, float* out, float* hist, const float* taps,
                                        int taps_len);
extern "C" void simd_fir_complex_apply_avx2(const float* in, int in_len, float* out, float* hist_i, float* hist_q,
                                            const float* taps, int taps_len);
extern "C" int simd_hb_decim2_complex_avx2(const float* in, int in_len, float* out, float* hist_i, float* hist_q,
                                           const float* taps, int taps_len);
extern "C" int simd_hb_decim2_real_avx2(const float* in, int in_len, float* out, float* hist, const float* taps,
                                        int taps_len);
#endif

#if defined(__aarch64__)
extern "C" void simd_fir_complex_apply_neon(const float* in, int in_len, float* out, float* hist_i, float* hist_q,
                                            const float* taps, int taps_len);
extern "C" int simd_hb_decim2_complex_neon(const float* in, int in_len, float* out, float* hist_i, float* hist_q,
                                           const float* taps, int taps_len);
extern "C" int simd_hb_decim2_real_neon(const float* in, int in_len, float* out, float* hist, const float* taps,
                                        int taps_len);
#endif

/* -------------------------------------------------------------------------- */
/* Scalar Reference Implementations                                           */
/* -------------------------------------------------------------------------- */

/**
 * Scalar complex symmetric FIR filter (no decimation).
 * Exploits tap symmetry: acc += tap[k] * (x[center-d] + x[center+d]).
 */
static void
simd_fir_complex_apply_scalar(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
                              int taps_len) {
    if (taps_len < 3 || (taps_len & 1) == 0 || in_len < 2) {
        return;
    }

    const int N = in_len >> 1; /* complex samples */
    const int hist_len = taps_len - 1;
    const int center = (taps_len - 1) >> 1;

    /* Get last sample for edge handling */
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

    for (int n = 0; n < N; n++) {
        int center_idx = hist_len + n;
        float accI = 0.0f;
        float accQ = 0.0f;

        /* Center tap */
        float ci, cq;
        get_iq(center_idx, ci, cq);
        float cc = taps[center];
        accI += cc * ci;
        accQ += cc * cq;

        /* Symmetric pairs */
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

    /* Update history with last (hist_len) complex samples */
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
 * Scalar complex half-band decimator by 2.
 * Exploits zero-valued odd taps (except center) AND tap symmetry.
 */
static int
simd_hb_decim2_complex_scalar(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
                              int taps_len) {
    if (taps_len < 3 || (taps_len & 1) == 0) {
        return 0;
    }

    int ch_len = in_len >> 1;     /* per-channel samples */
    int out_ch_len = ch_len >> 1; /* decimated per-channel */
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

    for (int n = 0; n < out_ch_len; n++) {
        int center_idx = (taps_len - 1) + (n << 1);
        float accI = 0.0f;
        float accQ = 0.0f;

        /* Center tap */
        float ci, cq;
        get_iq(center_idx, ci, cq);
        float cc = taps[center];
        accI += cc * ci;
        accQ += cc * cq;

        /* Symmetric pairs: only even indices have non-zero taps in half-band */
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

    /* Update histories with last (taps_len-1) per-channel input samples */
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
 * Scalar real half-band decimator by 2.
 * Exploits zero-valued odd taps (except center) AND tap symmetry.
 */
static int
simd_hb_decim2_real_scalar(const float* in, int in_len, float* out, float* hist, const float* taps, int taps_len) {
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

    for (int n = 0; n < out_len; n++) {
        int center_idx = hist_len + (n << 1);
        float acc = 0.0f;

        /* Center tap */
        acc += taps[center] * get_sample(center_idx);

        /* Symmetric pairs: only even indices have non-zero taps */
        for (int e = 0; e < center; e += 2) {
            float ce = taps[e];
            if (ce == 0.0f) {
                continue;
            }
            int d = center - e;
            float xm = get_sample(center_idx - d);
            float xp = get_sample(center_idx + d);
            acc += ce * (xm + xp);
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

/* -------------------------------------------------------------------------- */
/* CPU Feature Detection                                                      */
/* -------------------------------------------------------------------------- */

#if defined(__x86_64__) || defined(_M_X64)

static bool
cpu_has_avx2_with_os_support() {
#if defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        return false;
    }
    bool osxsave = (ecx & (1u << 27)) != 0;
    bool avx = (ecx & (1u << 28)) != 0;
    bool fma = (ecx & (1u << 12)) != 0;
    if (!osxsave || !avx) {
        return false;
    }
    /* Check OS has enabled YMM state saving via XGETBV */
    unsigned long long xcr0 = dsd_xgetbv(0);
    bool ymm_enabled = (xcr0 & 0x6) == 0x6; /* XMM + YMM state enabled */
    if (!ymm_enabled) {
        return false;
    }
    /* Check AVX2 support in extended features */
    if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        return false;
    }
    bool has_avx2 = (ebx & (1u << 5)) != 0;
    return has_avx2 && fma;
#elif defined(_MSC_VER)
    int cpuInfo[4];
    __cpuid(cpuInfo, 1);
    bool osxsave = (cpuInfo[2] & (1 << 27)) != 0;
    bool avx = (cpuInfo[2] & (1 << 28)) != 0;
    bool fma = (cpuInfo[2] & (1 << 12)) != 0;
    if (!osxsave || !avx) {
        return false;
    }
    unsigned long long xcr0 = _xgetbv(0);
    bool ymm_enabled = (xcr0 & 0x6) == 0x6;
    if (!ymm_enabled) {
        return false;
    }
    __cpuidex(cpuInfo, 7, 0);
    bool has_avx2 = (cpuInfo[1] & (1 << 5)) != 0;
    return has_avx2 && fma;
#else
    return false;
#endif
}

#endif /* x86_64 */

/* -------------------------------------------------------------------------- */
/* Function Pointer Dispatch                                                  */
/* -------------------------------------------------------------------------- */

using fir_complex_fn = void (*)(const float*, int, float*, float*, float*, const float*, int);
using hb_decim2_complex_fn = int (*)(const float*, int, float*, float*, float*, const float*, int);
using hb_decim2_real_fn = int (*)(const float*, int, float*, float*, const float*, int);

static fir_complex_fn g_fir_complex_impl = simd_fir_complex_apply_scalar;
static hb_decim2_complex_fn g_hb_decim2_complex_impl = simd_hb_decim2_complex_scalar;
static hb_decim2_real_fn g_hb_decim2_real_impl = simd_hb_decim2_real_scalar;
static const char* g_impl_name = "scalar";

/* Dispatch init state: 0 = not started, 1 = in progress, 2 = done */
static std::atomic<int> g_fir_init_done{0};

static void
simd_fir_init_dispatch() {
    int expected = 0;
    if (!g_fir_init_done.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        /* Another thread is initializing; spin until done */
        while (g_fir_init_done.load(std::memory_order_acquire) != 2) {
            /* spin */
        }
        return;
    }

    /* Perform one-time initialization */
#if defined(__x86_64__) || defined(_M_X64)
    if (cpu_has_avx2_with_os_support()) {
        g_fir_complex_impl = simd_fir_complex_apply_avx2;
        g_hb_decim2_complex_impl = simd_hb_decim2_complex_avx2;
        g_hb_decim2_real_impl = simd_hb_decim2_real_avx2;
        g_impl_name = "avx2";
    } else {
        g_fir_complex_impl = simd_fir_complex_apply_sse2;
        g_hb_decim2_complex_impl = simd_hb_decim2_complex_sse2;
        g_hb_decim2_real_impl = simd_hb_decim2_real_sse2;
        g_impl_name = "sse2";
    }
#elif defined(__aarch64__)
    g_fir_complex_impl = simd_fir_complex_apply_neon;
    g_hb_decim2_complex_impl = simd_hb_decim2_complex_neon;
    g_hb_decim2_real_impl = simd_hb_decim2_real_neon;
    g_impl_name = "neon";
#else
    /* Already set to scalar */
#endif

    g_fir_init_done.store(2, std::memory_order_release);
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

extern "C" void
simd_fir_complex_apply(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
                       int taps_len) {
    if (g_fir_init_done.load(std::memory_order_acquire) != 2) {
        simd_fir_init_dispatch();
    }
    g_fir_complex_impl(in, in_len, out, hist_i, hist_q, taps, taps_len);
}

extern "C" int
simd_hb_decim2_complex(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
                       int taps_len) {
    if (g_fir_init_done.load(std::memory_order_acquire) != 2) {
        simd_fir_init_dispatch();
    }
    return g_hb_decim2_complex_impl(in, in_len, out, hist_i, hist_q, taps, taps_len);
}

extern "C" int
simd_hb_decim2_real(const float* in, int in_len, float* out, float* hist, const float* taps, int taps_len) {
    if (g_fir_init_done.load(std::memory_order_acquire) != 2) {
        simd_fir_init_dispatch();
    }
    return g_hb_decim2_real_impl(in, in_len, out, hist, taps, taps_len);
}

extern "C" const char*
simd_fir_get_impl_name(void) {
    if (g_fir_init_done.load(std::memory_order_acquire) != 2) {
        simd_fir_init_dispatch();
    }
    return g_impl_name;
}
