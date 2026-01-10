// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Confirmed data CRC-9 bit-span/order tests for DMR.
 * Verifies confirmed data CRC spans used by decoder:
 * information bits plus DBSN (7 bits), MSB-first.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/dmr/dmr_utils_api.h>

static void
append_bits(uint8_t* dst, unsigned start, uint32_t val, unsigned k) {
    // Append k bits of val MSB-first into dst starting at index 'start'
    for (unsigned i = 0; i < k; i++) {
        unsigned bit = (val >> (k - 1 - i)) & 1U;
        dst[start + i] = (uint8_t)bit;
    }
}

static void
test_r12_confirmed_crc9(void) {
    // Construct a 96-bit deinterleaved BPTC payload layout for confirmed 1/2-rate:
    // bits [0..6]   = DBSN (7 bits)
    // bits [7..15]  = CRC9 (masked)
    // bits [16..95] = 80 information bits (10 octets)
    uint8_t bits[96];
    memset(bits, 0, sizeof(bits));

    // Deterministic payload pattern: 80 bits
    uint8_t payload[80];
    for (unsigned i = 0; i < 80; i++) {
        payload[i] = (uint8_t)((i * 5 + 3) & 1U);
    }

    // DBSN arbitrary 7-bit value
    uint32_t dbsn = 0x35; // 53
    append_bits(bits, 0, dbsn & 0x7F, 7);

    // Place payload at [16..95]
    for (unsigned i = 0; i < 80; i++) {
        bits[16 + i] = payload[i];
    }

    // Compute CRC9 over 80 info bits + 7 DBSN bits
    uint8_t span[87];
    for (unsigned i = 0; i < 80; i++) {
        span[i] = payload[i];
    }
    for (unsigned i = 0; i < 7; i++) {
        span[80 + i] = bits[i];
    }
    uint16_t crc9 = ComputeCrc9Bit(span, 87);

    // Apply mask per code path for 1/2-rate confirmed: 0x0F0
    uint16_t masked = (uint16_t)(crc9 ^ 0x0F0);

    // Place CRC into [7..15], MSB-first
    append_bits(bits, 7, masked & 0x1FF, 9);

    // Emulate extraction/compare performed in handler
    uint32_t ext = (uint32_t)ConvertBitIntoBytes(&bits[7], 9);
    ext ^= 0x0F0;
    // Rebuild span for CRC (payload + DBSN)
    for (unsigned i = 0; i < 80; i++) {
        span[i] = bits[16 + i];
    }
    for (unsigned i = 0; i < 7; i++) {
        span[80 + i] = bits[i];
    }
    uint16_t cmp = ComputeCrc9Bit(span, 87);
    assert(ext == cmp);

    // Negative test: flip a payload bit and ensure mismatch
    bits[16 + 7] ^= 1;
    for (unsigned i = 0; i < 80; i++) {
        span[i] = bits[16 + i];
    }
    for (unsigned i = 0; i < 7; i++) {
        span[80 + i] = bits[i];
    }
    cmp = ComputeCrc9Bit(span, 87);
    assert(ext != cmp);
}

static void
test_r1_confirmed_crc9(void) {
    // Construct a 196-bit raw burst bit array layout for confirmed rate 1:
    // bits [0..6]   = DBSN (7 bits)
    // bits [7..15]  = CRC9 (masked)
    // bits [16..95] = first 80 information bits
    // bits [96..99] = pad bits
    // bits [100..195] = remaining 96 information bits
    uint8_t info[196];
    memset(info, 0, sizeof(info));

    // 176 payload bits
    uint8_t payload[176];
    for (unsigned i = 0; i < 176; i++) {
        payload[i] = (uint8_t)(((i ^ 0xA) + 1) & 1U);
    }

    // DBSN
    uint32_t dbsn = 0x12; // arbitrary
    append_bits(info, 0, dbsn & 0x7F, 7);

    // Place first 80 bits at [16..95]
    for (unsigned i = 0; i < 80; i++) {
        info[16 + i] = payload[i];
    }
    // pad bits [96..99] already zero
    // Place remaining 96 bits at [100..195]
    for (unsigned i = 0; i < 96; i++) {
        info[100 + i] = payload[80 + i];
    }

    // Compute CRC9 over 176 info bits + 7 DBSN bits
    uint8_t span[183];
    for (unsigned i = 0; i < 176; i++) {
        span[i] = payload[i];
    }
    for (unsigned i = 0; i < 7; i++) {
        span[176 + i] = info[i];
    }
    uint16_t crc9 = ComputeCrc9Bit(span, 183);
    uint16_t masked = (uint16_t)(crc9 ^ 0x10F);
    append_bits(info, 7, masked & 0x1FF, 9);

    // Emulate extraction/compare performed in handler
    uint32_t ext = (uint32_t)ConvertBitIntoBytes(&info[7], 9);
    ext ^= 0x10F;

    // Rebuild contiguous span for CRC: info bits + DBSN
    unsigned k = 0;
    for (unsigned i = 16; i < 96; i++) {
        span[k++] = info[i];
    }
    for (unsigned i = 100; i < 196; i++) {
        span[k++] = info[i];
    }
    for (unsigned i = 0; i < 7; i++) {
        span[k++] = info[i];
    }
    assert(k == 183);
    uint16_t cmp = ComputeCrc9Bit(span, k);
    assert(ext == cmp);

    // Negative test: flip a payload bit and ensure mismatch
    info[16 + 31] ^= 1;
    k = 0;
    for (unsigned i = 16; i < 96; i++) {
        span[k++] = info[i];
    }
    for (unsigned i = 100; i < 196; i++) {
        span[k++] = info[i];
    }
    for (unsigned i = 0; i < 7; i++) {
        span[k++] = info[i];
    }
    cmp = ComputeCrc9Bit(span, k);
    assert(ext != cmp);
}

static void
test_r34_confirmed_crc9(void) {
    // Build DMR_PDU_bits as in the trellis path:
    // [0..6]=DBSN, [7..15]=CRC9(masked), [16..143]=128 info bits
    uint8_t bits[144];
    memset(bits, 0, sizeof(bits));
    // payload 128 bits
    uint8_t payload[128];
    for (unsigned i = 0; i < 128; i++) {
        payload[i] = (uint8_t)((i * 7 + 1) & 1U);
    }
    // DBSN arbitrary
    uint32_t dbsn = 0x5A & 0x7F;
    append_bits(bits, 0, dbsn, 7);
    // place 128 bits at [16..143]
    for (unsigned i = 0; i < 128; i++) {
        bits[16 + i] = payload[i];
    }
    // Compute CRC9 over 128 info bits + 7 DBSN bits
    uint8_t span[135];
    for (unsigned i = 0; i < 128; i++) {
        span[i] = payload[i];
    }
    for (unsigned i = 0; i < 7; i++) {
        span[128 + i] = bits[i];
    }
    uint16_t crc9 = ComputeCrc9Bit(span, 135);
    uint16_t masked = (uint16_t)(crc9 ^ 0x1FF);
    append_bits(bits, 7, masked & 0x1FF, 9);
    // emulate extraction and compare
    uint32_t ext = (uint32_t)ConvertBitIntoBytes(&bits[7], 9);
    ext ^= 0x1FF;
    for (unsigned i = 0; i < 128; i++) {
        span[i] = bits[16 + i];
    }
    for (unsigned i = 0; i < 7; i++) {
        span[128 + i] = bits[i];
    }
    uint16_t cmp = ComputeCrc9Bit(span, 135);
    assert(ext == cmp);
    // flip one info bit
    bits[16 + 12] ^= 1;
    for (unsigned i = 0; i < 128; i++) {
        span[i] = bits[16 + i];
    }
    for (unsigned i = 0; i < 7; i++) {
        span[128 + i] = bits[i];
    }
    cmp = ComputeCrc9Bit(span, 135);
    assert(ext != cmp);
}

int
main(void) {
    test_r12_confirmed_crc9();
    test_r1_confirmed_crc9();
    test_r34_confirmed_crc9();
    printf("DMR confirmed CRC9 spans: OK\n");
    return 0;
}
