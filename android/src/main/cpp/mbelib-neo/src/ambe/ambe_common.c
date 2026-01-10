// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Internal helpers shared by AMBE 3600x2400 and 3600x2450 paths.
 *
 * Provides common ECC correction for C0, demodulation of C1 using a
 * pseudo-random sequence derived from C0, and packing of 49 AMBE
 * parameter bits from the four bitplanes.
 */

#include "ambe_common.h"
#include "mbelib-neo/mbelib.h"

int
mbe_eccAmbe3600C0_common(char fr[4][24]) {
    int j, errs;
    char in[23], out[23];
    for (j = 0; j < 23; j++) {
        in[j] = fr[0][j + 1];
    }
    errs = mbe_golay2312(in, out);
    for (j = 0; j < 23; j++) {
        fr[0][j + 1] = out[j];
    }
    return errs;
}

void
mbe_demodulateAmbe3600Data_common(char fr[4][24]) {
    int i, j, k;
    unsigned short pr[115];
    unsigned short foo = 0;

    /* create pseudo-random modulator */
    for (i = 23; i >= 12; i--) {
        foo <<= 1;
        foo |= fr[0][i];
    }
    pr[0] = (unsigned short)(16 * foo);
    for (i = 1; i < 24; i++) {
        pr[i] = (unsigned short)((173 * pr[i - 1]) + 13849 - (65536 * (((173 * pr[i - 1]) + 13849) / 65536)));
    }
    for (i = 1; i < 24; i++) {
        pr[i] = (unsigned short)(pr[i] / 32768);
    }

    /* demodulate fr with pr */
    k = 1;
    for (j = 22; j >= 0; j--) {
        fr[1][j] = (char)((fr[1][j]) ^ pr[k]);
        k++;
    }
}

int
mbe_eccAmbe3600Data_common(char fr[4][24], char* out49) {
    int j, errs;
    char *ambe, gin[24], gout[24];
    ambe = out49;
    /* just copy C0 */
    for (j = 23; j > 11; j--) {
        *ambe = fr[0][j];
        ambe++;
    }
    /* ecc and copy C1 */
    for (j = 0; j < 23; j++) {
        gin[j] = fr[1][j];
    }
    errs = mbe_golay2312(gin, gout);
    for (j = 22; j > 10; j--) {
        *ambe = gout[j];
        ambe++;
    }
    /* just copy C2 */
    for (j = 10; j >= 0; j--) {
        *ambe = fr[2][j];
        ambe++;
    }
    /* just copy C3 */
    for (j = 13; j >= 0; j--) {
        *ambe = fr[3][j];
        ambe++;
    }
    return errs;
}
