// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

// Implementation test for P25p1 MBF 3/4 decoder

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/p25/p25p1_mbf34.h>

// Local CRC helpers (copied from DMR utils, simplified) — forward decls
static uint16_t test_crc9(const uint8_t* bits, unsigned int len);
static uint32_t test_crc32(const uint8_t* bits, unsigned int len);

// Must match mapping used in src/protocol/p25/phase1/p25p1_mbf34.c
static const uint8_t interleave[98] = {0,  1,  8,  9,  16, 17, 24, 25, 32, 33, 40, 41, 48, 49, 56, 57, 64, 65, 72, 73,
                                       80, 81, 88, 89, 96, 97, 2,  3,  10, 11, 18, 19, 26, 27, 34, 35, 42, 43, 50, 51,
                                       58, 59, 66, 67, 74, 75, 82, 83, 90, 91, 4,  5,  12, 13, 20, 21, 28, 29, 36, 37,
                                       44, 45, 52, 53, 60, 61, 68, 69, 76, 77, 84, 85, 92, 93, 6,  7,  14, 15, 22, 23,
                                       30, 31, 38, 39, 46, 47, 54, 55, 62, 63, 70, 71, 78, 79, 86, 87, 94, 95};

static const uint8_t constellation_map[16] = {11, 12, 0, 7, 14, 9, 5, 2, 10, 13, 1, 6, 15, 8, 4, 3};
static const uint8_t fsm[64] = {0, 8,  4, 12, 2, 10, 6, 14, 4, 12, 2, 10, 6, 14, 0, 8, 1, 9,  5, 13, 3, 11,
                                7, 15, 5, 13, 3, 11, 7, 15, 1, 9,  3, 11, 7, 15, 1, 9, 5, 13, 7, 15, 1, 9,
                                5, 13, 3, 11, 2, 10, 6, 14, 0, 8,  4, 12, 6, 14, 0, 8, 4, 12, 2, 10};

static uint8_t inverse_const_map[16];

static void
init_inverse_map(void) {
    for (int i = 0; i < 16; i++) {
        inverse_const_map[constellation_map[i]] = (uint8_t)i;
    }
}

static void
bytes_to_bits_msbf(const uint8_t* in, size_t nbytes, uint8_t* out_bits) {
    for (size_t i = 0; i < nbytes; i++) {
        for (int b = 0; b < 8; b++) {
            out_bits[i * 8 + b] = (uint8_t)((in[i] >> (7 - b)) & 1);
        }
    }
}

static void
build_block(uint8_t dbsn, const uint8_t payload[16], uint8_t out[18]) {
    memset(out, 0, 18);
    out[0] = (uint8_t)(dbsn << 1); // reserve bit0 for CRC9 MSB
    // Compute CRC9 over 7 bits of DBSN + 128 payload bits (MSB-first)
    uint8_t bits[7 + 128];
    for (int i = 0; i < 7; i++) {
        bits[i] = (uint8_t)((dbsn >> (6 - i)) & 1);
    }
    uint8_t payload_bits[128];
    bytes_to_bits_msbf(payload, 16, payload_bits);
    memcpy(bits + 7, payload_bits, 128);
    uint16_t crc9 = 0; // declared below
    crc9 = test_crc9(bits, 135);
    out[0] |= (uint8_t)((crc9 >> 8) & 0x1);
    out[1] = (uint8_t)(crc9 & 0xFF);
    memcpy(out + 2, payload, 16);
}

static void
block_to_tribits(const uint8_t block[18], uint8_t tribits[49]) {
    uint8_t bits[144];
    bytes_to_bits_msbf(block, 18, bits);
    for (int i = 0; i < 48; i++) {
        tribits[i] = (uint8_t)((bits[i * 3 + 0] << 2) | (bits[i * 3 + 1] << 1) | bits[i * 3 + 2]);
    }
    tribits[48] = 0; // tail filler
}

static void
encode_tribits_to_dibits(const uint8_t tribits[49], uint8_t out_dibits[98]) {
    // Create sequential nibble stream via FSM then apply interleave
    uint8_t state = 0;
    uint8_t deint[98];
    int dp = 0;
    for (int i = 0; i < 49; i++) {
        uint8_t pt = fsm[(state * 8) + (tribits[i] & 7)];
        state = tribits[i] & 7;
        uint8_t nib = inverse_const_map[pt];
        deint[dp++] = (uint8_t)((nib >> 2) & 3);
        deint[dp++] = (uint8_t)(nib & 3);
    }
    for (int i = 0; i < 98; i++) {
        out_dibits[i] = deint[interleave[i]];
    }
}

static uint32_t
crc32_mbf_bytes(const uint8_t* buf, int nbits) {
    // Copy of p25p1_mdpu.c bit-wise CRC32 for cross-check
    const uint32_t poly = 0x04c11db7;
    uint64_t crc = 0;
    for (int i = 0; i < nbits; i++) {
        crc <<= 1;
        int b = (buf[i / 8] >> (7 - (i % 8))) & 1;
        if (((crc >> 32) ^ b) & 1) {
            crc ^= poly;
        }
    }
    crc = (crc & 0xffffffff) ^ 0xffffffff;
    return (uint32_t)crc;
}

// Local CRC helpers (copied from DMR utils, simplified)
static uint16_t
test_crc9(const uint8_t* bits, unsigned int len) {
    uint16_t crc = 0;
    const uint16_t poly = 0x059; // x^9 + x^6 + x^4 + x^3 + 1
    for (unsigned int i = 0; i < len; i++) {
        if (((crc >> 8) & 1) ^ (bits[i] & 1)) {
            crc = (crc << 1) ^ poly;
        } else {
            crc <<= 1;
        }
    }
    crc &= 0x01FF;
    crc ^= 0x01FF;
    return crc;
}

static uint32_t
test_crc32(const uint8_t* bits, unsigned int len) {
    uint32_t crc = 0;
    const uint32_t poly = 0x04C11DB7;
    for (unsigned int i = 0; i < len; i++) {
        if (((crc >> 31) & 1) ^ (bits[i] & 1)) {
            crc = (crc << 1) ^ poly;
        } else {
            crc <<= 1;
        }
    }
    // reverse byte order to match mbf_bytes form
    uint32_t a = (crc & 0xFF) << 24;
    uint32_t b = ((crc & 0xFF00) >> 8) << 16;
    uint32_t c = ((crc & 0xFF0000) >> 16) << 8;
    uint32_t d = ((crc & 0xFF000000) >> 24);
    crc = a | b | c | d;
    return crc;
}

int
main(void) {
    init_inverse_map();

    // 1) Build a synthetic block
    uint8_t payload[16];
    for (int i = 0; i < 16; i++) {
        payload[i] = (uint8_t)(0xA0 + i);
    }
    uint8_t dbsn = 0x2A; // 42
    uint8_t block[18];
    build_block(dbsn, payload, block);

    // 2) Encode to dibits and decode back
    uint8_t tribits[49];
    block_to_tribits(block, tribits);
    uint8_t in_dibits[98];
    memset(in_dibits, 0, sizeof(in_dibits));
    encode_tribits_to_dibits(tribits, in_dibits);

    uint8_t out_block[18];
    memset(out_block, 0, sizeof(out_block));
    int rc = p25_mbf34_decode(in_dibits, out_block);
    if (rc != 0) {
        fprintf(stderr, "decoder rc=%d\n", rc);
        return 10;
    }
    if (memcmp(block, out_block, 18) != 0) {
        fprintf(stderr, "decoded block mismatch\n");
        return 11;
    }

    // 3) Validate CRC9 from decoded block
    uint8_t bits[7 + 128];
    for (int i = 0; i < 7; i++) {
        bits[i] = (uint8_t)((out_block[0] >> (7 - i)) & 1);
    }
    uint8_t payload_bits[128];
    bytes_to_bits_msbf(out_block + 2, 16, payload_bits);
    memcpy(bits + 7, payload_bits, 128);
    uint16_t crc9_cmp = test_crc9(bits, 135);
    uint16_t crc9_ext = (uint16_t)(((out_block[0] & 1) << 8) | out_block[1]);
    if (crc9_cmp != crc9_ext) {
        fprintf(stderr, "CRC9 mismatch: %03X vs %03X\n", crc9_cmp, crc9_ext);
        return 12;
    }

    // 4) Validate CRC32 over payload bits using two forms
    uint8_t concat_payload[16];
    memcpy(concat_payload, out_block + 2, 16);
    uint32_t c32_bytes = crc32_mbf_bytes(concat_payload, 128);
    const uint32_t expected_crc32 = 0x96CF85AEu; // fixed payload pattern-dependent
    if (c32_bytes != expected_crc32) {
        fprintf(stderr, "CRC32 mismatch: %08X vs %08X\n", c32_bytes, expected_crc32);
        return 13;
    }

    // 5) Error injection: flip one dibit, ensure CRC9 no longer matches
    // flip a bunch of dibits to induce CRC9 failure
    for (int i = 0; i < 20; i += 2) {
        in_dibits[i] ^= 3;
    }
    uint8_t out_err[18];
    memset(out_err, 0, sizeof(out_err));
    rc = p25_mbf34_decode(in_dibits, out_err);
    if (rc != 0) {
        fprintf(stderr, "decoder(rc=%d) after error inj\n", rc);
        return 14;
    }
    for (int i = 0; i < 7; i++) {
        bits[i] = (uint8_t)((out_err[0] >> (7 - i)) & 1);
    }
    bytes_to_bits_msbf(out_err + 2, 16, payload_bits);
    memcpy(bits + 7, payload_bits, 128);
    crc9_cmp = test_crc9(bits, 135);
    crc9_ext = (uint16_t)(((out_err[0] & 1) << 8) | out_err[1]);
    if (crc9_cmp == crc9_ext) {
        fprintf(stderr, "CRC9 unexpectedly matched after error injection\n");
        return 15;
    }

    fprintf(stderr, "p25_mbf34 decode+CRC tests OK\n");
    return 0;
}
