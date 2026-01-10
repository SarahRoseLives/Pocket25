// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/dmr/dmr_utils_api.h>

int
main(void) {
    // Build a 16-bit pattern 0xABCD in MSB-first bit order
    uint8_t bits[16];
    uint16_t val = 0xABCD;
    for (int i = 0; i < 16; i++) {
        bits[i] = (uint8_t)((val >> (15 - i)) & 1);
    }
    uint64_t out = ConvertBitIntoBytes(bits, 16);
    assert(out == 0xABCDULL);

    // Zero-length converts to 0
    assert(ConvertBitIntoBytes(bits, 0) == 0ULL);

    printf("DMR bit utils: OK\n");
    return 0;
}
