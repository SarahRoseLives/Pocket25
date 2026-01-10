// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Half-band FIR decimator implementation for real sequences.
 *
 * Implements float half-band decimation by 2 with persistent history.
 */

#include <stdint.h>
#include <string.h>

#include <dsd-neo/dsp/halfband.h>
#include <dsd-neo/dsp/simd_fir.h>

/* Half-band coefficients normalized to unity gain (see declaration in header) */
const float hb_q15_taps[HB_TAPS] = {-108.0f / 32768.0f, 0.0f, 1800.0f / 32768.0f, 0.0f, -500.0f / 32768.0f, 0.0f,
                                    7000.0f / 32768.0f, 0.5f, 7000.0f / 32768.0f, 0.0f, -500.0f / 32768.0f, 0.0f,
                                    1800.0f / 32768.0f, 0.0f, -108.0f / 32768.0f};

/*
 * Higher-order half-band coefficients generated via windowed-sinc (Blackman)
 * and renormalized so that the center tap is exactly 0.5 and the sum equals 1.
 * Odd-indexed taps are zero except the center.
 */
const float hb31_q15_taps[31] = {0.0f,
                                 0.0f,
                                 13.0f / 32768.0f,
                                 0.0f,
                                 -73.0f / 32768.0f,
                                 0.0f,
                                 233.0f / 32768.0f,
                                 0.0f,
                                 -587.0f / 32768.0f,
                                 0.0f,
                                 1314.0f / 32768.0f,
                                 0.0f,
                                 -2953.0f / 32768.0f,
                                 0.0f,
                                 10244.0f / 32768.0f,
                                 16386.0f / 32768.0f,
                                 10244.0f / 32768.0f,
                                 0.0f,
                                 -2953.0f / 32768.0f,
                                 0.0f,
                                 1314.0f / 32768.0f,
                                 0.0f,
                                 -587.0f / 32768.0f,
                                 0.0f,
                                 233.0f / 32768.0f,
                                 0.0f,
                                 -73.0f / 32768.0f,
                                 0.0f,
                                 13.0f / 32768.0f,
                                 0.0f,
                                 0.0f};

const float hb23_q15_taps[23] = {0.0f,
                                 0.0f,
                                 38.0f / 32768.0f,
                                 0.0f,
                                 -238.0f / 32768.0f,
                                 0.0f,
                                 865.0f / 32768.0f,
                                 0.0f,
                                 -2559.0f / 32768.0f,
                                 0.0f,
                                 10087.0f / 32768.0f,
                                 16382.0f / 32768.0f,
                                 10087.0f / 32768.0f,
                                 0.0f,
                                 -2559.0f / 32768.0f,
                                 0.0f,
                                 865.0f / 32768.0f,
                                 0.0f,
                                 -238.0f / 32768.0f,
                                 0.0f,
                                 38.0f / 32768.0f,
                                 0.0f,
                                 0.0f};

/**
 * @brief Decimate one real-valued channel by 2 using a half-band FIR.
 *
 * Applies a 15-tap half-band low-pass to input samples and writes every second
 * filtered sample to the output. Maintains a left-wing history across calls to
 * preserve continuity at block boundaries. Uses SIMD-dispatched implementation.
 *
 * @param in Pointer to real input samples (length in_len).
 * @param in_len Number of input samples.
 * @param out Output buffer, size must be at least in_len/2.
 * @param hist Persistent history of length HB_TAPS-1 (left wing).
 * @return Number of output samples written (in_len/2).
 */
int
hb_decim2_real(const float* in, int in_len, float* out, float* hist) {
    /* Use SIMD-dispatched real half-band decimator */
    return simd_hb_decim2_real(in, in_len, out, hist, hb_q15_taps, HB_TAPS);
}
