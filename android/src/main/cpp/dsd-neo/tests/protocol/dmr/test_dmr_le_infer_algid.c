// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/fec/block_codes.h>
#include <dsd-neo/protocol/dmr/dmr.h>
#include <dsd-neo/protocol/dmr/dmr_utils_api.h>

static uint8_t
bits_to_u4_msb(const unsigned char bits4[4]) {
    uint8_t v = 0;
    for (int i = 0; i < 4; i++) {
        v = (uint8_t)((v << 1) | (bits4[i] & 1U));
    }
    return v & 0xF;
}

static void
fill_le_fragments_for_mi(dsd_state* state, uint8_t slot, uint32_t mi32) {
    // Build 36-bit MI+CRC (MSB-first): 32-bit MI then CRC4.
    uint8_t mi_bits32[32];
    for (int i = 0; i < 32; i++) {
        mi_bits32[i] = (uint8_t)((mi32 >> (31 - i)) & 1U);
    }
    const uint8_t crc = crc4(mi_bits32, 32);

    unsigned char msg36[36];
    memset(msg36, 0, sizeof(msg36));
    for (int i = 0; i < 32; i++) {
        msg36[i] = (unsigned char)(mi_bits32[i] & 1U);
    }
    for (int i = 0; i < 4; i++) {
        msg36[32 + i] = (unsigned char)((crc >> (3 - i)) & 1U);
    }

    // Golay(24,12) parity bits for each 12-bit chunk (MSB-first).
    unsigned char go36[36];
    memset(go36, 0, sizeof(go36));
    for (int chunk = 0; chunk < 3; chunk++) {
        unsigned char orig[12];
        unsigned char enc[24];
        memset(orig, 0, sizeof(orig));
        memset(enc, 0, sizeof(enc));
        for (int i = 0; i < 12; i++) {
            orig[i] = msg36[chunk * 12 + i] & 1U;
        }
        Golay_24_12_encode(orig, enc);
        for (int i = 0; i < 12; i++) {
            assert((enc[i] & 1U) == (orig[i] & 1U)); // systematic encoding assumed by LE split
            go36[chunk * 12 + i] = enc[12 + i] & 1U;
        }
    }

    // Pack 12-bit chunks into 3 nibbles across vc=1..3 (MI) and vc=4..6 (GO), per column.
    for (int col = 0; col < 3; col++) {
        for (int row = 0; row < 3; row++) {
            const int bit_base = col * 12 + row * 4;
            const uint8_t mi_nib = bits_to_u4_msb(&msg36[bit_base]);
            const uint8_t go_nib = bits_to_u4_msb(&go36[bit_base]);
            state->late_entry_mi_fragment[slot][1 + row][col] = mi_nib;
            state->late_entry_mi_fragment[slot][4 + row][col] = go_nib;
        }
    }
}

static void
run_case(unsigned long long key, uint32_t mi, int expect_algid) {
    static dsd_opts opts;
    static dsd_state state;
    memset(&opts, 0, sizeof(opts));
    memset(&state, 0, sizeof(state));

    state.currentslot = 0;
    state.M = 0;
    state.R = key;
    state.RR = key;
    state.dmr_so = 0x40U;

    fill_le_fragments_for_mi(&state, 0, mi);
    dmr_late_entry_mi(&opts, &state);

    assert(state.payload_algid == expect_algid);
    assert(state.payload_keyid == 0xFF);
    if (expect_algid == 0x21) {
        assert((uint32_t)state.payload_mi == mi);
    } else if (expect_algid == 0x22) {
        assert(state.payload_miP != 0);
        assert((uint32_t)(state.payload_miP >> 32) == mi);
        assert((uint32_t)state.payload_mi == (uint32_t)state.payload_miP);
    } else {
        assert(0 && "unexpected algid");
    }
}

int
main(void) {
    InitAllFecFunction();

    // RC4 key (<=40-bit) should infer ALG ID 0x21.
    run_case(0xE3AE36E22AULL, 0xEC60C8BEU, 0x21);

    // DES key (>40-bit) should infer ALG ID 0x22.
    run_case(0x0123456789ABCDULL, 0x11223344U, 0x22);

    printf("DMR LE infer algid: OK\n");
    return 0;
}
