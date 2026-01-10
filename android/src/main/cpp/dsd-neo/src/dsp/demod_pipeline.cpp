// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief DSP demodulation pipeline (FM/complex baseband) implementation.
 *
 * Provides decimation, optional FLL/TED, discrimination, deemphasis, DC block,
 * and audio filtering. Public APIs are declared in `dsp/demod_pipeline.h`.
 */

#include <dsd-neo/dsp/costas.h>
#include <dsd-neo/dsp/demod_pipeline.h>
#include <dsd-neo/dsp/demod_state.h>
#include <dsd-neo/dsp/firdes.h>
#include <dsd-neo/dsp/fll.h>
#include <dsd-neo/dsp/halfband.h>
#include <dsd-neo/dsp/math_utils.h>
#include <dsd-neo/dsp/simd_fir.h>
#include <dsd-neo/dsp/ted.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/mem.h>
#include <dsd-neo/runtime/rtl_stream_metrics_hooks.h>

#include <algorithm>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

/* Macros and constants from the original file */
#ifndef MAXIMUM_OVERSAMPLE
#define MAXIMUM_OVERSAMPLE 16
#endif
#ifndef DEFAULT_BUF_LENGTH
#define DEFAULT_BUF_LENGTH 16384
#endif
#ifndef MAXIMUM_BUF_LENGTH
#define MAXIMUM_BUF_LENGTH (MAXIMUM_OVERSAMPLE * DEFAULT_BUF_LENGTH)
#endif

#ifndef DSD_NEO_ALIGN
#define DSD_NEO_ALIGN 64
#endif

#ifndef DSD_NEO_RESTRICT
#if defined(_MSC_VER)
#define DSD_NEO_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define DSD_NEO_RESTRICT __restrict__
#else
#define DSD_NEO_RESTRICT
#endif
#endif

#ifndef DSD_NEO_IVDEP
#define DSD_NEO_IVDEP
#endif

static inline int
debug_cqpsk_enabled(void) {
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (!cfg) {
        dsd_neo_config_init(NULL);
        cfg = dsd_neo_get_config();
    }
    return (cfg && cfg->debug_cqpsk_enable) ? 1 : 0;
}

/* Platform-specific aligned pointer assumption */
template <typename T>
static inline T*
assume_aligned_ptr(T* p, size_t /*align_unused*/) {
    return p;
}

template <typename T>
static inline const T*
assume_aligned_ptr(const T* p, size_t /*align_unused*/) {
    return p;
}

/* Fixed channel low-pass for high-rate mode.
 *
 * Profiles (see DSD_CH_LPF_PROFILE_*):
 *   - Wide/analog: 8000 Hz cutoff.
 *   - 6.25 kHz modes: 3500 Hz cutoff (NXDN48/dPMR/D-STAR).
 *   - 12.5 kHz 4FSK modes: 5100 Hz cutoff (DMR/NXDN96/X2TDMA/YSF/M17).
 *   - ProVoice: 6250 Hz cutoff.
 *   - P25 C4FM: 5200 Hz cutoff.
 *   - P25 CQPSK/LSM: 7250 Hz cutoff.
 *
 * Legacy 63-tap Blackman prototypes are kept as fallback; preferred taps are
 * generated per sample rate to preserve the intended spectral shape at any Fs.
 *
 * At 48 kHz with 1200 Hz transition width:
 *   - Hamming: ntaps = (53 * 48000) / (22 * 1200) = 97
 *   - Blackman: ntaps = (74 * 48000) / (22 * 1200) = 135
 * Size 144 provides headroom for higher sample rates. */
static const int kChannelLpfTaps = 144;
/* Legacy fallback filters are 63 taps (designed for 24 kHz). Only used when
 * dynamic filter generation fails; prefer dynamically generated taps. */
static const int kChannelLpfFallbackTaps = 63;
static const float channel_lpf_wide[kChannelLpfFallbackTaps] = {
    0.0f,
    0.0f,
    -1.0f / 32768.0f,
    3.0f / 32768.0f,
    0.0f,
    -9.0f / 32768.0f,
    14.0f / 32768.0f,
    0.0f,
    -28.0f / 32768.0f,
    39.0f / 32768.0f,
    0.0f,
    -68.0f / 32768.0f,
    88.0f / 32768.0f,
    0.0f,
    -142.0f / 32768.0f,
    178.0f / 32768.0f,
    0.0f,
    -271.0f / 32768.0f,
    330.0f / 32768.0f,
    0.0f,
    -486.0f / 32768.0f,
    586.0f / 32768.0f,
    0.0f,
    -859.0f / 32768.0f,
    1047.0f / 32768.0f,
    0.0f,
    -1625.0f / 32768.0f,
    2111.0f / 32768.0f,
    0.0f,
    -4441.0f / 32768.0f,
    8995.0f / 32768.0f,
    21845.0f / 32768.0f,
    8995.0f / 32768.0f,
    -4441.0f / 32768.0f,
    0.0f,
    2111.0f / 32768.0f,
    -1625.0f / 32768.0f,
    0.0f,
    1047.0f / 32768.0f,
    -859.0f / 32768.0f,
    0.0f,
    586.0f / 32768.0f,
    -486.0f / 32768.0f,
    0.0f,
    330.0f / 32768.0f,
    -271.0f / 32768.0f,
    0.0f,
    178.0f / 32768.0f,
    -142.0f / 32768.0f,
    0.0f,
    88.0f / 32768.0f,
    -68.0f / 32768.0f,
    0.0f,
    39.0f / 32768.0f,
    -28.0f / 32768.0f,
    0.0f,
    14.0f / 32768.0f,
    -9.0f / 32768.0f,
    0.0f,
    3.0f / 32768.0f,
    -1.0f / 32768.0f,
    0.0f,
    0.0f,
};

/* Legacy digital profile taps (fc≈5 kHz @ 24 kHz, 63 taps). Designed as
   Blackman-windowed sinc and normalized to unity DC gain. */
static const float channel_lpf_digital[kChannelLpfFallbackTaps] = {
    0.0f,
    0.0f,
    0.0f,
    -3.0f / 32768.0f,
    -4.0f / 32768.0f,
    5.0f / 32768.0f,
    15.0f / 32768.0f,
    0.0f,
    -31.0f / 32768.0f,
    -22.0f / 32768.0f,
    42.0f / 32768.0f,
    68.0f / 32768.0f,
    -26.0f / 32768.0f,
    -130.0f / 32768.0f,
    -43.0f / 32768.0f,
    178.0f / 32768.0f,
    180.0f / 32768.0f,
    -156.0f / 32768.0f,
    -368.0f / 32768.0f,
    0.0f,
    542.0f / 32768.0f,
    339.0f / 32768.0f,
    -579.0f / 32768.0f,
    -859.0f / 32768.0f,
    313.0f / 32768.0f,
    1492.0f / 32768.0f,
    486.0f / 32768.0f,
    -2111.0f / 32768.0f,
    -2367.0f / 32768.0f,
    2564.0f / 32768.0f,
    10033.0f / 32768.0f,
    13654.0f / 32768.0f,
    10033.0f / 32768.0f,
    2564.0f / 32768.0f,
    -2367.0f / 32768.0f,
    -2111.0f / 32768.0f,
    486.0f / 32768.0f,
    1492.0f / 32768.0f,
    313.0f / 32768.0f,
    -859.0f / 32768.0f,
    -579.0f / 32768.0f,
    339.0f / 32768.0f,
    542.0f / 32768.0f,
    0.0f,
    -368.0f / 32768.0f,
    -156.0f / 32768.0f,
    180.0f / 32768.0f,
    178.0f / 32768.0f,
    -43.0f / 32768.0f,
    -130.0f / 32768.0f,
    -26.0f / 32768.0f,
    68.0f / 32768.0f,
    42.0f / 32768.0f,
    -22.0f / 32768.0f,
    -31.0f / 32768.0f,
    0.0f,
    15.0f / 32768.0f,
    5.0f / 32768.0f,
    -4.0f / 32768.0f,
    -3.0f / 32768.0f,
    0.0f,
    0.0f,
    0.0f,
};

/* ---------------- Post-demod audio polyphase decimator (M > 2) -------------- */
/*
 * Lightweight 1/M polyphase decimator for audio. Designs a windowed-sinc
 * prototype (Q15) for the given M and streams with a circular history.
 * Keeps state in demod_state to persist across blocks.
 */
static void
audio_polydecim_ensure(struct demod_state* d, int M) {
    if (!d) {
        return;
    }
    if (M <= 2) {
        d->post_polydecim_enabled = 0;
        return;
    }
    const int K = 16; /* taps */
    int redesign = 0;
    if (!d->post_polydecim_taps || !d->post_polydecim_hist || d->post_polydecim_M != M || d->post_polydecim_K != K) {
        redesign = 1;
    }
    if (!redesign) {
        d->post_polydecim_enabled = 1;
        return;
    }
    if (d->post_polydecim_taps) {
        dsd_neo_aligned_free(d->post_polydecim_taps);
        d->post_polydecim_taps = NULL;
    }
    if (d->post_polydecim_hist) {
        dsd_neo_aligned_free(d->post_polydecim_hist);
        d->post_polydecim_hist = NULL;
    }
    {
        void* mem_ptr = dsd_neo_aligned_malloc((size_t)K * sizeof(float));
        d->post_polydecim_taps = (float*)mem_ptr;
    }
    {
        void* mem_ptr = dsd_neo_aligned_malloc((size_t)K * sizeof(float));
        d->post_polydecim_hist = (float*)mem_ptr;
    }
    if (!d->post_polydecim_taps || !d->post_polydecim_hist) {
        if (d->post_polydecim_taps) {
            free(d->post_polydecim_taps);
            d->post_polydecim_taps = NULL;
        }
        if (d->post_polydecim_hist) {
            free(d->post_polydecim_hist);
            d->post_polydecim_hist = NULL;
        }
        d->post_polydecim_enabled = 0;
        return;
    }
    memset(d->post_polydecim_hist, 0, (size_t)K * sizeof(float));
    d->post_polydecim_hist_head = 0;
    d->post_polydecim_phase = 0;

    /* Design windowed-sinc low-pass: fc ≈ 0.45 / M (normalized), Hamming window */
    double fc = 0.45 / (double)M;
    int N = K;
    int mid = (N - 1) / 2;
    double gain = 0.0;
    for (int n = 0; n < N; n++) {
        int m = n - mid;
        double w = 0.54 - 0.46 * cos(2.0 * 3.14159265358979323846 * (double)n / (double)(N - 1));
        double h = 2.0 * fc * dsd_neo_sinc(2.0 * fc * (double)m);
        double t = h * w;
        gain += t;
    }
    if (gain == 0.0) {
        gain = 1.0;
    }
    for (int n = 0; n < N; n++) {
        int m = n - mid;
        double w = 0.54 - 0.46 * cos(2.0 * 3.14159265358979323846 * (double)n / (double)(N - 1));
        double h = 2.0 * fc * dsd_neo_sinc(2.0 * fc * (double)m);
        double t = (h * w) / gain;
        d->post_polydecim_taps[n] = (float)t;
    }
    d->post_polydecim_M = M;
    d->post_polydecim_K = K;
    d->post_polydecim_enabled = 1;
}

static int
audio_polydecim_process(struct demod_state* d, const float* in, int in_len, float* out) {
    if (!d || !d->post_polydecim_enabled || !d->post_polydecim_taps || !d->post_polydecim_hist || in_len <= 0) {
        if (in && out && in_len > 0 && in != out) {
            memcpy(out, in, (size_t)in_len * sizeof(float));
        }
        return in_len;
    }
    const int K = d->post_polydecim_K;
    const int M = d->post_polydecim_M;
    int head = d->post_polydecim_hist_head;
    int phase = d->post_polydecim_phase;
    const float* taps = d->post_polydecim_taps;
    int out_len = 0;
    for (int n = 0; n < in_len; n++) {
        /* push */
        d->post_polydecim_hist[head] = in[n];
        head++;
        if (head == K) {
            head = 0;
        }
        /* phase accum */
        phase++;
        if (phase >= M) {
            phase -= M;
            /* dot product over K most recent samples */
            int idx = head - 1;
            if (idx < 0) {
                idx += K;
            }
            float acc = 0.0f;
            for (int k = 0; k < K; k++) {
                acc += d->post_polydecim_hist[idx] * taps[k];
                idx--;
                if (idx < 0) {
                    idx += K;
                }
            }
            out[out_len++] = acc;
        }
    }
    d->post_polydecim_hist_head = head;
    d->post_polydecim_phase = phase;
    return out_len;
}

/* ---------------- Fixed channel LPF (complex, no decimation) ----------------- */

/* Dynamically generated channel LPF taps (per-rate); fall back to legacy static prototypes on error. */
static float s_channel_wide_taps[kChannelLpfTaps];
static float s_channel_6k25_taps[kChannelLpfTaps];
static float s_channel_12k5_taps[kChannelLpfTaps];
static float s_channel_provoice_taps[kChannelLpfTaps];
static float s_channel_p25_c4fm_taps[kChannelLpfTaps];
static float s_channel_p25_cqpsk_taps[kChannelLpfTaps];
static int s_channel_wide_ntaps = 0;
static int s_channel_6k25_ntaps = 0;
static int s_channel_12k5_ntaps = 0;
static int s_channel_provoice_ntaps = 0;
static int s_channel_p25_c4fm_ntaps = 0;
static int s_channel_p25_cqpsk_ntaps = 0;
static double s_channel_taps_sample_rate = 0.0;

/**
 * @brief Ensure channel LPF taps are generated for the given sample rate.
 *
 * Uses dsd_firdes_low_pass() which is a direct port of GNU Radio's firdes::low_pass().
 * Keeps absolute cutoffs in Hz constant across sample rates.
 */
static void
channel_lpf_ensure_taps(double sample_rate) {
    if (sample_rate <= 0.0) {
        return;
    }
    if (sample_rate == s_channel_taps_sample_rate && s_channel_wide_ntaps > 0 && s_channel_6k25_ntaps > 0
        && s_channel_12k5_ntaps > 0 && s_channel_provoice_ntaps > 0 && s_channel_p25_c4fm_ntaps > 0
        && s_channel_p25_cqpsk_ntaps > 0) {
        return; /* Already generated for this sample rate */
    }

    s_channel_wide_ntaps =
        dsd_firdes_low_pass(1.0, sample_rate, 8000.0, 1200.0, DSD_WIN_BLACKMAN, s_channel_wide_taps, kChannelLpfTaps);
    if (s_channel_wide_ntaps < 0) {
        s_channel_wide_ntaps = 0;
    }

    s_channel_6k25_ntaps =
        dsd_firdes_low_pass(1.0, sample_rate, 3500.0, 1200.0, DSD_WIN_BLACKMAN, s_channel_6k25_taps, kChannelLpfTaps);
    if (s_channel_6k25_ntaps < 0) {
        s_channel_6k25_ntaps = 0;
    }

    s_channel_12k5_ntaps =
        dsd_firdes_low_pass(1.0, sample_rate, 5100.0, 1200.0, DSD_WIN_BLACKMAN, s_channel_12k5_taps, kChannelLpfTaps);
    if (s_channel_12k5_ntaps < 0) {
        s_channel_12k5_ntaps = 0;
    }

    s_channel_provoice_ntaps = dsd_firdes_low_pass(1.0, sample_rate, 6250.0, 1200.0, DSD_WIN_BLACKMAN,
                                                   s_channel_provoice_taps, kChannelLpfTaps);
    if (s_channel_provoice_ntaps < 0) {
        s_channel_provoice_ntaps = 0;
    }

    s_channel_p25_c4fm_ntaps = dsd_firdes_low_pass(1.0, sample_rate, 5200.0, 1200.0, DSD_WIN_BLACKMAN,
                                                   s_channel_p25_c4fm_taps, kChannelLpfTaps);
    if (s_channel_p25_c4fm_ntaps < 0) {
        s_channel_p25_c4fm_ntaps = 0;
    }

    s_channel_p25_cqpsk_ntaps = dsd_firdes_low_pass(1.0, sample_rate, 7250.0, 1200.0, DSD_WIN_BLACKMAN,
                                                    s_channel_p25_cqpsk_taps, kChannelLpfTaps);
    if (s_channel_p25_cqpsk_ntaps < 0) {
        s_channel_p25_cqpsk_ntaps = 0;
    }

    s_channel_taps_sample_rate = sample_rate;
}

static void
channel_lpf_apply(struct demod_state* d) {
    if (!d || !d->channel_lpf_enable || d->lp_len < 2) {
        return;
    }

    const float* taps = NULL;
    int taps_len = 0;

    /* Select filter taps based on profile */
    switch (d->channel_lpf_profile) {
        case DSD_CH_LPF_PROFILE_6K25:
            channel_lpf_ensure_taps((double)d->rate_out);
            if (s_channel_6k25_ntaps > 0) {
                taps = s_channel_6k25_taps;
                taps_len = s_channel_6k25_ntaps;
            } else {
                taps = channel_lpf_digital;
                taps_len = kChannelLpfFallbackTaps;
            }
            break;
        case DSD_CH_LPF_PROFILE_12K5:
            channel_lpf_ensure_taps((double)d->rate_out);
            if (s_channel_12k5_ntaps > 0) {
                taps = s_channel_12k5_taps;
                taps_len = s_channel_12k5_ntaps;
            } else {
                taps = channel_lpf_digital;
                taps_len = kChannelLpfFallbackTaps;
            }
            break;
        case DSD_CH_LPF_PROFILE_PROVOICE:
            channel_lpf_ensure_taps((double)d->rate_out);
            if (s_channel_provoice_ntaps > 0) {
                taps = s_channel_provoice_taps;
                taps_len = s_channel_provoice_ntaps;
            } else {
                taps = channel_lpf_wide;
                taps_len = kChannelLpfFallbackTaps;
            }
            break;
        case DSD_CH_LPF_PROFILE_P25_C4FM:
            channel_lpf_ensure_taps((double)d->rate_out);
            if (s_channel_p25_c4fm_ntaps > 0) {
                taps = s_channel_p25_c4fm_taps;
                taps_len = s_channel_p25_c4fm_ntaps;
            } else {
                taps = channel_lpf_wide;
                taps_len = kChannelLpfFallbackTaps;
            }
            break;
        case DSD_CH_LPF_PROFILE_P25_CQPSK:
            channel_lpf_ensure_taps((double)d->rate_out);
            if (s_channel_p25_cqpsk_ntaps > 0) {
                taps = s_channel_p25_cqpsk_taps;
                taps_len = s_channel_p25_cqpsk_ntaps;
            } else {
                taps = channel_lpf_wide;
                taps_len = kChannelLpfFallbackTaps;
            }
            break;
        case DSD_CH_LPF_PROFILE_WIDE:
        default:
            channel_lpf_ensure_taps((double)d->rate_out);
            if (s_channel_wide_ntaps > 0) {
                taps = s_channel_wide_taps;
                taps_len = s_channel_wide_ntaps;
            } else {
                taps = channel_lpf_wide; /* fallback (63 taps, 24 kHz design) */
                taps_len = kChannelLpfFallbackTaps;
            }
            break;
    }

    const int hist_len = taps_len - 1;
    const int N = d->lp_len >> 1; /* complex samples */
    if (hist_len > d->channel_lpf_hist_len) {
        d->channel_lpf_hist_len = hist_len;
    }

    const float* in = assume_aligned_ptr(d->lowpassed, DSD_NEO_ALIGN);
    float* out = (d->lowpassed == d->hb_workbuf) ? d->timing_buf : d->hb_workbuf;
    float* hi = assume_aligned_ptr(d->channel_lpf_hist_i, DSD_NEO_ALIGN);
    float* hq = assume_aligned_ptr(d->channel_lpf_hist_q, DSD_NEO_ALIGN);

    /* Use SIMD-dispatched complex symmetric FIR */
    simd_fir_complex_apply(in, d->lp_len, out, hi, hq, taps, taps_len);

    d->lowpassed = out;
    d->lp_len = N << 1;
}

/**
 * @brief Half-band decimator for complex interleaved I/Q data.
 *
 * Decimates by 2:1 using symmetric FIR filter. Uses SIMD-dispatched implementation.
 *
 * @param in      Input complex samples (interleaved I/Q).
 * @param in_len  Number of complex samples (total elements = 2 * in_len).
 * @param out     Output buffer for decimated complex samples.
 * @param hist_i  Persistent I-channel history of length HB_TAPS-1.
 * @param hist_q  Persistent Q-channel history of length HB_TAPS-1.
 * @param taps_q15 Half-band filter taps (odd count, odd indices zero except center).
 * @param taps_len Number of taps.
 * @return Number of output floats (decimated complex samples * 2).
 */
static int
hb_decim2_complex_interleaved_ex(const float* DSD_NEO_RESTRICT in, int in_len, float* DSD_NEO_RESTRICT out,
                                 float* DSD_NEO_RESTRICT hist_i, float* DSD_NEO_RESTRICT hist_q,
                                 const float* DSD_NEO_RESTRICT taps_q15, int taps_len) {
    /* Use SIMD-dispatched complex half-band decimator */
    return simd_hb_decim2_complex(in, in_len, out, hist_i, hist_q, taps_q15, taps_len);
}

/**
 * @brief Boxcar low-pass and decimate by step (no wraparound).
 *
 * Length must be a multiple of step.
 *
 * @param signal2 In/out buffer of samples.
 * @param len     Length of input buffer.
 * @param step    Decimation factor.
 * @return New length after decimation.
 */
int
low_pass_simple(float* signal2, int len, int step) {
    int i, i2;
    if (step <= 0) {
        return len;
    }
    for (i = 0; i + (step - 1) < len; i += step) {
        float sum = 0.0f;
        for (i2 = 0; i2 < step; i2++) {
            sum += signal2[i + i2];
        }
        // Normalize by step with rounding. Writes output at i/step index.
        float val = sum / (float)step;
        signal2[i / step] = val;
    }
    /* Duplicate the final sample to provide one-sample lookahead for callers
	   that expect at least one extra element. Only do this when there is
	   capacity (i.e., out_len < len) to avoid writing past the end. */
    int out_len = len / step;
    if (out_len > 0 && out_len < len) {
        signal2[out_len] = signal2[out_len - 1];
    }
    return out_len;
}

/**
 * @brief Simple square window FIR on real samples with decimation to rate_out2.
 *
 * @param s Demodulator state (uses result buffer and decimation state).
 */
void
low_pass_real(struct demod_state* s) {
    int i = 0, i2 = 0;
    float* r = assume_aligned_ptr(s->result, DSD_NEO_ALIGN);
    int fast = (int)s->rate_in;
    int slow = s->rate_out2;
    int decim = (slow != 0) ? (fast / slow) : 1;
    if (decim < 1) {
        decim = 1;
    }
    float recip = 1.0f / (float)decim;
    DSD_NEO_IVDEP
    while (i < s->result_len) {
        s->now_lpr += r[i];
        i++;
        s->prev_lpr_index += slow;
        if (s->prev_lpr_index < fast) {
            continue;
        }
        r[i2] = s->now_lpr * recip;
        s->prev_lpr_index -= fast;
        s->now_lpr = 0.0f;
        i2 += 1;
    }
    s->result_len = i2;
}

/**
 * @brief Perform FM discriminator on interleaved low-passed I/Q to produce audio PCM.
 *
 * Uses the active discriminator configured in fm->discriminator.
 *
 * @param fm Demodulator state (uses lowpassed as input, writes to result).
 */
void
dsd_fm_demod(struct demod_state* fm) {
    const int pairs = fm->lp_len >> 1; /* complex samples */
    if (pairs <= 0) {
        fm->result_len = 0;
        return;
    }

    const float* iq = assume_aligned_ptr(fm->lowpassed, DSD_NEO_ALIGN);
    float* out = assume_aligned_ptr(fm->result, DSD_NEO_ALIGN);

    float prev_r = fm->pre_r;
    float prev_j = fm->pre_j;
    /* Seed history on first use to keep the first phase delta well-defined. */
    if (prev_r == 0.0f && prev_j == 0.0f) {
        prev_r = iq[0];
        prev_j = iq[1];
    }

    for (int n = 0; n < pairs; n++) {
        float cr = iq[(size_t)(n << 1) + 0];
        float cj = iq[(size_t)(n << 1) + 1];
        /* z_n * conj(z_{n-1}) => phase delta; amplitude cancels inside atan2 */
        float re = cr * prev_r + cj * prev_j;
        float im = cj * prev_r - cr * prev_j;
        float angle = atan2f(im, re);
        if (fm->fll_enabled) {
            angle += 0.5f * fm->fll_freq;
        }
        out[n] = angle;
        prev_r = cr;
        prev_j = cj;
    }

    fm->pre_r = prev_r;
    fm->pre_j = prev_j;
    fm->result_len = pairs;
}

/**
 * @brief Pass-through demodulator: copies low-passed samples to output unchanged.
 *
 * @param fm Demodulator state (copies lowpassed to result).
 */
void
raw_demod(struct demod_state* fm) {
    int i;
    for (i = 0; i < fm->lp_len; i++) {
        fm->result[i] = fm->lowpassed[i];
    }
    fm->result_len = fm->lp_len;
}

/**
 * @brief Differential phasor (OP25-style) applied before the Costas loop.
 *
 * Implements GNU Radio's `digital.diff_phasor_cc`: y[n] = x[n] * conj(x[n-1]).
 * Maintains the previous raw complex sample across blocks via
 * cqpsk_diff_prev_r/j. Treats inputs as Q15 to keep output on the same scale
 * as the incoming complex stream.
 *
 * @param d Demodulator state (in-place transform of interleaved I/Q in lowpassed).
 */
static void
cqpsk_diff_phasor(struct demod_state* d) {
    if (!d || !d->lowpassed || d->lp_len < 2) {
        return;
    }
    const int pairs = d->lp_len >> 1;
    float* iq = assume_aligned_ptr(d->lowpassed, DSD_NEO_ALIGN);
    float prev_r = d->cqpsk_diff_prev_r;
    float prev_j = d->cqpsk_diff_prev_j;

    for (int n = 0; n < pairs; n++) {
        float cr = iq[(size_t)(n << 1) + 0];
        float cj = iq[(size_t)(n << 1) + 1];

        float out_r = cr * prev_r + cj * prev_j;
        float out_j = cj * prev_r - cr * prev_j;

        iq[(size_t)(n << 1) + 0] = out_r;
        iq[(size_t)(n << 1) + 1] = out_j;

        prev_r = cr;
        prev_j = cj;
    }

    d->cqpsk_diff_prev_r = prev_r;
    d->cqpsk_diff_prev_j = prev_j;
}

/**
 * @brief QPSK helper demodulator: copy I channel from interleaved complex baseband.
 *
 * Assumes fm->lowpassed holds interleaved I/Q samples that have already passed
 * through CQPSK processing (front-end filtering, optional acquisition FLL/TED, Costas).
 * Produces a single real stream (I only) to feed the legacy symbol sampler path.
 */
/**
 * @brief Phase extractor for CQPSK differential phasors (OP25-compatible).
 *
 * Expects fm->lowpassed to hold symbol-rate CQPSK samples after:
 *   op25_gardner_cc -> op25_diff_phasor_cc -> op25_costas_loop_cc
 * (differenced and carrier-corrected). Converts each complex phasor to a
 * scaled float symbol value:
 *   theta_n = arg(z_n) * (4/pi)
 *
 * The 4/pi scaling (OP25: multiply_const_ff(4.0/pi)) maps CQPSK constellation
 * points at ±45°/±135° to symbol levels ~{-3, -1, +1, +3}, matching OP25's
 * fsk4_slicer_fb input expectation.
 *
 * @param fm Demodulator state (reads interleaved I/Q in lowpassed, writes symbols to result).
 */
void
qpsk_differential_demod(struct demod_state* fm) {
    if (!fm || !fm->lowpassed || fm->lp_len < 2) {
        if (fm) {
            fm->result_len = 0;
        }
        return;
    }

    const int pairs = fm->lp_len >> 1; /* complex samples */
    const float* iq = assume_aligned_ptr(fm->lowpassed, DSD_NEO_ALIGN);
    float* out = assume_aligned_ptr(fm->result, DSD_NEO_ALIGN);

    /* OP25 scaling: 4/pi maps ±pi/4 (±45°) to ±1, ±3*pi/4 (±135°) to ±3 */
    const float k4_over_pi = 4.0f / 3.14159265358979323846f; /* ~1.2732 */

    for (int n = 0; n < pairs; n++) {
        float phase = atan2f(iq[(size_t)(n << 1) + 1], iq[(size_t)(n << 1) + 0]);
        out[n] = phase * k4_over_pi;
    }

    fm->result_len = pairs;
}

/*
 * OP25-style RMS AGC for CQPSK path.
 *
 * Direct port of op25/gr-op25_repeater/apps/rms_agc.py which uses:
 *   rms_agc.rms_agc(alpha=0.45, reference=0.85)
 *
 * The rms_agc.py algorithm (from Daniel Estevez):
 *   1. Compute RMS of input: rms = blocks.rms_cf(alpha)
 *   2. Scale: scaled_rms = rms / reference
 *   3. Add epsilon to avoid division by zero: scaled_rms += 1e-18
 *   4. Divide input by scaled_rms: out = in / scaled_rms
 *
 * The underlying blocks.rms_cf computes:
 *   d_avg = d_beta * d_avg + d_alpha * mag_sqrd
 *   rms = sqrt(d_avg)
 * where d_beta = 1 - d_alpha
 *
 * OP25 parameters: alpha=0.45, reference=0.85
 *
 * This matches op25/gr-op25_repeater/lib/rmsagc_ff_impl.cc exactly:
 *   d_avg = d_beta*d_avg + d_alpha*mag_sqrd;
 *   if (d_avg > 0)
 *       out[i] = d_gain * in[i] / sqrt(d_avg);
 *   else
 *       out[i] = d_gain * in[i];
 *
 * Copyright 2005,2010,2013 Free Software Foundation, Inc.
 * Copyright 2020 Graham J. Norbury, gnorbury@bondcar.com
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
static inline void
cqpsk_rms_agc(struct demod_state* d) {
    if (!d || !d->lowpassed || d->lp_len < 2) {
        return;
    }

    /* OP25 parameters from p25_demodulator.py line 409:
     *   self.agc = rms_agc.rms_agc(0.45, 0.85)
     * where alpha=0.45, reference=0.85 */
    const float alpha = 0.45f;
    const float beta = 1.0f - alpha; /* 0.55 */
    const float gain = 0.85f;        /* reference/k parameter */

    float* iq = d->lowpassed;
    const int pairs = d->lp_len >> 1;

    /* Get running average from state (initialized to 1.0 to avoid startup spike) */
    float avg = d->cqpsk_agc_avg;
    if (avg <= 0.0f) {
        avg = 1.0f; /* OP25 rmsagc_ff_impl.cc: set_alpha sets d_avg = 1.0 */
    }

    /* Process each complex sample exactly as op25's rmsagc_ff_impl.cc:
     *   d_avg = d_beta*d_avg + d_alpha*mag_sqrd;
     *   out = d_gain * in / sqrt(d_avg); */
    for (int i = 0; i < pairs; i++) {
        float I = iq[(size_t)(i << 1)];
        float Q = iq[(size_t)(i << 1) + 1];

        /* mag_sqrd = I*I + Q*Q (complex magnitude squared) */
        float mag_sqrd = I * I + Q * Q;

        /* Update running average: d_avg = d_beta*d_avg + d_alpha*mag_sqrd */
        avg = beta * avg + alpha * mag_sqrd;

        /* Compute gain and apply: out = gain * in / sqrt(avg) */
        if (avg > 0.0f) {
            float scale = gain / sqrtf(avg);
            iq[(size_t)(i << 1)] = I * scale;
            iq[(size_t)(i << 1) + 1] = Q * scale;
        }
        /* else: pass through unchanged (matches op25 behavior) */
    }

    /* Save state for next block */
    d->cqpsk_agc_avg = avg;
}

/**
 * @brief Apply post-demod deemphasis IIR filter.
 *
 * @param fm Demodulator state (reads/writes result, updates deemph_avg).
 */
void
deemph_filter(struct demod_state* fm) {
    float avg = fm->deemph_avg; /* per-instance state */
    float* res = assume_aligned_ptr(fm->result, DSD_NEO_ALIGN);
    float alpha = fm->deemph_a;
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    /* Single-pole IIR: avg += (x - avg) * alpha */
    DSD_NEO_IVDEP
    for (int i = 0; i < fm->result_len; i++) {
        float d = res[i] - avg;
        avg += d * alpha;
        res[i] = avg;
    }
    fm->deemph_avg = avg; /* write back state */
}

/**
 * @brief Apply a simple DC blocking (leaky integrator high-pass) filter to audio.
 *
 * @param fm Demodulator state (reads/writes result, updates dc_avg).
 */
void
dc_block_filter(struct demod_state* fm) {
    /* Leaky integrator high-pass: dc += (x - dc) / 2^k; y = x - dc */
    float dc = fm->dc_avg;
    const int k = 11; /* cutoff ~ Fs / 2^k (k in 10..12) */
    float k_scale = 1.0f / (float)(1 << k);
    float* res = assume_aligned_ptr(fm->result, DSD_NEO_ALIGN);
    DSD_NEO_IVDEP
    for (int i = 0; i < fm->result_len; i++) {
        float x = res[i];
        dc += (x - dc) * k_scale;
        float y = x - dc;
        res[i] = y;
    }
    fm->dc_avg = dc;
}

/**
 * @brief Apply a simple one-pole low-pass filter to audio.
 *
 * @param fm Demodulator state (reads/writes result, updates audio_lpf_state).
 */
void
audio_lpf_filter(struct demod_state* fm) {
    if (!fm->audio_lpf_enable) {
        return;
    }
    float* res = assume_aligned_ptr(fm->result, DSD_NEO_ALIGN);
    float y = fm->audio_lpf_state;
    float alpha = fm->audio_lpf_alpha;
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    DSD_NEO_IVDEP
    for (int i = 0; i < fm->result_len; i++) {
        float x = res[i];
        float d = x - y;
        y += d * alpha;
        res[i] = y;
    }
    fm->audio_lpf_state = y;
}

/**
 * @brief Calculate mean power (squared RMS) with DC bias removed.
 *
 * @param samples Input samples buffer.
 * @param len     Number of samples.
 * @param step    Step size for sampling.
 * @return Mean power (squared RMS) with DC bias removed.
 */
float
mean_power(float* samples, int len, int step) {
    double p = 0.0;
    double t = 0.0;
    for (int i = 0; i < len; i += step) {
        double s = (double)samples[i];
        t += s;
        p += s * s;
    }
    /* DC-corrected energy ≈ p - (t^2)/len */
    double dc_corr = 0.0;
    if (len > 0) {
        dc_corr = (t * t) / (double)len;
    }
    double energy = p - dc_corr;
    if (energy < 0.0) {
        energy = 0.0;
    }
    return (float)(energy / (double)(len > 0 ? len : 1));
}

/**
 * @brief Estimate frequency error using the configured discriminator and update the
 * FLL loop control variables in Q15.
 *
 * Mirrors the modular FLL path used by the RTL front-end.
 *
 * @param d Demodulator state (syncs to/from `fll_state`, updates loop vars).
 */
static void
fll_update_error(struct demod_state* d) {
    if (!d->fll_enabled) {
        return;
    }
    d->fll_state.freq = d->fll_freq;
    d->fll_state.phase = d->fll_phase;
    d->fll_state.prev_r = d->fll_prev_r;
    d->fll_state.prev_j = d->fll_prev_j;

    fll_config_t cfg;
    cfg.enabled = d->fll_enabled;
    cfg.alpha = d->fll_alpha;
    cfg.beta = d->fll_beta;
    cfg.deadband = d->fll_deadband;
    cfg.slew_max = d->fll_slew_max;

    fll_update_error(&cfg, &d->fll_state, d->lowpassed, d->lp_len);

    d->fll_freq = d->fll_state.freq;
    d->fll_phase = d->fll_state.phase;
    d->fll_prev_r = d->fll_state.prev_r;
    d->fll_prev_j = d->fll_state.prev_j;
}

/**
 * @brief Mix low-passed I/Q by the FLL NCO and advance the loop accumulators.
 *
 * Rotates the complex baseband to reduce residual CFO and synchronizes the
 * demod state with the modular FLL implementation.
 *
 * @param d Demodulator state (reads/writes `lowpassed`, updates `fll_state`).
 */
static void
fll_mix_and_update(struct demod_state* d) {
    if (!d->fll_enabled) {
        return;
    }

    /* Sync from demod_state to module state */
    d->fll_state.freq = d->fll_freq;
    d->fll_state.phase = d->fll_phase;
    d->fll_state.prev_r = d->fll_prev_r;
    d->fll_state.prev_j = d->fll_prev_j;

    fll_config_t cfg;
    cfg.enabled = d->fll_enabled;
    cfg.alpha = d->fll_alpha;
    cfg.beta = d->fll_beta;
    cfg.deadband = d->fll_deadband;
    cfg.slew_max = d->fll_slew_max;

    fll_mix_and_update(&cfg, &d->fll_state, d->lowpassed, d->lp_len);

    /* Sync back to demod_state */
    d->fll_freq = d->fll_state.freq;
    d->fll_phase = d->fll_state.phase;
    d->fll_prev_r = d->fll_state.prev_r;
    d->fll_prev_j = d->fll_state.prev_j;
}

/*
 * FM envelope AGC/limiter (per-sample)
 *
 * Normalizes complex I/Q magnitude toward a target RMS to reduce amplitude
 * bounce from low-cost front-ends (e.g., RTL-SDR). Matches the OP25/GNU Radio
 * style of per-sample RMS tracking and gain update (attack/decay), rather than
 * a block-stepped gain. Disabled for CQPSK paths to leave constellation
 * amplitude untouched.
 */
static inline void
fm_envelope_agc(struct demod_state* d) {
    if (!d || !d->fm_agc_enable || d->cqpsk_enable || !d->lowpassed || d->lp_len < 2) {
        return;
    }
    const int pairs = d->lp_len >> 1;
    if (pairs <= 0) {
        return;
    }
    /* Guard: avoid boosting pure noise when input is below configured minimum */
    double acc = 0.0;
    const float* iq_pre = d->lowpassed;
    for (int n = 0; n < pairs; n++) {
        double I = (double)iq_pre[(size_t)(n << 1) + 0];
        double Q = (double)iq_pre[(size_t)(n << 1) + 1];
        acc += I * I + Q * Q;
    }
    if (acc == 0) {
        return;
    }
    double rms_pre = sqrt(acc / (double)pairs);
    float min_rms = (d->fm_agc_min_rms > 0.0f) ? d->fm_agc_min_rms : 0.06f;
    if (rms_pre < (double)min_rms) {
        return;
    }
    float target = (d->fm_agc_target_rms > 0.0f) ? d->fm_agc_target_rms : 0.30f;
    if (target < 0.05f) {
        target = 0.05f;
    }
    if (target > 2.5f) {
        target = 2.5f;
    }
    float alpha_up = (d->fm_agc_alpha_up > 0.0f) ? d->fm_agc_alpha_up : 0.25f;
    float alpha_dn = (d->fm_agc_alpha_down > 0.0f) ? d->fm_agc_alpha_down : 0.75f;
    if (alpha_up < 0.0f) {
        alpha_up = 0.0f;
    }
    if (alpha_up > 1.0f) {
        alpha_up = 1.0f;
    }
    if (alpha_dn < 0.0f) {
        alpha_dn = 0.0f;
    }
    if (alpha_dn > 1.0f) {
        alpha_dn = 1.0f;
    }
    const float kEps = 1e-6f;

    float rms = (float)d->fm_agc_ema_rms;
    if (rms <= 0.0f) {
        rms = target; /* seed estimator near target */
    }
    float rms2 = rms * rms;

    float* out = d->lowpassed;
    float last_g = 1.0f;
    for (int n = 0; n < pairs; n++) {
        float I = (float)out[(size_t)(n << 1)];
        float Q = (float)out[(size_t)(n << 1) + 1];
        float mag2 = I * I + Q * Q;

        /* Faster update when we need to back off gain; slower when ramping up. */
        float alpha = (mag2 > rms2) ? alpha_dn : alpha_up;
        if (alpha < 0.0f) {
            alpha = 0.0f;
        }
        if (alpha > 1.0f) {
            alpha = 1.0f;
        }
        rms2 = (1.0f - alpha) * rms2 + alpha * mag2;
        if (rms2 < 0.0f) {
            rms2 = 0.0f;
        }
        rms = sqrtf(rms2);

        float g = target / (rms + kEps);
        if (g > 8.0f) {
            g = 8.0f;
        }
        if (g < 0.125f) {
            g = 0.125f;
        }
        last_g = g;

        out[(size_t)(n << 1)] = I * g;
        out[(size_t)(n << 1) + 1] = Q * g;
    }
    d->fm_agc_gain = last_g;
    d->fm_agc_ema_rms = (double)rms;
}

/* Optional complex DC blocker prior to FM discrimination (per-sample leaky integrator) */
static inline void
iq_dc_block(struct demod_state* d) {
    if (!d || !d->iq_dc_block_enable || !d->lowpassed || d->lp_len < 2) {
        return;
    }
    int k = d->iq_dc_shift;
    if (k < 6) {
        k = 6;
    }
    if (k > 15) {
        k = 15;
    }
    float* iq = d->lowpassed;
    const int pairs = d->lp_len >> 1;
    float dcI = d->iq_dc_avg_r;
    float dcQ = d->iq_dc_avg_i;
    float alpha = 1.0f / (float)(1 << k);
    for (int n = 0; n < pairs; n++) {
        float I = iq[(size_t)(n << 1) + 0];
        float Q = iq[(size_t)(n << 1) + 1];
        /* Update leaky integrator toward current sample */
        dcI += (I - dcI) * alpha;
        dcQ += (Q - dcQ) * alpha;
        float yI = I - dcI;
        float yQ = Q - dcQ;
        iq[(size_t)(n << 1) + 0] = yI;
        iq[(size_t)(n << 1) + 1] = yQ;
    }
    d->iq_dc_avg_r = dcI;
    d->iq_dc_avg_i = dcQ;
}

/* Optional constant-envelope limiter: per-sample normalization around target magnitude */
static inline void
fm_constant_envelope_limiter(struct demod_state* d) {
    if (!d || !d->fm_limiter_enable || d->cqpsk_enable || !d->lowpassed || d->lp_len < 2) {
        return;
    }
    float target = (d->fm_agc_target_rms > 0.0f) ? d->fm_agc_target_rms : 0.30f;
    if (target < 0.05f) {
        target = 0.05f;
    }
    if (target > 2.5f) {
        target = 2.5f;
    }
    const float t2 = target * target;
    const float lo2 = t2 * 0.25f; /* 0.5^2 */
    const float hi2 = t2 * 4.0f;  /* 2.0^2 */
    float* iq = d->lowpassed;
    const int pairs = d->lp_len >> 1;
    for (int n = 0; n < pairs; n++) {
        float I = iq[(size_t)(n << 1) + 0];
        float Q = iq[(size_t)(n << 1) + 1];
        float m2 = I * I + Q * Q;
        if (m2 <= 0.0f) {
            continue;
        }
        if (m2 >= lo2 && m2 <= hi2) {
            /* within tolerance, leave unchanged */
            continue;
        }
        double mag = sqrt((double)m2);
        if (mag < 1e-6) {
            continue;
        }
        double g = (double)target / mag;
        if (g > 8.0) {
            g = 8.0;
        }
        if (g < 0.125) {
            g = 0.125;
        }
        float yI = (float)((double)I * g);
        float yQ = (float)((double)Q * g);
        iq[(size_t)(n << 1) + 0] = yI;
        iq[(size_t)(n << 1) + 1] = yQ;
    }
}

/**
 * @brief Apply a lightweight Gardner timing correction to complex baseband.
 *
 * When enabled, this may adjust `lowpassed` and `lp_len` via the modular TED API.
 * Intended primarily for digital modes; typically disabled for analog FM.
 *
 * @param d Demodulator state (syncs `ted_state`, may modify samples/length).
 */
static void
gardner_timing_adjust(struct demod_state* d) {
    if (!d->ted_enabled || d->ted_sps <= 1) {
        return;
    }

    /* Sync from demod_state to module state */
    d->ted_state.mu = d->ted_mu;

    ted_config_t cfg = {}; /* Zero-initialize for safety if struct grows */
    cfg.enabled = d->ted_enabled;
    cfg.force = d->ted_force;
    cfg.sps = d->ted_sps;
    /* Map runtime ted_gain to loop gain parameter.
     * If ted_gain > 0, use it; otherwise let ted.cpp use defaults. */
    cfg.gain_mu = d->ted_gain;
    cfg.gain_omega = 0.0f; /* 0 = use defaults (0.1 * gain_mu^2) */
    cfg.omega_rel = 0.0f;  /* 0 = use defaults (0.002) */

    /* For CQPSK paths, use OP25-compatible decimating Gardner; for FM/C4FM
       paths, use the legacy non-decimating Farrow-based TED to preserve the
       expected sample-rate interface for downstream processing. */
    if (d->cqpsk_enable) {
        gardner_timing_adjust(&cfg, &d->ted_state, d->lowpassed, &d->lp_len, d->timing_buf);
    } else {
        gardner_timing_adjust_farrow(&cfg, &d->ted_state, d->lowpassed, &d->lp_len, d->timing_buf);
    }

    /* Sync back to demod_state */
    d->ted_mu = d->ted_state.mu;

    /* Debug: TED state when DSD_NEO_DEBUG_CQPSK=1 */
    {
        static int call_count = 0;
        if (debug_cqpsk_enabled() && d->cqpsk_enable && (++call_count % 50) == 0) {
            float lock_norm =
                (d->ted_state.lock_count > 0) ? d->ted_state.lock_accum / (float)d->ted_state.lock_count : 0.0f;
            fprintf(stderr, "[TED] omega:%.3f mu:%.3f e_ema:%.4f lock:%.2f in:%d out:%d\n", d->ted_state.omega,
                    d->ted_state.mu, d->ted_state.e_ema, lock_norm, d->lp_len, d->lp_len);
        }
    }
}

/**
 * @brief Full demodulation pipeline for one block.
 *
 * Applies decimation via half-band cascade, optional FLL and timing
 * correction, followed by the configured discriminator and post-processing.
 *
 * @param d Demodulator state (consumes lowpassed, produces result).
 */
void
full_demod(struct demod_state* d) {
    int i, ds_p;
    ds_p = d->downsample_passes;
    if (ds_p > 0) {
        /* Apply ds_p stages of 2:1 half-band decimation on interleaved lowpassed */
        int in_len = d->lp_len;
        float* src = d->lowpassed;
        float* dst = d->hb_workbuf;
        for (i = 0; i < ds_p; i++) {
            /* Stage-aware HB selection: heavier early, light later */
            const float* taps = hb_q15_taps;
            int taps_len = HB_TAPS;
            if (i == 0) {
                taps = hb31_q15_taps;
                taps_len = 31;
            }
            /* Fused complex HB decimation on interleaved I/Q */
            int out_len_interleaved =
                hb_decim2_complex_interleaved_ex(src, in_len, dst, d->hb_hist_i[i], d->hb_hist_q[i], taps, taps_len);
            /* Next stage */
            src = dst;
            in_len = out_len_interleaved;
            dst = (src == d->hb_workbuf) ? d->lowpassed : d->hb_workbuf;
        }
        /* Final output resides in 'src' with length in_len; consume in-place (no copy) */
        d->lowpassed = src;
        d->lp_len = in_len;
    }
    /* Bound channel noise when running at higher Fs (24 kHz default) */
    channel_lpf_apply(d);
    /* Update channel power measurement (post-channel-filter) for UI/squelch */
    if (d->lowpassed && d->lp_len >= 2) {
        int n = d->lp_len;
        if (n > 512) {
            n = 512;
        }
        d->channel_pwr = mean_power(d->lowpassed, n, 1);
    }
    /* Channel-based squelch: if power below threshold, zero buffer but continue processing.
     * Industry standard: DSP pipeline always flows at constant rate. Squelch is a metadata
     * flag, not a flow stopper. This keeps downstream consumers (frame sync, UI updates)
     * running smoothly even when squelched. Decoders will simply fail to find sync on zeros. */
    if (d->lowpassed && d->lp_len > 0 && d->channel_squelch_level > 0.0f && d->channel_pwr < d->channel_squelch_level) {
        d->channel_squelched = 1;
        d->squelch_gate_open = 0;
        /* Zero the buffer so downstream sees silence */
        for (int k = 0; k < d->lp_len; k++) {
            d->lowpassed[k] = 0.0f;
        }
        /* Continue processing with zeroed buffer - don't return early.
         * This maintains continuous sample flow for UI responsiveness. */
    } else {
        d->channel_squelched = 0;
        d->squelch_gate_open = 1;
    }
    /* Branch early by mode to simplify ordering. */
    if (d->cqpsk_enable) {
        /*
         * OP25-aligned CQPSK signal chain:
         *   AGC -> FLL band-edge -> Gardner (timing, decimating) -> diff_phasor -> Costas (carrier)
         *
         * Implemented by cqpsk_rms_agc(), op25_fll_band_edge_cc(), op25_gardner_cc(),
         * op25_diff_phasor_cc(), op25_costas_loop_cc().
         */

        /* Fast path when squelched: skip expensive DSP but produce zero symbols to keep
         * the pipeline flowing for UI responsiveness. The expensive AGC/FLL/TED/Costas
         * processing is pointless on a zeroed buffer. */
        if (d->channel_squelched) {
            /* Estimate output symbol count: input IQ pairs / samples_per_symbol.
             * TED decimates from sample rate to symbol rate. */
            int in_pairs = d->lp_len >> 1;
            int sps = (d->ted_sps > 0) ? d->ted_sps : 5;
            int out_syms = (in_pairs + sps - 1) / sps; /* ceiling division */
            if (out_syms < 1) {
                out_syms = 1;
            }
            if (out_syms > MAXIMUM_BUF_LENGTH) {
                out_syms = MAXIMUM_BUF_LENGTH;
            }
            /* Produce zero symbols directly */
            for (int k = 0; k < out_syms; k++) {
                d->result[k] = 0.0f;
            }
            d->result_len = out_syms;
            return; /* Skip all CQPSK DSP and go straight to output */
        }

        /* OP25: rms_agc.rms_agc(0.45, 0.85)
         * RMS AGC normalizes amplitude using running RMS estimate. */
        cqpsk_rms_agc(d);

        /* Debug: Post-AGC magnitudes when DSD_NEO_DEBUG_CQPSK=1 */
        {
            static int call_count = 0;
            if (debug_cqpsk_enabled() && (++call_count % 50) == 0 && d->lp_len >= 8) {
                const float* iq = d->lowpassed;
                float mag_sum = 0.0f;
                float max_env = 0.0f;
                int pairs = d->lp_len >> 1;
                for (int k = 0; k < pairs && k < 100; k++) {
                    float I = iq[(k << 1)];
                    float Q = iq[(k << 1) + 1];
                    float mag = sqrtf(I * I + Q * Q);
                    mag_sum += mag;
                    if (mag > max_env) {
                        max_env = mag;
                    }
                }
                float avg_mag = mag_sum / (pairs < 100 ? pairs : 100);
                fprintf(stderr, "[POST-AGC] avg_mag:%.3f max_env:%.3f samples:%d\n", avg_mag, max_env, d->lp_len / 2);
            }
        }

        /*
         * OP25-aligned CQPSK signal chain (matches p25_demodulator_dev.py line 486):
         *
         *   AGC -> FLL -> Gardner (timing) -> diff_phasor -> Costas (carrier)
         *
         * This is the CORRECT OP25 flow where:
         *   0. FLL band-edge does coarse frequency acquisition (BEFORE timing recovery)
         *   1. Gardner does ONLY timing recovery (no NCO rotation)
         *   2. diff_phasor is applied at symbol rate after Gardner
         *   3. Costas loop operates at symbol rate on differential symbols
         *
         * Key differences from old combined block:
         *   - FLL for coarse frequency correction before timing recovery
         *   - No NCO rotation in Gardner
         *   - Costas operates at symbol rate (not sample rate)
         *   - Costas uses loop_bw=0.008, computed alpha≈0.0223, beta≈0.000253
         *   - Phase limited to ±π/2 (not wrapped)
         *
         * Skip during unit tests that use raw_demod.
         */
        if (d->mode_demod != &raw_demod) {
            int pre_len = d->lp_len;

            /* 0. FLL band-edge frequency acquisition (OP25's fll block)
             *    This corrects coarse frequency offset BEFORE timing recovery.
             *    Critical for initial acquisition after channel retunes.
             *    From p25_demodulator_dev.py line 403:
             *      self.fll = digital.fll_band_edge_cc(sps, excess_bw, 2*sps+1, TWO_PI/sps/350)
             */
            op25_fll_band_edge_cc(d);

            /* 1. Gardner timing recovery (OP25's clock block)
             *    Output: symbol-rate samples, NOT carrier corrected */
            op25_gardner_cc(d);

            /* 2. Differential phasor (OP25's diffdec block)
             *    Output: differential phase symbols */
            op25_diff_phasor_cc(d);

            /* 3. Costas carrier recovery at symbol rate (OP25's costas block)
             *    Output: carrier-corrected differential symbols */
            op25_costas_loop_cc(d);

            /* Debug: Post-processing state when DSD_NEO_DEBUG_CQPSK=1 */
            {
                static int call_count = 0;
                if (debug_cqpsk_enabled() && (++call_count % 50) == 0) {
                    dsd_costas_loop_state_t* c = &d->costas_state;
                    dsd_fll_band_edge_state_t* f = &d->fll_band_edge_state;
                    ted_state_t* ted = &d->ted_state;
                    const float kTwoPi = 6.28318530717958647692f;
                    /* FLL freq at sample rate */
                    float fll_freq_hz = f->freq * ((float)d->rate_out / kTwoPi);
                    /* Costas freq at symbol rate (not sample rate) */
                    int sym_rate = (d->ted_sps > 0 && d->rate_out > 0) ? (d->rate_out / d->ted_sps) : 4800;
                    float costas_freq_hz = c->freq * ((float)sym_rate / kTwoPi);
                    fprintf(stderr, "[OP25] in:%d out:%d omega:%.3f fll_freq:%.1fHz costas_freq:%.1fHz phase:%.3f\n",
                            pre_len / 2, d->lp_len / 2, ted->omega, fll_freq_hz, costas_freq_hz, c->phase);
                    /* Log IQ constellation: first few symbols to check positioning */
                    if (d->lp_len >= 8) {
                        const float* iq = d->lowpassed;
                        fprintf(stderr, "[IQ] ");
                        for (int k = 0; k < 4 && k < (d->lp_len >> 1); k++) {
                            float I = iq[(k << 1)];
                            float Q = iq[(k << 1) + 1];
                            float phase_deg = atan2f(Q, I) * 57.2957795f;
                            float mag = sqrtf(I * I + Q * Q);
                            fprintf(stderr, "(%.2f,%.2f|%.0f°,%.2f) ", I, Q, phase_deg, mag);
                        }
                        fprintf(stderr, "\n");
                    }
                }
            }
        } else {
            /* For raw_demod tests, just do differential decoding without Costas */
            cqpsk_diff_phasor(d);
        }
    } else {
        /* Fast path when squelched: skip expensive DSP for FM/C4FM but keep samples flowing.
         * The buffer is already zeroed, so just skip conditioning and let the demod produce zeros. */
        if (d->channel_squelched) {
            /* Skip all conditioning - zeros don't need DC block, AGC, limiter, or FLL.
             * Fall through to mode_demod which will produce zero output. */
        } else {
            /* Baseband conditioning order (FM/C4FM):
               1) Remove DC offset on I/Q to avoid biasing AGC and discriminator
               2) Block-based envelope AGC to normalize |z|
               3) Optional per-sample limiter to clamp fast AM ripple */
            iq_dc_block(d);
            /* Avoid running both AGC and limiter simultaneously to reduce gain "pumping". */
            if (d->fm_agc_enable) {
                fm_envelope_agc(d);
            } else if (d->fm_limiter_enable) {
                fm_constant_envelope_limiter(d);
            }
            /* Residual-CFO FLL when enabled */
            if (d->fll_enabled) {
                /* Update control only when squelch indicates a carrier; always apply rotation. */
                if (d->squelch_gate_open) {
                    fll_update_error(d);
                }
                fll_mix_and_update(d);
            }
        }
    }
    /* Mode-aware generic IQ balance (image suppression) after CFO rotation.
     * Skip when squelched - zeros don't benefit from IQ correction. */
    if (d->iqbal_enable && !d->cqpsk_enable && !d->channel_squelched && d->lowpassed && d->lp_len >= 2) {
        /* Estimate s2 = E[z^2], p2 = E[|z|^2] over this block */
        double s2r = 0.0, s2i = 0.0, p2 = 0.0;
        int N = d->lp_len >> 1; /* complex pairs */
        const float* iq = d->lowpassed;
        for (int n = 0; n < N; n++) {
            double I = (double)iq[(size_t)(n << 1) + 0];
            double Q = (double)iq[(size_t)(n << 1) + 1];
            s2r += I * I - Q * Q;
            s2i += 2.0 * I * Q;
            p2 += I * I + Q * Q;
        }
        if (p2 <= 1e-9) {
            p2 = 1e-9;
        }
        float ar = (float)(s2r / p2);
        float ai = (float)(s2i / p2);
        float ema_alpha = d->iqbal_alpha_ema_a > 0.0f ? d->iqbal_alpha_ema_a : 0.2f;
        /* EMA update: er += alpha * (ar - er) */
        float er = d->iqbal_alpha_ema_r;
        float ei = d->iqbal_alpha_ema_i;
        er += ema_alpha * (ar - er);
        ei += ema_alpha * (ai - ei);
        d->iqbal_alpha_ema_r = er;
        d->iqbal_alpha_ema_i = ei;
        float thr = d->iqbal_thr > 0.0f ? d->iqbal_thr : 0.02f;
        float mag2 = er * er + ei * ei;
        float thr2 = thr * thr;
        if (mag2 >= thr2) {
            float* out = d->lowpassed;
            for (int n = 0; n < N; n++) {
                float I = out[(size_t)(n << 1) + 0];
                float Q = out[(size_t)(n << 1) + 1];
                float tI = er * I + ei * Q;
                float tQ = -er * Q + ei * I;
                float yI = I - tI;
                float yQ = Q - tQ;
                out[(size_t)(n << 1) + 0] = yI;
                out[(size_t)(n << 1) + 1] = yQ;
            }
        }
    }
    /* Apply Gardner TED for non-CQPSK paths (e.g., C4FM) using the legacy
       non-decimating Farrow-based implementation so that FM/C4FM downstream
       stages continue to see sample-rate complex baseband. Requires integer
       SPS; analog FM remains excluded unless explicitly forced.
       Skip when squelched - timing recovery on zeros is pointless. */
    if (d->ted_enabled && !d->cqpsk_enable && !d->channel_squelched && d->sps_is_integer
        && (d->mode_demod != &dsd_fm_demod || d->ted_force)) {
        gardner_timing_adjust(d);
    }
    /*
     * For CQPSK, produce a single real stream of differential phase symbols
     * (arg(z_n), where z_n was differenced pre-Costas) to feed the legacy
     * symbol sampler instead of FM discriminating. For other paths, use the
     * configured demodulator.
     */
    if (d->cqpsk_enable) {
        qpsk_differential_demod(d);
        /* Debug: Symbol histogram when DSD_NEO_DEBUG_CQPSK=1 */
        {
            static unsigned int call_count = 0; /* unsigned to avoid signed overflow UB */
            /* Accumulate histogram over multiple blocks for meaningful statistics */
            static int hist_p3 = 0, hist_p1 = 0, hist_m1 = 0, hist_m3 = 0, hist_other = 0;
            static int hist_samples = 0;
            /* Track EVM/SNR across the same window to catch slicer health issues */
            static double evm_err_acc = 0.0;
            static double evm_ref_acc = 0.0;
            static int evm_count = 0;
            if (debug_cqpsk_enabled()) {
                const float* syms = d->result;
                for (int k = 0; k < d->result_len; k++) {
                    float s = syms[k];
                    float ideal;
                    if (s > 2.0f) {
                        hist_p3++;
                        ideal = 3.0f;
                    } else if (s > 0.0f) {
                        hist_p1++;
                        ideal = 1.0f;
                    } else if (s > -2.0f) {
                        hist_m1++;
                        ideal = -1.0f;
                    } else {
                        hist_m3++;
                        ideal = -3.0f;
                    }
                    hist_samples++;
                    double err = (double)s - (double)ideal;
                    evm_err_acc += err * err;
                    evm_ref_acc += (double)ideal * (double)ideal;
                    evm_count++;
                }
                if ((++call_count % 25) == 0 && hist_samples > 0) {
                    float total = (float)hist_samples;
                    fprintf(stderr, "[SYM] +3:%.1f%% +1:%.1f%% -1:%.1f%% -3:%.1f%% (n=%d)\n", 100.0f * hist_p3 / total,
                            100.0f * hist_p1 / total, 100.0f * hist_m1 / total, 100.0f * hist_m3 / total, hist_samples);
                    if (evm_count > 0 && evm_ref_acc > 1e-9) {
                        double mse = evm_err_acc / (double)evm_count;
                        double ref_pwr = evm_ref_acc / (double)evm_count;
                        double evm_rms = sqrt(mse);
                        double ref_rms = sqrt(ref_pwr);
                        double evm_pct = (ref_rms > 1e-9) ? (evm_rms / ref_rms) * 100.0 : 0.0;
                        /* Apply dynamic bias correction for consistency with main SNR display */
                        double bias = dsd_rtl_stream_metrics_hook_snr_bias_evm();
                        double snr_db = (mse > 1e-12) ? 10.0 * log10(ref_pwr / mse) - bias : 99.0;
                        fprintf(stderr, "[CQPSK] EVM:%.2f%% SNR:%.1f dB ref_rms:%.2f n:%d\n", evm_pct, snr_db, ref_rms,
                                evm_count);
                    }
                    /* Reset histogram */
                    hist_p3 = hist_p1 = hist_m1 = hist_m3 = hist_other = 0;
                    hist_samples = 0;
                    evm_err_acc = 0.0;
                    evm_ref_acc = 0.0;
                    evm_count = 0;
                }
            }
        }
    } else {
        d->mode_demod(d); /* lowpassed -> result */
    }
    if (d->mode_demod == &raw_demod) {
        return;
    }
    /* todo, fm noise squelch */
    // use nicer filter here too?
    if (!d->cqpsk_enable && d->post_downsample > 1) {
        int decim = d->post_downsample;
        if (decim > 2) {
            /* Higher-quality polyphase decimator for larger factors */
            audio_polydecim_ensure(d, decim);
            int out_n = audio_polydecim_process(d, d->result, d->result_len, d->timing_buf);
            if (out_n > 0) {
                memcpy(d->result, d->timing_buf, (size_t)out_n * sizeof(float));
                d->result_len = out_n;
            }
        } else {
            /* Pre-filter with one-pole and simple decimation for small factor */
            int Fs = (d->rate_out > 0) ? d->rate_out : 48000;
            double fc = 0.2 * ((double)Fs / (double)decim);
            if (fc < 50.0) {
                fc = 50.0;
            }
            double a = 1.0 - exp(-2.0 * 3.14159265358979323846 * fc / (double)Fs);
            if (a < 0.0) {
                a = 0.0;
            }
            if (a > 1.0) {
                a = 1.0;
            }
            float alpha_f = (float)a;
            if (alpha_f < 0.0f) {
                alpha_f = 0.0f;
            }
            if (alpha_f > 1.0f) {
                alpha_f = 1.0f;
            }
            float y = (d->result_len > 0) ? d->result[0] : 0.0f;
            for (int k = 0; k < d->result_len; k++) {
                float x = d->result[k];
                float dlt = x - y;
                y += dlt * alpha_f;
                d->result[k] = y;
            }
            d->result_len = low_pass_simple(d->result, d->result_len, decim);
        }
    }
    if (!d->cqpsk_enable) {
        if (d->deemph) {
            deemph_filter(d);
        }
        /* Optional post-demod audio LPF */
        audio_lpf_filter(d);
        if (d->dc_block) {
            dc_block_filter(d);
        }
    }
    /* Skip post-demod audio decimator for CQPSK symbol streams. */
    if (!d->cqpsk_enable && d->rate_out2 > 0) {
        low_pass_real(d);
        //arbitrary_resample(d->result, d->result, d->result_len, d->result_len * d->rate_out2 / d->rate_out);
    }

    /* Apply soft squelch envelope on audio (skip for CQPSK symbol stream) */
    if (!d->cqpsk_enable) {
        float env = d->squelch_env;
        float target = d->squelch_gate_open ? 1.0f : 0.0f;
        float alpha = d->squelch_gate_open ? (d->squelch_env_attack > 0.0f ? d->squelch_env_attack : 0.125f)
                                           : (d->squelch_env_release > 0.0f ? d->squelch_env_release : 0.03125f);
        float err = target - env;
        env += alpha * err;
        if (env < 0.0f) {
            env = 0.0f;
        }
        if (env > 1.0f) {
            env = 1.0f;
        }
        d->squelch_env = env;
        /* Multiply audio by envelope */
        float* res = d->result;
        for (int k = 0; k < d->result_len; k++) {
            res[k] = res[k] * env;
        }
    }
}
