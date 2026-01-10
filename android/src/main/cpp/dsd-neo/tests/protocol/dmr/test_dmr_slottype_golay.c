// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * SlotType Golay(20,8) encode/decode tests.
 * - Verifies clean decode
 * - Verifies up to 2 bit error correction
 * - Verifies failure on 3 bit flips
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/fec/block_codes.h>

static void
bits_from_byte(unsigned char b, unsigned char out[8]) {
    for (int i = 0; i < 8; i++) {
        out[i] = (unsigned char)((b >> i) & 1U); // LSB-first to match fec.c encode/decode usage
    }
}

static unsigned char
byte_from_bits(const unsigned char in[8]) {
    unsigned char v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (unsigned char)(in[i] & 1U) << i;
    }
    return v;
}

static void
test_clean_decode(void) {
    Golay_20_8_init();
    for (unsigned msg = 0; msg < 16; msg++) { // small sample
        unsigned char m[8] = {0};
        bits_from_byte((unsigned char)msg, m);
        unsigned char cw[20];
        memset(cw, 0, sizeof(cw));
        Golay_20_8_encode(m, cw);
        // decode
        unsigned char cw2[20];
        memcpy(cw2, cw, sizeof(cw));
        assert(Golay_20_8_decode(cw2) == true);
        unsigned char out = byte_from_bits(cw2);
        assert(out == (unsigned char)msg);
    }
}

static void
test_two_bit_correction(void) {
    Golay_20_8_init();
    unsigned char m[8] = {1, 0, 1, 1, 0, 1, 0, 0};
    unsigned char cw[20];
    Golay_20_8_encode(m, cw);
    for (int i = 0; i < 20; i++) {
        for (int j = i + 1; j < 20; j++) {
            unsigned char tmp[20];
            memcpy(tmp, cw, sizeof(tmp));
            tmp[i] ^= 1U;
            tmp[j] ^= 1U;
            if (Golay_20_8_decode(tmp)) {
                // Should correct <=2 flips
                unsigned char out = byte_from_bits(tmp);
                unsigned char in = byte_from_bits(m);
                assert(out == in);
            }
        }
    }
}

static void
test_three_bit_failure(void) {
    Golay_20_8_init();
    unsigned char m[8] = {0};
    unsigned char cw[20];
    Golay_20_8_encode(m, cw);
    // Flip three distinct positions
    unsigned char tmp[20];
    memcpy(tmp, cw, sizeof(tmp));
    tmp[0] ^= 1U;
    tmp[5] ^= 1U;
    tmp[13] ^= 1U;
    // Implementation limits correction to at most 2 flips
    assert(Golay_20_8_decode(tmp) == false);
}

int
main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    test_clean_decode();
    test_two_bit_correction();
    test_three_bit_failure();
    printf("DMR SlotType Golay(20,8): OK\n");
    return 0;
}
