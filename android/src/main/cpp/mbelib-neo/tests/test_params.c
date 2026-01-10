// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Targeted parameter-derivation tests for IMBE/AMBE paths.
 */

#include <assert.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <string.h>

#include "mbelib-neo/mbelib.h"

/**
 * @brief Zero-initialize a bit array of length n.
 * @param bits Pointer to bit array (0/1 chars) to clear.
 * @param n    Number of entries to set to zero.
 */
static void
set_bits_zero(char* bits, int n) {
    memset(bits, 0, (size_t)n);
}

/**
 * @brief Compose IMBE 7200x4400 b0 into the imbe_d vector.
 *
 * Matches the extraction in `mbe_decodeImbe4400Parms`, where b0 is formed
 * from `imbe_d[0..5], imbe_d[85], imbe_d[86]` (MSB-first).
 *
 * @param imbe_d IMBE parameter bit vector (88 entries), modified in-place.
 * @param b0     8-bit value to encode into the proper positions.
 */
static void
set_imbe7200_b0(char imbe_d[88], int b0) {
    for (int i = 0; i < 8; ++i) {
        int bit = (b0 >> (7 - i)) & 1; // MSB-first
        if (i < 6) {
            imbe_d[i] = (char)bit;
        } else if (i == 6) {
            imbe_d[85] = (char)bit;
        } else { // i == 7
            imbe_d[86] = (char)bit;
        }
    }
}

/**
 * @brief Compose AMBE 3600x2450 b0 into the ambe_d vector.
 *
 * Matches the extraction in `mbe_decodeAmbe2450Parms`, mapping
 * `b0 = d0<<6 | d1<<5 | d2<<4 | d3<<3 | d37<<2 | d38<<1 | d39`.
 *
 * @param ambe_d AMBE parameter bit vector (49 entries), modified in-place.
 * @param b0     7-bit value to encode into the proper positions.
 */
static void
set_ambe2450_b0(char ambe_d[49], int b0) {
    ambe_d[0] = (char)((b0 >> 6) & 1);
    ambe_d[1] = (char)((b0 >> 5) & 1);
    ambe_d[2] = (char)((b0 >> 4) & 1);
    ambe_d[3] = (char)((b0 >> 3) & 1);
    ambe_d[37] = (char)((b0 >> 2) & 1);
    ambe_d[38] = (char)((b0 >> 1) & 1);
    ambe_d[39] = (char)((b0 >> 0) & 1);
}

/**
 * @brief Compare two floats for approximate equality.
 * @param a   First value.
 * @param b   Second value.
 * @param eps Absolute tolerance.
 * @return 1 if |a - b| <= eps, else 0.
 */
static int
approx_equal(float a, float b, float eps) {
    float diff = a - b;
    if (diff < 0) {
        diff = -diff;
    }
    return diff <= eps;
}

/**
 * @brief Test entry: IMBE and AMBE parameter derivation spot checks.
 */
int
main(void) {
    // IMBE 7200x4400: verify w0/L/K from b0
    {
        char imbe_d[88];
        mbe_parms cur = {0}, prev = {0};
        mbe_parms dummy;
        mbe_initMbeParms(&cur, &prev, &dummy);

        set_bits_zero(imbe_d, 88);
        // Pick a few valid b0 values well inside range to avoid edge cases
        int b0_values[] = {0, 100, 206};
        for (unsigned i = 0; i < sizeof(b0_values) / sizeof(b0_values[0]); ++i) {
            set_bits_zero(imbe_d, 88);
            set_imbe7200_b0(imbe_d, b0_values[i]);
            int rc = mbe_decodeImbe4400Parms(imbe_d, &cur, &prev);
            // valid voice frame expected
            assert(rc == 0);
            (void)rc; /* Suppress unused-but-set warning when asserts disabled */
            float w0_expected = (float)((4.0 * M_PI) / ((double)b0_values[i] + 39.5));
            int L_expected = (int)(0.9254 * (int)((M_PI / w0_expected) + 0.25));
            int K_expected = (L_expected < 37) ? ((L_expected + 2) / 3) : 12;
            assert(approx_equal(cur.w0, w0_expected, 1e-6f));
            assert(cur.L == L_expected);
            assert(cur.K == K_expected);
            (void)w0_expected; /* Suppress unused-but-set warning when asserts disabled */
            (void)L_expected;
            (void)K_expected;
        }
    }

    // AMBE+2 3600x2450: verify w0/L mapping from table for a couple of b0 values
    // We avoid including internal headers to prevent duplicate symbol definitions; instead we
    // embed expected reference values for select indices taken from the public table in source.
    {
        char ambe_d[49];
        mbe_parms cur = {0}, prev = {0};
        mbe_parms dummy;
        mbe_initMbeParms(&cur, &prev, &dummy);

        struct {
            int b0;
            float f0; // table value AmbeW0table[b0]
            int L;
        } cases[] = {
            {0, 0.049971f, 9},
            {119, 0.008125f, 56},
        };

        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            set_bits_zero(ambe_d, 49);
            set_ambe2450_b0(ambe_d, cases[i].b0);
            int rc = mbe_decodeAmbe2450Parms(ambe_d, &cur, &prev);
            // For these b0 values we expect normal voice decode (rc == 0)
            assert(rc == 0);
            (void)rc; /* Suppress unused-but-set warning when asserts disabled */
            float w0_expected = cases[i].f0 * (float)(2.0 * M_PI);
            assert(approx_equal(cur.w0, w0_expected, 1e-6f));
            assert(cur.L == cases[i].L);
            (void)w0_expected; /* Suppress unused-but-set warning when asserts disabled */
        }
    }

    return 0;
}
