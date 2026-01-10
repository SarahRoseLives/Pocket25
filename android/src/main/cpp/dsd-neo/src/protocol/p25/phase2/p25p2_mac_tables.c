// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 2 MAC opcode length table and vendor overrides.
 */

#include <stdint.h>

#include <dsd-neo/protocol/p25/p25p2_mac_tables.h>

// Length semantics: number of octets following the opcode byte
// (i.e., includes MFID and payload, excludes the opcode itself).
static const uint8_t mac_msg_len[256] = {
    0,  7,  8,  7,  0,  16, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //0F
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //1F
    0,  14, 15, 0,  0,  15, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //2F
    5,  7,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //3F
    9,  7,  9,  0,  9,  8,  9,  0,  10, 10, 9,  0,  10, 0,  0,  0,  //4F
    0,  0,  0,  0,  9,  7,  0,  0,  10, 0,  7,  0,  10, 8,  14, 7,  //5F
    9,  9,  0,  0,  9,  0,  0,  9,  10, 0,  7,  10, 10, 7,  0,  9,  //6F
    9,  29, 9,  9,  9,  9,  10, 13, 9,  9,  9,  11, 9,  9,  0,  0,  //7F
    8,  18, 0,  7,  11, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  7,  //8F (Harris variants observed)
    0,  17, 0,  0,  0,  17, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //9F (Moto 0x91/0x95 observed as 17)
    16, 0,  0,  11, 13, 11, 11, 11, 10, 0,  0,  0,  0,  0,  0,  0,  //AF
    17, 0,  0,  0,  0,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //BF (B0 observed as 17; B5 observed as 5)
    11, 0,  0,  8,  15, 12, 15, 32, 12, 12, 0,  27, 14, 29, 29, 32, //CF
    0,  0,  0,  0,  0,  0,  9,  0,  14, 29, 11, 27, 14, 0,  40, 11, //DF
    28, 0,  0,  14, 17, 14, 0,  0,  16, 8,  11, 0,  13, 19, 0,  0,  //EF
    0,  29, 16, 14, 0,  0,  12, 0,  22, 29, 11, 13, 11, 0,  15, 0   //FF (F1 set to 29)
};

static inline int
base_len_for(uint8_t opcode) {
    return mac_msg_len[opcode];
}

int
p25p2_mac_len_for(uint8_t mfid, uint8_t opcode) {
    int base = base_len_for(opcode);
    if (base != 0) {
        return base;
    }

    // Vendor overrides observed in the wild when the base table is zero.
    // Motorola
    if (mfid == 0x90 && (opcode == 0x91 || opcode == 0x95)) {
        return 17;
    }
    // Harris (generic observed length)
    if (mfid == 0xB0) {
        return 17;
    }
    // Tait (generic observed length)
    if (mfid == 0xB5) {
        return 5;
    }
    // Harris additional (0x81/0x8F MFIDs used with short fixed messages)
    if (mfid == 0x81 || mfid == 0x8F) {
        return 7;
    }

    return base; // remains zero if unknown
}
