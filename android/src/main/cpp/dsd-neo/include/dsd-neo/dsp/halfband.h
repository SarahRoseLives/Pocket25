// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Half-band decimation filter definitions (float).
 *
 * Declares FIR coefficients and the decimate-by-2 function with persistent
 * history for real-valued sequences.
 */

#pragma once

#include <stdint.h>

/**
 * Number of taps for the half-band FIR low-pass filter.
 */
#define HB_TAPS 15

/* Maximum half-band tap count supported by the complex decimator history. */
#ifndef HB_TAPS_MAX
#define HB_TAPS_MAX 31
#endif

/**
 * Symmetric half-band coefficients normalized to unity DC gain.
 *
 * Odd-indexed taps are zero; the center tap is 0.5. The remaining even
 * taps sum to 0.5 to yield unity DC gain.
 */
extern const float hb_q15_taps[HB_TAPS];

/* Optional higher-order half-band prototypes (Q15). */
extern const float hb31_q15_taps[31];
extern const float hb23_q15_taps[23];

/**
 * Decimate a real-valued sequence by 2 using a half-band FIR.
 *
 * Applies a 15-tap half-band low-pass to input samples and writes every second
 * filtered sample to the output. Maintains a left-wing history across calls to
 * preserve continuity at block boundaries.
 *
 * @param in     Pointer to real input samples (length in_len).
 * @param in_len Number of input samples.
 * @param out    Output buffer, size must be at least in_len/2.
 * @param hist   Persistent history of length HB_TAPS-1 (left wing).
 * @return Number of output samples written (in_len/2).
 */
int hb_decim2_real(const float* in, int in_len, float* out, float* hist);
