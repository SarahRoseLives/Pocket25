// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * CRC mask tests for DMR: verifies 24-bit LC masks (VLC/TLC) via
 * ComputeAndCorrectFullLinkControlCrc and 16-bit CCITT masks for
 * PI/CSBK/MBC Header/Data Header/USBD via ComputeCrcCCITT.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/dmr/dmr_utils_api.h>

// Minimal forward declarations to keep test dependencies narrow.
#define RS_12_9_DATASIZE     9
#define RS_12_9_CHECKSUMSIZE 3

typedef struct {
    uint8_t data[RS_12_9_DATASIZE + RS_12_9_CHECKSUMSIZE];
} rs_12_9_codeword_t;

typedef struct {
    uint8_t bytes[3];
} rs_12_9_checksum_t;

rs_12_9_checksum_t* rs_12_9_calc_checksum(rs_12_9_codeword_t* codeword);

// Forward declaration of LC CRC helper
uint32_t ComputeAndCorrectFullLinkControlCrc(uint8_t* FullLinkControlDataBytes, uint32_t* CRCComputed,
                                             uint32_t CRCMask);

static void
append_bits(uint8_t* dst, unsigned start, uint32_t val, unsigned k) {
    // Append k bits of val MSB-first into dst starting at index 'start'
    for (unsigned i = 0; i < k; i++) {
        unsigned bit = (val >> (k - 1 - i)) & 1U;
        dst[start + i] = (uint8_t)bit;
    }
}

static void
test_lc_crc24_mask(uint32_t mask24) {
    // Build a 12-byte LC codeword with valid RS(12,9) parity, then mask it.
    rs_12_9_codeword_t cw;
    memset(&cw, 0, sizeof(cw));

    // Deterministic 9-byte LC payload
    for (int i = 0; i < RS_12_9_DATASIZE; i++) {
        cw.data[i] = (uint8_t)(0x10 + i * 7);
    }

    rs_12_9_checksum_t* chk = rs_12_9_calc_checksum(&cw);
    // Save unmasked parity for later comparison
    uint32_t parity_unmasked =
        ((uint32_t)chk->bytes[0] << 16) | ((uint32_t)chk->bytes[1] << 8) | ((uint32_t)chk->bytes[2]);

    // Write masked parity into cw
    cw.data[9] = (uint8_t)(chk->bytes[0] ^ (uint8_t)(mask24 >> 16));
    cw.data[10] = (uint8_t)(chk->bytes[1] ^ (uint8_t)(mask24 >> 8));
    cw.data[11] = (uint8_t)(chk->bytes[2] ^ (uint8_t)(mask24 >> 0));

    // Feed into CRC check/correct with the same mask
    uint32_t crc_computed = 0;
    uint32_t ok = ComputeAndCorrectFullLinkControlCrc(cw.data, &crc_computed, mask24);
    assert(ok == 1);
    assert(crc_computed == parity_unmasked);

    // Ensure output remains masked
    assert(cw.data[9] == (uint8_t)(chk->bytes[0] ^ (uint8_t)(mask24 >> 16)));
    assert(cw.data[10] == (uint8_t)(chk->bytes[1] ^ (uint8_t)(mask24 >> 8)));
    assert(cw.data[11] == (uint8_t)(chk->bytes[2] ^ (uint8_t)(mask24 >> 0)));

    // Note: Do not assert failure cases here; RS(12,9) correction behavior may
    // vary with error locations. This test focuses on mask application success.
}

static void
test_ccitt16_mask(uint16_t mask16) {
    // Simulate BPTC(196,96) deinterleaved payload: first 80 bits are info,
    // last 16 bits [80..95] carry the masked CCITT CRC.
    uint8_t bits[96];
    memset(bits, 0, sizeof(bits));

    // Fill 80 info bits with a deterministic pattern
    for (unsigned i = 0; i < 80; i++) {
        bits[i] = (uint8_t)(((i * 5) ^ 0x3) & 1U);
    }

    // Compute CCITT over first 80 bits
    uint16_t ccitt = ComputeCrcCCITT(bits);
    uint16_t masked = (uint16_t)(ccitt ^ mask16);

    // Place masked CRC at [80..95], MSB-first
    append_bits(bits, 80, masked, 16);

    // Emulate extraction
    uint16_t ext = (uint16_t)ConvertBitIntoBytes(&bits[80], 16);
    ext ^= mask16;
    uint16_t cmp = ComputeCrcCCITT(bits);
    assert(ext == cmp);

    // Negative: flip an info bit and require mismatch
    bits[37] ^= 1U;
    cmp = ComputeCrcCCITT(bits);
    assert(ext != cmp);
}

int
main(void) {
    // 24-bit LC masks (VLC/TLC)
    test_lc_crc24_mask(0x969696); // VLC
    test_lc_crc24_mask(0x999999); // TLC

    // 16-bit CCITT masks for other PDUs
    test_ccitt16_mask(0x6969); // PI
    test_ccitt16_mask(0xA5A5); // CSBK
    test_ccitt16_mask(0xAAAA); // MBC Header
    test_ccitt16_mask(0xCCCC); // Data Header
    test_ccitt16_mask(0x3333); // USBD

    printf("DMR CRC masks: OK\n");
    return 0;
}
