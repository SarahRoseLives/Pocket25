// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Focused FEC boundary tests for P25 Phase 1 components:
 * - Hamming(10,6,3): single-bit correction on hex and parity.
 * - Golay(24,12,8): correct up to 3 bit errors; fail on 4.
 * - RS(24,16,9): correct up to 4 symbol errors; fail on 5.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/p25/p25p1_check_hdu.h>
#include <dsd-neo/protocol/p25/p25p1_check_ldu.h>

static void
bits_from_u(int value, int n_bits, char* out_bits) {
    for (int i = 0; i < n_bits; i++) {
        out_bits[i] = (char)((value >> (n_bits - 1 - i)) & 1);
    }
}

static int
u_from_bits(const char* bits, int n_bits) {
    int v = 0;
    for (int i = 0; i < n_bits; i++) {
        v = (v << 1) | (bits[i] & 1);
    }
    return v;
}

static void
flip_bit(char* arr, int idx) {
    arr[idx] = (char)(arr[idx] ? 0 : 1);
}

static int
expect_eq_hex(const char* tag, const char* got, const char* want, int n) {
    if (memcmp(got, want, (size_t)n) != 0) {
        fprintf(stderr, "%s mismatch\n", tag);
        return 1;
    }
    return 0;
}

static int
expect_eq_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;

    // Hamming(10,6,3): single-bit correction
    {
        char hex[6], parity[4], orig[6];
        bits_from_u(0x2A /* 101010 */, 6, hex);
        memcpy(orig, hex, 6);
        encode_hamming_10_6_3(hex, parity);

        // Flip one data bit
        char h1[6], p1[4];
        memcpy(h1, orig, 6);
        memcpy(p1, parity, 4);
        flip_bit(h1, 3);
        int est1 = check_and_fix_hamming_10_6_3(h1, p1);
        rc |= expect_eq_hex("Hamming single data-bit fix", h1, orig, 6);
        // est1 should be >=1, but we don't assert the exact value.
        if (est1 <= 0) {
            fprintf(stderr, "Hamming estimated errors not positive (%d)\n", est1);
            rc |= 1;
        }

        // Flip one parity bit
        char h2[6], p2[4];
        memcpy(h2, orig, 6);
        memcpy(p2, parity, 4);
        flip_bit(p2, 1);
        int est2 = check_and_fix_hamming_10_6_3(h2, p2);
        rc |= expect_eq_hex("Hamming single parity-bit fix (data unchanged)", h2, orig, 6);
        if (est2 <= 0) {
            fprintf(stderr, "Hamming estimated errors not positive for parity flip (%d)\n", est2);
            rc |= 1;
        }
    }

    // Golay(24,12,8): up to 3 bit errors corrected; 4 becomes irrecoverable
    {
        char dodeca[12], parity[12], orig[12];
        bits_from_u(0xACE /* 12 bits */, 12, dodeca);
        memcpy(orig, dodeca, 12);
        encode_golay_24_12(dodeca, parity);

        // 3 random flips across data+parity
        char d1[12], p1[12];
        memcpy(d1, orig, 12);
        memcpy(p1, parity, 12);
        flip_bit(d1, 0);
        flip_bit(d1, 7);
        flip_bit(p1, 4);
        int fixed = 0;
        int irr = check_and_fix_golay_24_12(d1, p1, &fixed);
        rc |= expect_eq_int("Golay(24,12) irrecoverable(<=3)", irr, 0);
        rc |= expect_eq_hex("Golay corrected data", d1, orig, 12);

        // 4 flips: expect irrecoverable
        char d2[12], p2[12];
        memcpy(d2, orig, 12);
        memcpy(p2, parity, 12);
        flip_bit(d2, 0);
        flip_bit(d2, 1);
        flip_bit(d2, 2);
        flip_bit(p2, 0);
        fixed = 0;
        irr = check_and_fix_golay_24_12(d2, p2, &fixed);
        rc |= expect_eq_int("Golay(24,12) irrecoverable(>=4)", irr, 1);
    }

    // RS(24,16,9): up to 4 symbol errors corrected; 5 fails
    {
        // Build 16 6-bit data symbols
        char data_bits[16 * 6];
        for (int i = 0; i < 16; i++) {
            int sym = (i * 7 + 3) & 0x3F;
            bits_from_u(sym, 6, &data_bits[(size_t)i * 6]);
        }
        char parity_bits[8 * 6];
        encode_reedsolomon_24_16_9(data_bits, parity_bits);

        // Make a working copy and flip 4 entire symbols (invert all 6 bits)
        char w1[16 * 6];
        memcpy(w1, data_bits, sizeof(w1));
        int flip_syms1[] = {0, 5, 9, 15};
        for (unsigned i = 0; i < sizeof(flip_syms1) / sizeof(flip_syms1[0]); i++) {
            int s = flip_syms1[i];
            for (int b = 0; b < 6; b++) {
                flip_bit(&w1[(size_t)s * 6], b);
            }
        }
        int irr = check_and_fix_reedsolomon_24_16_9(w1, parity_bits);
        rc |= expect_eq_int("RS(24,16,9) irrecoverable(<=4 sym)", irr, 0);
        rc |= expect_eq_hex("RS(24,16,9) corrected data", w1, data_bits, ((size_t)16) * 6);

        // Flip 5 symbols
        char w2[16 * 6];
        memcpy(w2, data_bits, sizeof(w2));
        int flip_syms2[] = {0, 1, 4, 8, 12};
        for (unsigned i = 0; i < sizeof(flip_syms2) / sizeof(flip_syms2[0]); i++) {
            int s = flip_syms2[i];
            for (int b = 0; b < 6; b++) {
                flip_bit(&w2[(size_t)s * 6], b);
            }
        }
        irr = check_and_fix_reedsolomon_24_16_9(w2, parity_bits);
        rc |= expect_eq_int("RS(24,16,9) irrecoverable(>=5 sym)", irr, 1);
    }

    return rc;
}
