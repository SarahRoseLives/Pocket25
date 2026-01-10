// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief SIMD FIR filter dispatch API with runtime CPU detection.
 *
 * Provides vectorized implementations of FIR filter hot paths:
 * - Complex half-band decimator (exploits zero-tap sparsity + symmetry)
 * - Complex general symmetric FIR (exploits symmetry only)
 * - Real half-band decimator (exploits zero-tap sparsity + symmetry)
 *
 * Runtime dispatch automatically selects the best available implementation:
 * - x86-64: AVX2+FMA > SSE2 > scalar
 * - ARM64: NEON (always available)
 * - Other: scalar fallback
 */

#ifndef DSD_NEO_SIMD_FIR_H
#define DSD_NEO_SIMD_FIR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Complex interleaved general symmetric FIR filter (no decimation).
 * Used for channel_lpf_apply() - 63-tap filters with non-zero odd taps.
 * Exploits tap symmetry (fold pairs) but NOT zero-tap skipping.
 *
 * @param in       Input interleaved I/Q samples.
 * @param in_len   Input length in floats (num_pairs * 2).
 * @param out      Output interleaved I/Q samples.
 * @param hist_i   I history buffer (taps_len - 1 elements).
 * @param hist_q   Q history buffer (taps_len - 1 elements).
 * @param taps     Symmetric FIR taps (odd count).
 * @param taps_len Number of taps (must be odd).
 */
void simd_fir_complex_apply(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
                            int taps_len);

/**
 * Complex half-band decimator by 2.
 * Exploits zero-valued odd taps in half-band filters AND tap symmetry.
 * API matches hb_decim2_complex_interleaved_ex() exactly.
 *
 * @param in       Input interleaved I/Q samples.
 * @param in_len   Input length in floats (num_pairs * 2).
 * @param out      Output interleaved I/Q samples (decimated).
 * @param hist_i   I history buffer (taps_len - 1 elements).
 * @param hist_q   Q history buffer (taps_len - 1 elements).
 * @param taps     Half-band taps (odd count, odd indices zero except center).
 * @param taps_len Number of taps (15, 23, or 31).
 * @return Output length in floats.
 */
int simd_hb_decim2_complex(const float* in, int in_len, float* out, float* hist_i, float* hist_q, const float* taps,
                           int taps_len);

/**
 * Real half-band decimator by 2.
 * API matches hb_decim2_real() exactly.
 *
 * @param in       Real input samples.
 * @param in_len   Number of input samples.
 * @param out      Decimated output (in_len / 2 samples).
 * @param hist     History buffer (taps_len - 1 elements).
 * @param taps     Half-band taps (odd count).
 * @param taps_len Number of taps (15, 23, or 31).
 * @return Number of output samples (in_len / 2).
 */
int simd_hb_decim2_real(const float* in, int in_len, float* out, float* hist, const float* taps, int taps_len);

/**
 * Query the active SIMD implementation name for debugging.
 * @return "scalar", "sse2", "avx2", or "neon".
 */
const char* simd_fir_get_impl_name(void);

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_SIMD_FIR_H */
