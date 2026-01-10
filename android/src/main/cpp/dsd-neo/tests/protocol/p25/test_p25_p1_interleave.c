// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 1 IMBE interleave schedule consistency tests.
 *
 * Validates that (iW,iX) and (iY,iZ) indices are in-range and that a
 * round-trip mapping through the schedule preserves the original 72 dibits.
 */

#include <stdint.h>
#include <stdio.h>

// Local copy of the IMBE interleave schedule to validate mapping logic
static const int IW[72] = {0, 2, 4, 1, 3, 5, 0, 2, 4, 1, 3, 6, 0, 2, 4, 1, 3, 6, 0, 2, 4, 1, 3, 6,
                           0, 2, 4, 1, 3, 6, 0, 2, 4, 1, 3, 6, 0, 2, 5, 1, 3, 6, 0, 2, 5, 1, 3, 6,
                           0, 2, 5, 1, 3, 7, 0, 2, 5, 1, 3, 7, 0, 2, 5, 1, 4, 7, 0, 3, 5, 2, 4, 7};
static const int IX[72] = {22, 20, 10, 20, 18, 0, 20, 18, 8, 18, 16, 13, 18, 16, 6,  16, 14, 11, 16, 14, 4,  14, 12, 9,
                           14, 12, 2,  12, 10, 7, 12, 10, 0, 10, 8,  5,  10, 8,  13, 8,  6,  3,  8,  6,  11, 6,  4,  1,
                           6,  4,  9,  4,  2,  6, 4,  2,  7, 2,  0,  4,  2,  0,  5,  0,  13, 2,  0,  21, 3,  21, 11, 0};
static const int IY[72] = {1, 3, 5, 0, 2, 4, 1, 3, 6, 0, 2, 4, 1, 3, 6, 0, 2, 4, 1, 3, 6, 0, 2, 4,
                           1, 3, 6, 0, 2, 4, 1, 3, 6, 0, 2, 5, 1, 3, 6, 0, 2, 5, 1, 3, 6, 0, 2, 5,
                           1, 3, 6, 0, 2, 5, 1, 3, 7, 0, 2, 5, 1, 4, 7, 0, 3, 5, 2, 4, 7, 1, 3, 5};
static const int IZ[72] = {21, 19, 1, 21, 19, 9, 19, 17, 14, 19, 17, 7,  17, 15, 12, 17, 15, 5,  15, 13, 10, 15, 13, 3,
                           13, 11, 8, 13, 11, 1, 11, 9,  6,  11, 9,  14, 9,  7,  4,  9,  7,  12, 7,  5,  2,  7,  5,  10,
                           5,  3,  0, 5,  3,  8, 3,  1,  5,  3,  1,  6,  1,  14, 3,  1,  22, 4,  22, 12, 1,  22, 20, 2};

static int
expect_ok(const char* tag, int ok) {
    if (!ok) {
        fprintf(stderr, "%s failed\n", tag);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;

    // Check index ranges
    for (int j = 0; j < 72; j++) {
        int w = IW[j], x = IX[j], y = IY[j], z = IZ[j];
        rc |= expect_ok("iW range", (w >= 0 && w <= 7));
        rc |= expect_ok("iY range", (y >= 0 && y <= 7));
        int xmax = (w < 4) ? 23 : 15;
        int zmax = (y < 4) ? 23 : 15;
        rc |= expect_ok("iX range", (x >= 0 && x < xmax));
        rc |= expect_ok("iZ range", (z >= 0 && z < zmax));
    }

    // Round-trip mapping: write dibits via schedule then read back
    char imbe_fr[8][23] = {{0}};
    int in_dibit[72];
    int out_dibit[72];

    for (int j = 0; j < 72; j++) {
        in_dibit[j] = (j & 3); // two-bit pattern
        int b1 = (in_dibit[j] >> 1) & 1;
        int b0 = in_dibit[j] & 1;
        imbe_fr[IW[j]][IX[j]] = (char)b1;
        imbe_fr[IY[j]][IZ[j]] = (char)b0;
    }
    for (int j = 0; j < 72; j++) {
        int b1 = imbe_fr[IW[j]][IX[j]] & 1;
        int b0 = imbe_fr[IY[j]][IZ[j]] & 1;
        out_dibit[j] = (b1 << 1) | b0;
    }
    for (int j = 0; j < 72; j++) {
        if (in_dibit[j] != out_dibit[j]) {
            fprintf(stderr, "round-trip mismatch at %d: in=%d out=%d\n", j, in_dibit[j], out_dibit[j]);
            rc |= 1;
        }
    }

    return rc;
}
