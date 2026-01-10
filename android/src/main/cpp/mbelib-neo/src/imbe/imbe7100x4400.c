// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 *
 * Copyright (C) 2010 mbelib Author
 * GPG Key ID: 0xEA5EFE2C (9E7A 5527 9CDC EBF7 BF1B  D772 4F98 E863 EA5E FE2C)
 *
 * Portions were originally under the ISC license; this mbelib-neo
 * distribution is provided under GPL-2.0-or-later. See LICENSE for details.
 */
/**
 * @file
 * @brief IMBE 7100x4400 parameter decode, ECC, and synthesis hooks.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "mbelib-neo/mbelib.h"

/**
 * @brief Print IMBE 7100x4400 parameter bits to stderr (debug aid).
 * @param imbe_d IMBE parameter bits (88).
 */
void
mbe_dumpImbe7100x4400Data(char* imbe_d) {

    int i;
    char* imbe;

    imbe = imbe_d;
    for (i = 0; i < 88; i++) {
        if ((i == 7) || (i == 19) || (i == 31) || (i == 43) || (i == 54) || (i == 65)) {
            fprintf(stderr, " ");
        }
        fprintf(stderr, "%i", *imbe);
        imbe++;
    }
}

/**
 * @brief Print raw IMBE 7100x4400 frame bitplanes to stderr.
 * @param imbe_fr Frame as 7x24 bitplanes.
 */
void
mbe_dumpImbe7100x4400Frame(char imbe_fr[7][24]) {

    int i, j;

    for (j = 18; j >= 0; j--) {
        if (j == 11) {
            fprintf(stderr, " ");
        }
        fprintf(stderr, "%i", imbe_fr[0][j]);
    }
    fprintf(stderr, " ");

    for (j = 23; j >= 0; j--) {
        if (j == 11) {
            fprintf(stderr, " ");
        }
        fprintf(stderr, "%i", imbe_fr[1][j]);
    }
    fprintf(stderr, " ");

    for (i = 2; i < 4; i++) {
        for (j = 22; j >= 0; j--) {
            if (j == 10) {
                fprintf(stderr, " ");
            }
            fprintf(stderr, "%i", imbe_fr[i][j]);
        }
        fprintf(stderr, " ");
    }
    for (i = 4; i < 6; i++) {
        for (j = 14; j >= 0; j--) {
            if (j == 3) {
                fprintf(stderr, " ");
            }
            fprintf(stderr, "%i", imbe_fr[i][j]);
        }
        fprintf(stderr, " ");
    }
    for (j = 22; j >= 0; j--) {
        fprintf(stderr, "%i", imbe_fr[6][j]);
    }
}

/**
 * @brief Apply ECC to IMBE 7100x4400 C0 and update in-place.
 * @param imbe_fr Frame as 7x24 bitplanes.
 * @return Number of corrected errors in C0.
 */
int
mbe_eccImbe7100x4400C0(char imbe_fr[7][24]) {

    int j, errs;
    char in[23], out[23];

    for (j = 0; j < 18; j++) {
        in[j] = imbe_fr[0][j + 1];
    }
    for (j = 18; j < 23; j++) {
        in[j] = 0;
    }

    errs = mbe_golay2312(in, out);
    for (j = 0; j < 18; j++) {
        imbe_fr[0][j + 1] = out[j];
    }

    return (errs);
}

/**
 * @brief Internal: Apply ECC to IMBE 7100x4400 data with separate C4 error tracking.
 * @param imbe_fr Frame as 7x24 bitplanes.
 * @param imbe_d  Output parameter bits (88).
 * @param errs_c4 Output: errors in C4 (Hamming coset 4), or NULL if not needed.
 * @return Number of corrected errors in protected fields.
 */
static int
mbe_eccImbe7100x4400DataInternal(char imbe_fr[7][24], char* imbe_d, int* errs_c4) {

    int i, j, errs, hamming_errs;
    char *imbe, gin[23], gout[23], hin[15], hout[15];

    /* initialize errs implicitly via first ECC call below */
    imbe = imbe_d;

    // just copy C0
    for (j = 18; j > 11; j--) {
        *imbe = imbe_fr[0][j];
        imbe++;
    }

    // ecc and copy C1
    for (j = 0; j < 23; j++) {
        gin[j] = imbe_fr[1][j + 1];
    }
    errs = mbe_golay2312(gin, gout);
    for (j = 22; j > 10; j--) {
        *imbe = gout[j];
        imbe++;
    }

    // ecc and copy C2, C3
    for (i = 2; i < 4; i++) {
        for (j = 0; j < 23; j++) {
            gin[j] = imbe_fr[i][j];
        }
        errs += mbe_golay2312(gin, gout);
        for (j = 22; j > 10; j--) {
            *imbe = gout[j];
            imbe++;
        }
    }
    // ecc and copy C4, C5
    for (i = 4; i < 6; i++) {
        for (j = 0; j < 15; j++) {
            hin[j] = imbe_fr[i][j];
        }
        hamming_errs = mbe_7100x4400hamming1511(hin, hout);
        errs += hamming_errs;
        /* Track C4 (first Hamming coset) errors separately for adaptive smoothing */
        if (i == 4 && errs_c4 != NULL) {
            *errs_c4 = hamming_errs;
        }
        for (j = 14; j >= 4; j--) {
            *imbe = hout[j];
            imbe++;
        }
    }

    // just copy C6
    for (j = 22; j >= 0; j--) {
        *imbe = imbe_fr[6][j];
        imbe++;
    }

    return (errs);
}

/**
 * @brief Apply ECC to IMBE 7100x4400 data and pack parameter bits.
 * @param imbe_fr Frame as 7x24 bitplanes.
 * @param imbe_d  Output parameter bits (88).
 * @return Number of corrected errors in protected fields.
 */
int
mbe_eccImbe7100x4400Data(char imbe_fr[7][24], char* imbe_d) {
    return mbe_eccImbe7100x4400DataInternal(imbe_fr, imbe_d, NULL);
}

/**
 * @brief Demodulate interleaved IMBE 7100x4400 data in-place.
 * @param imbe Frame as 7x24 bitplanes (modified).
 */
void
mbe_demodulateImbe7100x4400Data(char imbe[7][24]) {

    int i, j, k;
    unsigned short pr[115];
    unsigned short seed;
    char tmpstr[24];

    // create pseudo-random modulator
    j = 0;
    tmpstr[7] = 0;
    for (i = 18; i > 11; i--) {
        tmpstr[j] = (imbe[0][i] + 48);
        j++;
    }
    seed = strtol(tmpstr, NULL, 2);
    pr[0] = (16 * seed);
    for (i = 1; i < 101; i++) {
        pr[i] = (173 * pr[i - 1]) + 13849 - (65536 * (((173 * pr[i - 1]) + 13849) / 65536));
    }
    /* retain pr[100] only for legacy reference; 'seed' not used afterward */
    for (i = 1; i < 101; i++) {
        pr[i] >>= 15; /* normalize to {0,1} cheaply */
    }

    // demodulate imbe with pr
    k = 1;
    for (j = 23; j >= 0; j--) {
        imbe[1][j] = ((imbe[1][j]) ^ pr[k]);
        k++;
    }

    for (i = 2; i < 4; i++) {
        for (j = 22; j >= 0; j--) {
            imbe[i][j] = ((imbe[i][j]) ^ pr[k]);
            k++;
        }
    }

    for (i = 4; i < 6; i++) {
        for (j = 14; j >= 0; j--) {
            imbe[i][j] = ((imbe[i][j]) ^ pr[k]);
            k++;
        }
    }
}

/**
 * @brief Convert IMBE 7100x4400 parameter layout to 7200x4400 layout.
 * @param imbe_d In/out parameter vector (88 bits), converted in-place.
 */
void
mbe_convertImbe7100to7200(char* imbe_d) {

    int i, j, k, K, L, b0;
    char tmpstr[9];
    char tmp_imbe[88];
    float w0;

    // decode fundamental frequency w0 from b0
    tmpstr[8] = 0;
    tmpstr[0] = imbe_d[1] + 48;
    tmpstr[1] = imbe_d[2] + 48;
    tmpstr[2] = imbe_d[3] + 48;
    tmpstr[3] = imbe_d[4] + 48;
    tmpstr[4] = imbe_d[5] + 48;
    tmpstr[5] = imbe_d[6] + 48;
    tmpstr[6] = imbe_d[86] + 48;
    tmpstr[7] = imbe_d[87] + 48;
    b0 = strtol(tmpstr, NULL, 2);
    w0 = ((float)(4 * M_PI) / (float)((float)b0 + 39.5));

    // decode L from w0
    L = (int)(0.9254 * (int)((M_PI / w0) + 0.25));

    // decode K from L
    if (L < 37) {
        K = (int)((float)(L + 2) / (float)3);
    } else {
        K = 12;
    }

    // rearrange bits from imbe7100x4400 format to imbe7200x4400 format
    tmp_imbe[87] = imbe_d[0];      // "status"/zero bit
    tmp_imbe[48 + K] = imbe_d[42]; // b2.2
    tmp_imbe[49 + K] = imbe_d[43]; // b2.1

    k = 44;
    j = 48;
    for (i = 0; i < K; i++) {
        tmp_imbe[j] = imbe_d[k]; // b1
        j++;
        k++;
    }

    j = 0;
    k = 1;
    while (j < 87) {
        tmp_imbe[j] = imbe_d[k];
        if (++j == 48) {
            j += (K + 2); // skip over b1, b2.2, b2.1 on dest
        }
        if (++k == 42) {
            k += (K + 2); // skip over b2.2, b2.1, b1 on src
        }
    }

    //copy new format back to imbe_d

    for (i = 0; i < 88; i++) {
        imbe_d[i] = tmp_imbe[i];
    }
}

/**
 * @brief Process a complete IMBE 7100x4400 frame into float PCM.
 * @param aout_buf Output buffer of 160 float samples.
 * @param errs,errs2,err_str Error reporting as per IMBE 4400 Dataf.
 * @param imbe_fr  Input frame as 7x24 bitplanes.
 * @param imbe_d   Scratch/output parameter bits (88).
 * @param cur_mp,prev_mp,prev_mp_enhanced Parameter state as per Dataf variant.
 * @param uvquality Unvoiced synthesis quality (1..64).
 */
void
mbe_processImbe7100x4400Framef(float* aout_buf, int* errs, int* errs2, char* err_str, char imbe_fr[7][24],
                               char imbe_d[88], mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced,
                               int uvquality) {

    int errs_c4 = 0;

    *errs = 0;
    *errs2 = 0;
    *errs = mbe_eccImbe7100x4400C0(imbe_fr);
    mbe_demodulateImbe7100x4400Data(imbe_fr);
    *errs2 = *errs;
    *errs2 += mbe_eccImbe7100x4400DataInternal(imbe_fr, imbe_d, &errs_c4);
    mbe_convertImbe7100to7200(imbe_d);

    /* Set C4 error count for adaptive smoothing (JMBE Algorithm #112 formula selection) */
    cur_mp->errorCount4 = errs_c4;

    mbe_processImbe4400Dataf(aout_buf, errs, errs2, err_str, imbe_d, cur_mp, prev_mp, prev_mp_enhanced, uvquality);
}

/**
 * @brief Process a complete IMBE 7100x4400 frame into 16-bit PCM.
 * @see mbe_processImbe7100x4400Framef for details.
 */
void
mbe_processImbe7100x4400Frame(short* aout_buf, int* errs, int* errs2, char* err_str, char imbe_fr[7][24],
                              char imbe_d[88], mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced,
                              int uvquality) {

    float float_buf[160];
    mbe_processImbe7100x4400Framef(float_buf, errs, errs2, err_str, imbe_fr, imbe_d, cur_mp, prev_mp, prev_mp_enhanced,
                                   uvquality);
    mbe_floattoshort(float_buf, aout_buf);
}
