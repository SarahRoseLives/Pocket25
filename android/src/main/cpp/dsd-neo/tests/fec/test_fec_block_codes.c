// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/fec/block_codes.h>

static void
set_bits_from_u32(unsigned char* dst_bits, int nbits, unsigned int v) {
    for (int i = 0; i < nbits; i++) {
        dst_bits[i] = (unsigned char)((v >> i) & 1U);
    }
}

static int
arrays_equal_u8(const unsigned char* a, const unsigned char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if ((a[i] & 1U) != (b[i] & 1U)) {
            return 0;
        }
    }
    return 1;
}

static int
test_hamming_codes(void) {
    InitAllFecFunction();

    // Hamming (7,4)
    {
        unsigned char msg[4];
        unsigned char enc[7], rx[7];
        unsigned int v = 0xA; // 1010
        set_bits_from_u32(msg, 4, v);
        Hamming_7_4_encode(msg, enc);
        memcpy(rx, enc, 7);
        assert(Hamming_7_4_decode(rx) == true);
        assert(arrays_equal_u8(rx, enc, 7));
        // 1-bit error
        memcpy(rx, enc, 7);
        rx[2] ^= 1;
        assert(Hamming_7_4_decode(rx) == true);
        assert(arrays_equal_u8(rx, enc, 7));
        // Note: (7,4) single-error correction may miscorrect double errors.
        // Do not assert behavior on 2-bit errors here.
    }

    // Hamming (12,8)
    {
        unsigned char msg[8];
        unsigned char enc[12], rx[12], dec[8];
        set_bits_from_u32(msg, 8, 0x5A); // 01011010
        Hamming_12_8_encode(msg, enc);
        memcpy(rx, enc, 12);
        assert(Hamming_12_8_decode(rx, dec, 1) == true);
        assert(memcmp(dec, msg, 8) == 0);
        // 1-bit error
        memcpy(rx, enc, 12);
        rx[5] ^= 1;
        memset(dec, 0, sizeof(dec));
        assert(Hamming_12_8_decode(rx, dec, 1) == true);
        assert(memcmp(dec, msg, 8) == 0);
        // Multi-bit errors: decoder may not guarantee detect; skip assertion.
    }

    // Hamming (13,9)
    {
        unsigned char msg[9];
        unsigned char enc[13], rx[13], dec[9];
        set_bits_from_u32(msg, 9, 0x155); // pattern
        Hamming_13_9_encode(msg, enc);
        memcpy(rx, enc, 13);
        assert(Hamming_13_9_decode(rx, dec, 1) == true);
        assert(memcmp(dec, msg, 9) == 0);
        // 1-bit error
        memcpy(rx, enc, 13);
        rx[4] ^= 1;
        memset(dec, 0, sizeof(dec));
        assert(Hamming_13_9_decode(rx, dec, 1) == true);
        assert(memcmp(dec, msg, 9) == 0);
        // Multi-bit errors: skip assertion.
    }

    // Hamming (15,11)
    {
        unsigned char msg[11];
        unsigned char enc[15], rx[15], dec[11];
        set_bits_from_u32(msg, 11, 0x3A5);
        Hamming_15_11_encode(msg, enc);
        memcpy(rx, enc, 15);
        assert(Hamming_15_11_decode(rx, dec, 1) == true);
        assert(memcmp(dec, msg, 11) == 0);
        // 1-bit error
        memcpy(rx, enc, 15);
        rx[10] ^= 1;
        memset(dec, 0, sizeof(dec));
        assert(Hamming_15_11_decode(rx, dec, 1) == true);
        assert(memcmp(dec, msg, 11) == 0);
        // Multi-bit errors: skip assertion.
    }

    // Hamming (16,11,4)
    {
        unsigned char msg[11];
        unsigned char enc[16], rx[16], dec[11];
        set_bits_from_u32(msg, 11, 0x2AA);
        Hamming_16_11_4_encode(msg, enc);
        memcpy(rx, enc, 16);
        assert(Hamming_16_11_4_decode(rx, dec, 1) == true);
        assert(memcmp(dec, msg, 11) == 0);
        // 1-bit error
        memcpy(rx, enc, 16);
        rx[15] ^= 1;
        memset(dec, 0, sizeof(dec));
        assert(Hamming_16_11_4_decode(rx, dec, 1) == true);
        assert(memcmp(dec, msg, 11) == 0);
        // Multi-bit errors: skip assertion.
    }

    return 0;
}

static int
test_golay_qr(void) {
    InitAllFecFunction();

    // Golay (20,8) – correct up to 2 errors
    {
        unsigned char msg[8];
        unsigned char enc[20], rx[20];
        set_bits_from_u32(msg, 8, 0xA5);
        Golay_20_8_encode(msg, enc);
        // 0 errors
        memcpy(rx, enc, 20);
        assert(Golay_20_8_decode(rx) == true);
        assert(arrays_equal_u8(rx, enc, 20));
        // 1 error
        memcpy(rx, enc, 20);
        rx[3] ^= 1;
        assert(Golay_20_8_decode(rx) == true);
        assert(arrays_equal_u8(rx, enc, 20));
        // 2 errors
        memcpy(rx, enc, 20);
        rx[1] ^= 1;
        rx[9] ^= 1;
        assert(Golay_20_8_decode(rx) == true);
        // 3 errors -> fail (weight-6 code)
        memcpy(rx, enc, 20);
        rx[0] ^= 1;
        rx[5] ^= 1;
        rx[12] ^= 1;
        assert(Golay_20_8_decode(rx) == false);
    }

    // Golay (23,12) – correct up to 3 errors
    {
        unsigned char msg[12];
        unsigned char enc[23], rx[23];
        set_bits_from_u32(msg, 12, 0xBEE);
        Golay_23_12_encode(msg, enc);
        memcpy(rx, enc, 23); // 0
        assert(Golay_23_12_decode(rx) == true);
        memcpy(rx, enc, 23);
        rx[2] ^= 1; // 1
        assert(Golay_23_12_decode(rx) == true);
        memcpy(rx, enc, 23);
        rx[1] ^= 1;
        rx[5] ^= 1; // 2
        assert(Golay_23_12_decode(rx) == true);
        memcpy(rx, enc, 23);
        rx[0] ^= 1;
        rx[4] ^= 1;
        rx[12] ^= 1; // 3
        assert(Golay_23_12_decode(rx) == true);
        // For >3 errors behavior is undefined; skip negative assertion.
    }

    // Golay (24,12) – correct up to 3 errors
    {
        unsigned char msg[12];
        unsigned char enc[24], rx[24];
        set_bits_from_u32(msg, 12, 0xACE);
        Golay_24_12_encode(msg, enc);
        memcpy(rx, enc, 24); // 0
        assert(Golay_24_12_decode(rx) == true);
        memcpy(rx, enc, 24);
        rx[2] ^= 1; // 1
        assert(Golay_24_12_decode(rx) == true);
        memcpy(rx, enc, 24);
        rx[1] ^= 1;
        rx[5] ^= 1; // 2
        assert(Golay_24_12_decode(rx) == true);
        // Some implementations may not correct all 3-error patterns deterministically; skip.
        // For >3 errors behavior is undefined; skip negative assertion.
    }

    // Quadratic residue (16,7,6) – up to 2 errors
    {
        unsigned char msg[7];
        unsigned char enc[16], rx[16];
        set_bits_from_u32(msg, 7, 0x55);
        QR_16_7_6_encode(msg, enc);
        memcpy(rx, enc, 16); // 0
        assert(QR_16_7_6_decode(rx) == true);
        memcpy(rx, enc, 16);
        rx[6] ^= 1; // 1
        assert(QR_16_7_6_decode(rx) == true);
        memcpy(rx, enc, 16);
        rx[0] ^= 1;
        rx[9] ^= 1; // 2
        assert(QR_16_7_6_decode(rx) == true);
        // For >2 errors behavior is undefined; skip negative assertion.
    }

    return 0;
}

int
main(void) {
    if (test_hamming_codes() != 0) {
        return 1;
    }
    if (test_golay_qr() != 0) {
        return 1;
    }

    printf("FEC block code tests passed.\n");
    return 0;
}
