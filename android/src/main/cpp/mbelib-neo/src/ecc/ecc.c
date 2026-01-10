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
 * @brief Error-correcting code helpers for Golay and Hamming.
 */

#include <stdint.h>
#include "ecc_const.h"
#include "mbelib-neo/mbelib.h"

/*
 * Precomputed correction masks for Hamming (15,11) variants.
 * These map 4-bit syndromes to a single-bit mask (1 << bit_index) and
 * avoid any runtime initialization or data races.
 */
static const int ham1511_lut[16] = {
    /* index: syndrome [0..15] */
    0, 8, 4, 2048, 2, 512, 64, 8192, 1, 256, 32, 4096, 16, 1024, 128, 16384,
};

static const int ham1511_7100_lut[16] = {
    /* index: syndrome [0..15] */
    0, 8, 4, 64, 2, 512, 32, 2048, 1, 16384, 256, 8192, 16, 128, 1024, 4096,
};

/**
 * @brief Correct a (23,12) Golay encoded block in-place and extract data.
 * @param block Pointer to packed 23-bit codeword (LSBs contain codeword). On return, holds 12-bit data.
 */
void
mbe_checkGolayBlock(long int* block) {

    int i;
    int syndrome, eccexpected, eccbits, databits;
    uint32_t mask;
    uint32_t block_u;

    block_u = (uint32_t)(*block);

    mask = 0x400000u; /* MSB of 23-bit codeword */
    eccexpected = 0;
    for (i = 0; i < 12; i++) {
        if ((block_u & mask) != 0u) {
            eccexpected ^= golayGenerator[i];
        }
        mask >>= 1;
    }
    eccbits = (int)(block_u & 0x7ffu);
    syndrome = eccexpected ^ eccbits;

    databits = (int)(block_u >> 11);
    databits ^= golayMatrix[syndrome];

    *block = (long)databits;
}

/**
 * @brief Decode a (23,12) Golay codeword.
 * @param in  Input bits, LSB at index 0, length 23.
 * @param out Output bits, corrected, LSB at index 0, length 23.
 * @return Number of corrected bit errors in the protected portion.
 */
int
mbe_golay2312(char* in, char* out) {

    int i, errs;
    uint32_t block = 0u;

    for (i = 22; i >= 0; i--) {
        block <<= 1;
        block |= (uint32_t)(in[i] & 1);
    }

    long tmp = (long)block;
    mbe_checkGolayBlock(&tmp);
    block = (uint32_t)tmp;

    for (i = 22; i >= 11; i--) {
        out[i] = (char)((block & 2048u) >> 11);
        block <<= 1;
    }
    for (i = 10; i >= 0; i--) {
        out[i] = in[i];
    }

    errs = 0;
    for (i = 22; i >= 11; i--) {
        if (out[i] != in[i]) {
            errs++;
        }
    }
    return errs;
}

/**
 * @brief Decode a (15,11) Hamming codeword.
 * @param in  Input bits, LSB at index 0, length 15.
 * @param out Output bits, corrected, LSB at index 0, length 15.
 * @return Number of corrected bit errors (0 or 1).
 * @note Uses a precomputed syndrome→bitmask LUT for thread safety (no lazy init).
 */
int
mbe_hamming1511(char* in, char* out) {
    int i, j, errs;
    uint32_t block = 0u;
    int syndrome, stmp, stmp2;

    errs = 0;

    for (i = 14; i >= 0; i--) {
        block <<= 1;
        block |= (uint32_t)(in[i] & 1);
    }

    syndrome = 0;
    for (i = 0; i < 4; i++) {
        stmp = (int)(block & (uint32_t)hammingGenerator[i]);
        stmp2 = (stmp & 1);
        for (j = 0; j < 14; j++) {
            stmp >>= 1;
            stmp2 ^= (stmp & 1);
        }
        syndrome |= (stmp2 << i);
    }
    if (syndrome > 0) {
        errs++;
        block ^= (uint32_t)ham1511_lut[syndrome];
    }

    for (i = 14; i >= 0; i--) {
        out[i] = (char)((block & 0x4000u) >> 14);
        block <<= 1;
    }
    return errs;
}

/**
 * @brief Decode a (15,11) Hamming codeword with IMBE 7100x4400 mapping.
 * @param in  Input bits, LSB at index 0, length 15.
 * @param out Output bits, corrected, LSB at index 0, length 15.
 * @return Number of corrected bit errors (0 or 1).
 * @note Uses a precomputed syndrome→bitmask LUT for thread safety (no lazy init).
 */
int
mbe_7100x4400hamming1511(char* in, char* out) {
    int i, j, errs;
    uint32_t block = 0u;
    int syndrome, stmp, stmp2;

    errs = 0;

    for (i = 14; i >= 0; i--) {
        block <<= 1;
        block |= (uint32_t)(in[i] & 1);
    }

    syndrome = 0;
    for (i = 0; i < 4; i++) {
        stmp = (int)(block & (uint32_t)imbe7100x4400hammingGenerator[i]);
        stmp2 = (stmp & 1);
        for (j = 0; j < 14; j++) {
            stmp >>= 1;
            stmp2 ^= (stmp & 1);
        }
        syndrome |= (stmp2 << i);
    }
    if (syndrome > 0) {
        errs++;
        block ^= (uint32_t)ham1511_7100_lut[syndrome];
    }

    for (i = 14; i >= 0; i--) {
        out[i] = (char)((block & 0x4000u) >> 14);
        block <<= 1;
    }
    return errs;
}
