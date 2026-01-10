// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * RC (Reverse Channel) CRC-7 test for DMR SB/RC handling.
 * Verifies masked extraction (mask 0x7A) against crc7 over 4-bit opcode.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/dmr/dmr_utils_api.h>

static void
append_bits(uint8_t* dst, unsigned start, uint32_t val, unsigned k) {
    for (unsigned i = 0; i < k; i++) {
        unsigned bit = (val >> (k - 1 - i)) & 1U;
        dst[start + i] = (uint8_t)bit;
    }
}

static void
test_rc_crc7_mask(void) {
    // sbrc_return layout for RC in code:
    // bits[0..3]  = 4-bit opcode payload
    // bits[4..10] = 7-bit CRC masked on-air (crc ^ 0x7A), MSB-first
    uint8_t sbrc_return[32];
    memset(sbrc_return, 0, sizeof(sbrc_return));

    // Example 4-bit opcode
    uint8_t opcode_bits[4] = {1, 0, 1, 1};
    memcpy(&sbrc_return[0], opcode_bits, 4);

    // Compute CRC-7 over the 4 opcode bits
    uint8_t crc = crc7(sbrc_return, 4);
    uint8_t masked = (uint8_t)(crc ^ 0x7A);

    // Place masked CRC bits at [4..10]
    append_bits(sbrc_return, 4, masked, 7);

    // Emulate extraction as in dmr_le.c
    uint16_t ext = 0;
    for (int i = 0; i < 7; i++) {
        ext = (uint16_t)((ext << 1) | (sbrc_return[4 + i] & 1));
    }
    ext ^= 0x7A;

    uint16_t cmp = crc7(sbrc_return, 4);
    assert(ext == cmp);

    // Negative: flip one opcode bit, CRC should mismatch
    sbrc_return[2] ^= 1U; // toggle an opcode bit
    cmp = crc7(sbrc_return, 4);
    assert(ext != cmp);
}

int
main(void) {
    test_rc_crc7_mask();
    printf("DMR RC CRC-7: OK\n");
    return 0;
}
