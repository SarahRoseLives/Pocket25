// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Prototype from crypt-aes.c
void aes_ofb_keystream_output(uint8_t* iv, uint8_t* key, uint8_t* output, int type, int nblocks);

static int
test_aes128_ofb(void) {
    // FIPS-197 example: AES-128 encrypt of block 00112233445566778899aabbccddeeff
    // with key 000102030405060708090a0b0c0d0e0f produces 69c4e0d86a7b0430d8cdb78070b4c55a
    const uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                             0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t iv[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                            0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint8_t expect[16] = {0x69, 0xC4, 0xE0, 0xD8, 0x6A, 0x7B, 0x04, 0x30,
                                0xD8, 0xCD, 0xB7, 0x80, 0x70, 0xB4, 0xC5, 0x5A};
    uint8_t out[16] = {0};
    aes_ofb_keystream_output((uint8_t*)iv, (uint8_t*)key, out, /*AES-128*/ 0, 1);
    if (memcmp(out, expect, 16) != 0) {
        fprintf(stderr, "AES-128 OFB: mismatch\n");
        return 1;
    }
    return 0;
}

static int
test_aes256_ofb(void) {
    // AES-256 encrypt of block 00112233445566778899aabbccddeeff
    // with key 000102...1f produces 8ea2b7ca516745bfeafc49904b496089 (FIPS-197-derived example)
    const uint8_t key[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
                             0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                             0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    const uint8_t iv[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                            0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint8_t expect[16] = {0x8E, 0xA2, 0xB7, 0xCA, 0x51, 0x67, 0x45, 0xBF,
                                0xEA, 0xFC, 0x49, 0x90, 0x4B, 0x49, 0x60, 0x89};
    uint8_t out[16] = {0};
    // The keystream block equals AES-encrypt(IV)
    aes_ofb_keystream_output((uint8_t*)iv, (uint8_t*)key, out, /*AES-256*/ 2, 1);
    if (memcmp(out, expect, 16) != 0) {
        fprintf(stderr, "AES-256 OFB: mismatch\n");
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    rc |= test_aes128_ofb();
    rc |= test_aes256_ofb();
    if (rc == 0) {
        fprintf(stderr, "AES OFB tests: OK\n");
    }
    return rc;
}
