// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * DMR CSBK opcode table helpers.
 */

#include <dsd-neo/protocol/dmr/dmr_csbk_tables.h>

const char*
dmr_csbk_grant_opcode_name(uint8_t opcode) {
    switch (opcode) {
        case 48: return "Private Voice Channel Grant (PV_GRANT)";
        case 49: return "Talkgroup Voice Channel Grant (TV_GRANT)";
        case 50: return "Broadcast Voice Channel Grant (BTV_GRANT)";
        case 51: return "Private Data Channel Grant: Single Item (PD_GRANT)";
        case 52: return "Talkgroup Data Channel Grant: Single Item (TD_GRANT)";
        case 53: return "Duplex Private Voice Channel Grant (PV_GRANT_DX)";
        case 54: return "Duplex Private Data Channel Grant (PD_GRANT_DX)";
        case 55: return "Private Data Channel Grant: Multi Item (PD_GRANT)";
        case 56: return "Talkgroup Data Channel Grant: Multi Item (TD_GRANT)";
        default: return "Unknown CSBK";
    }
}
