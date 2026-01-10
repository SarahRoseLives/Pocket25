// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/dsp/symbol_levels.h>

float
dsd_symbol_level_from_dibit(uint8_t dibit) {
    switch (dibit & 0x3u) {
        case 0: return 1.0f;  // +1
        case 1: return 3.0f;  // +3
        case 2: return -1.0f; // -1
        case 3: return -3.0f; // -3
        default: break;
    }
    return 0.0f;
}
