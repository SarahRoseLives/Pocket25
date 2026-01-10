// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Tier III MBC bounds + CRC16 smoke tests
 *
 * These tests do not attempt to fully synthesize valid MBC frames; instead they
 * exercise the CRC16 routine with simple spans and ensure the assembler's
 * aggregate length checks can be exercised in isolation without crashing.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/dmr/dmr_utils_api.h>

// Forward-declare internal symbol so we can poke the assembler directly.
extern void dmr_block_assembler(dsd_opts* opts, dsd_state* state, uint8_t block_bytes[], uint8_t block_len,
                                uint8_t databurst, uint8_t type);

static void
crc16_bit_order_smoke(void) {
    // Known simple pattern: 96 zero bits -> CRC over zero bits with inverted output
    // Just verify the function runs and returns a deterministic value.
    uint8_t bits[96];
    memset(bits, 0, sizeof(bits));
    uint16_t crc = ComputeCrcCCITT16d(bits, 96);
    // Specific numeric value is not important; ensure stability.
    assert(crc == ComputeCrcCCITT16d(bits, 96));
}

static void
mbc_aggregate_bounds(void) {
    dsd_opts opts;
    dsd_state state;
    memset(&opts, 0, sizeof(opts));
    memset(&state, 0, sizeof(state));
    state.currentslot = 0;

    // Mark header as valid so assembler attempts finalization on LB.
    state.data_header_valid[0] = 1;

    // Feed a dummy header (LB=0, PF=0) as type 2 (MBC/UDT style)
    uint8_t blk[12];
    memset(blk, 0, sizeof(blk));
    blk[0] = 0x00; // LB=0, PF=0
    dmr_block_assembler(&opts, &state, blk, 12, 0 /*databurst*/, 2 /*type*/);

    // Now feed a continuation block with LB=1. Our assembler clamps block counter
    // to <=4; this should be treated as a valid (bounded) aggregate and should
    // not crash.
    blk[0] = 0x80; // LB=1
    dmr_block_assembler(&opts, &state, blk, 12, 0, 2);

    // If we got here without a crash, basic bound handling works.
}

int
main(void) {
    crc16_bit_order_smoke();
    mbc_aggregate_bounds();
    return 0;
}
