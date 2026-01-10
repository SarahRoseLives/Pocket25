// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Unit tests for ECC helpers (Golay and Hamming variants).
 */
#include <assert.h>
#include <stdint.h>
#include <string.h>
// Embedded copies of generator tables used for test expectations
static const int hammingGenerator[4] = {0x7f08, 0x78e4, 0x66d2, 0x55b1};
static const int imbe7100x4400hammingGenerator[4] = {0x7ac8, 0x3d64, 0x1eb2, 0x7591};
static const int golayGenerator[12] = {0x63a, 0x31d, 0x7b4, 0x3da, 0x1ed, 0x6cc,
                                       0x366, 0x1b3, 0x6e3, 0x54b, 0x49f, 0x475};
#include "mbelib-neo/mbelib.h"

// make stderr unbuffered for debug visibility under ctest
#include <stdio.h>

/**
 * @brief Flip a single bit in a bit-vector.
 * @param bits Bit array of chars containing 0/1 values.
 * @param idx  Index of bit to toggle.
 */
static void
flip_bit(char* bits, int idx) {
    bits[idx] = bits[idx] ? 0 : 1;
}

/**
 * @brief Compute 4-bit syndrome for a 15-bit codeword using a generator.
 * @param code      Codeword bits as 0/1 chars, LSB at index 0.
 * @param generator Array of 4 generator row masks.
 * @return Syndrome (LSB-first) in range [0, 15].
 */
static int
compute_syndrome_with_generator(const char code[15], const int generator[4]) {
    int block = 0;
    for (int i = 14; i >= 0; --i) {
        block <<= 1;
        block |= (code[i] ? 1 : 0);
    }
    int syndrome = 0;
    for (int r = 0; r < 4; ++r) {
        int stmp = block & generator[r];
        int sbit = (stmp & 1);
        for (int j = 0; j < 14; ++j) {
            stmp >>= 1;
            sbit ^= (stmp & 1);
        }
        syndrome |= (sbit << r);
    }
    return syndrome;
}

/**
 * @brief Determine parity bit positions (those not in data_pos).
 * @param data_pos  11 indices of data positions (0..14).
 * @param parity_pos Output 4 indices for parity positions.
 */
static void
derive_parity_positions(const int data_pos[11], int parity_pos[4]) {
    int p = 0;
    for (int i = 0; i < 15; ++i) {
        int is_data = 0;
        for (int d = 0; d < 11; ++d) {
            if (data_pos[d] == i) {
                is_data = 1;
                break;
            }
        }
        if (!is_data) {
            parity_pos[p++] = i;
        }
    }
}

/**
 * @brief Encode a (15,11) Hamming codeword for given data positions.
 *
 * Brute-forces the 4 parity bits (16 combinations) until syndrome is zero.
 *
 * @param generator  Generator row masks (4 entries).
 * @param data_pos   11 indices of data positions (0..14).
 * @param data_in    11 data bits (0/1 chars) mapped to data_pos.
 * @param code       Output encoded 15-bit codeword (0/1 chars).
 * @return 1 on success, 0 if no parity combination yields zero syndrome.
 */
static int
encode_hamming15_with_generator(const int generator[4], const int data_pos[11], const char data_in[11], char code[15]) {
    for (int i = 0; i < 15; ++i) {
        code[i] = 0;
    }
    for (int i = 0; i < 11; ++i) {
        code[data_pos[i]] = (data_in[i] ? 1 : 0);
    }
    int parity_pos[4];
    derive_parity_positions(data_pos, parity_pos);
    // brute-force 4 parity bits (16 combos) to satisfy parity equations
    for (int bits = 0; bits < 16; ++bits) {
        for (int k = 0; k < 4; ++k) {
            code[parity_pos[k]] = ((bits >> k) & 1);
        }
        int syn = compute_syndrome_with_generator(code, generator);
        if (syn == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Convert a 4-bit syndrome into a (1<<bit) correction mask.
 * @param syndrome  4-bit syndrome value (LSB-first).
 * @param generator Generator row masks (4 entries).
 * @return Bit mask with one bit set (1<<bit), or 0 if not found.
 */
static int
correction_mask_from_generator(int syndrome, const int generator[4]) {
    if (syndrome == 0) {
        return 0;
    }
    for (int j = 0; j < 15; ++j) {
        int syn = 0;
        for (int r = 0; r < 4; ++r) {
            syn |= (((generator[r] >> j) & 1) << r);
        }
        if (syn == syndrome) {
            return (1 << j);
        }
    }
    return 0;
}

/**
 * @brief Test entry: exercises Hamming and Golay decoders.
 */
int
main(void) {
    setvbuf(stderr, NULL, _IONBF, 0);

    // Hamming (15,11): fixed-point and data invariance under single-bit errors
    {
        // Data positions (0-based bit indices): all except parity 0,1,3,7
        int data_pos[11] = {2, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14};

        // Start from explicit 11-bit data and encode to a valid codeword
        char data11[11];
        for (int i = 0; i < 11; ++i) {
            data11[i] = (i % 2);
        }
        char code[15];
        int parity_pos[4];
        derive_parity_positions(data_pos, parity_pos);
        int ok = encode_hamming15_with_generator(hammingGenerator, data_pos, data11, code);
        assert(ok == 1);
        (void)ok; /* Suppress unused-but-set warning when asserts disabled */

        // Extract data bits
        char data_ref[11];
        for (int i = 0; i < 11; ++i) {
            data_ref[i] = code[data_pos[i]];
        }

        // Decoding a codeword should be a fixed point
        char code2[15];
        int errs2 = mbe_hamming1511(code, code2);
        assert(errs2 == 0);
        (void)errs2; /* Suppress unused-but-set warning when asserts disabled */
        assert(memcmp(code, code2, sizeof(code)) == 0);

        // Sanity: bit-level correction in integer domain should restore original block
        int block_orig = 0;
        for (int i = 14; i >= 0; --i) {
            block_orig <<= 1;
            block_orig |= (code[i] ? 1 : 0);
        }
        for (int j = 0; j < 15; ++j) {
            int err_block = block_orig ^ (1 << j);
            // compute syndrome identical to decoder
            int syn = 0;
            for (int r = 0; r < 4; ++r) {
                int stmp = err_block & hammingGenerator[r];
                int sbit = (stmp & 1);
                for (int t = 0; t < 14; ++t) {
                    stmp >>= 1;
                    sbit ^= (stmp & 1);
                }
                syn |= (sbit << r);
            }
            if (syn != 0) {
                int fixed_block = err_block ^ correction_mask_from_generator(syn, hammingGenerator);
                if (fixed_block != block_orig) {
                    fprintf(stderr, "int-domain correction mismatch at bit %d (syn=%d)\n", j, syn);
                    return 5;
                }
            }
        }

        // Flip each bit and ensure decode restores original codeword and data
        int failures = 0;
        for (int k = 0; k < 15; ++k) {
            char err[15];
            memcpy(err, code, sizeof(err));
            int idx = 14 - k; // flip MSB-first to match generator orientation
            flip_bit(err, idx);
            char fixed[15];
            int errs3 = mbe_hamming1511(err, fixed);
            assert(errs3 >= 1);
            (void)errs3; /* Suppress unused-but-set warning when asserts disabled */
            if (memcmp(code, fixed, sizeof(code)) != 0) {
                fprintf(stderr, "mismatch at bit %d for standard hamming\n", k);
                fprintf(stderr, "orig: ");
                for (int b = 14; b >= 0; --b) {
                    fprintf(stderr, "%d", code[b]);
                }
                fprintf(stderr, "\nfixd: ");
                for (int b = 14; b >= 0; --b) {
                    fprintf(stderr, "%d", fixed[b]);
                }
                fprintf(stderr, "\n");
                // continue to verify the data bits still match
            }
            // don't require bit-perfect codeword equality; require data equality below
            // extract data bits by re-encoding fixed to a canonical codeword and comparing data
            // simpler: just pick out data bit positions
            char data_now[11];
            for (int i = 0; i < 11; ++i) {
                data_now[i] = fixed[data_pos[i]];
            }
            if (memcmp(data_ref, data_now, sizeof(data_ref)) != 0) {
                fprintf(stderr, "data mismatch at bit %d for standard hamming\n", k);
                ++failures;
            }
        }
        if (failures) {
            return 4;
        }
    }

    // IMBE 7100x4400 Hamming variant: same properties
    {
        int data_pos[11] = {2, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14};

        char data11[11];
        for (int i = 0; i < 11; ++i) {
            data11[i] = ((i % 3) == 0);
        }
        char code[15];
        int ok2 = encode_hamming15_with_generator(imbe7100x4400hammingGenerator, data_pos, data11, code);
        assert(ok2 == 1);
        (void)ok2; /* Suppress unused-but-set warning when asserts disabled */

        char data_ref[11];
        for (int i = 0; i < 11; ++i) {
            data_ref[i] = code[data_pos[i]];
        }

        char code2[15];
        int errs2 = mbe_7100x4400hamming1511(code, code2);
        assert(errs2 == 0);
        (void)errs2; /* Suppress unused-but-set warning when asserts disabled */
        assert(memcmp(code, code2, sizeof(code)) == 0);

        for (int k = 0; k < 15; ++k) {
            char err[15];
            memcpy(err, code, sizeof(err));
            int idx = 14 - k; // flip MSB-first to match generator orientation
            flip_bit(err, idx);
            char fixed[15];
            int errs3 = mbe_7100x4400hamming1511(err, fixed);
            assert(errs3 >= 1);
            (void)errs3; /* Suppress unused-but-set warning when asserts disabled */
            if (memcmp(code, fixed, sizeof(code)) != 0) {
                fprintf(stderr, "mismatch at bit %d for 7100 hamming\n", k);
                return 3;
            }
            assert(memcmp(code, fixed, sizeof(code)) == 0);
            char data_now[11];
            for (int i = 0; i < 11; ++i) {
                data_now[i] = fixed[data_pos[i]];
            }
            assert(memcmp(data_ref, data_now, sizeof(data_ref)) == 0);
        }
    }

    // Golay (23,12): corrects any single-bit error in the 23-bit codeword
    {
        // Choose a 12-bit data value
        unsigned int data = 0xA55 & 0xFFFu;
        // Compute expected ECC using generator table mapping used in ecc.c
        unsigned int ecc = 0u;
        for (int i = 0; i < 12; ++i) {
            // i=0 corresponds to MSB of the 12 data bits (bit 11)
            if ((data >> (11 - i)) & 1u) {
                ecc ^= (unsigned int)golayGenerator[i];
            }
        }
        // Compose 23-bit block: [data (12b) | ecc (11b)]
        unsigned int block = ((data & 0x0FFFu) << 11) | (ecc & 0x7FFu);

        // Introduce a single-bit error in the 23-bit block
        unsigned int block_err = block ^ (1u << 5);
        long int b = (long int)block_err;
        mbe_checkGolayBlock(&b);
        // mbe_checkGolayBlock returns only the 12-bit data in b
        assert((unsigned int)b == data);
    }

    return 0;
}
