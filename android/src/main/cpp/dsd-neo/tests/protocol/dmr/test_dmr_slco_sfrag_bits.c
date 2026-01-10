// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * DMR SLCO single-fragment (LCSS=0) bit-level checks:
 *  - Hamming(17,12,3): codeword with opcode=1, ts1=Group Voice, ts2=Idle passes correction
 *  - All-zero codeword (SLCO NULL) passes correction
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <dsd-neo/protocol/dmr/dmr_utils_api.h>

static void
slc17_build(uint8_t slc[17], uint8_t slco, uint8_t ts1_act, uint8_t ts2_act) {
    memset(slc, 0, 17);
    for (int i = 0; i < 4; i++) {
        slc[i] = (uint8_t)((slco >> (3 - i)) & 1U);
    }
    for (int i = 0; i < 4; i++) {
        slc[4 + i] = (uint8_t)((ts1_act >> (3 - i)) & 1U);
    }
    for (int i = 0; i < 4; i++) {
        slc[8 + i] = (uint8_t)((ts2_act >> (3 - i)) & 1U);
    }
    // Parity bits per Hamming17123
    bool c0 = slc[0] ^ slc[1] ^ slc[2] ^ slc[3] ^ slc[6] ^ slc[7] ^ slc[9];
    bool c1 = slc[0] ^ slc[1] ^ slc[2] ^ slc[3] ^ slc[4] ^ slc[7] ^ slc[8] ^ slc[10];
    bool c2 = slc[1] ^ slc[2] ^ slc[3] ^ slc[4] ^ slc[5] ^ slc[8] ^ slc[9] ^ slc[11];
    bool c3 = slc[0] ^ slc[1] ^ slc[4] ^ slc[5] ^ slc[7] ^ slc[10];
    bool c4 = slc[0] ^ slc[1] ^ slc[2] ^ slc[5] ^ slc[6] ^ slc[8] ^ slc[11];
    slc[12] = (uint8_t)c0;
    slc[13] = (uint8_t)c1;
    slc[14] = (uint8_t)c2;
    slc[15] = (uint8_t)c3;
    slc[16] = (uint8_t)c4;
}

int
main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // All-zero NULL codeword
    uint8_t slc[17];
    memset(slc, 0, sizeof(slc));
    assert(Hamming17123(slc) == true);

    // Activity Update: opcode=1, ts1=0x8 (Group Voice), ts2=0x0
    slc17_build(slc, 0x1, 0x8, 0x0);
    assert(Hamming17123(slc) == true);

    return 0;
}
