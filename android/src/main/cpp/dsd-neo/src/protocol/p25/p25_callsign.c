// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 WACN/SysID to FCC Callsign conversion.
 *
 * Algorithm based on APCO P25 Steering Committee specification (April 6, 2001)
 * and Eric Carlson's reference implementation.
 *
 * Reference: https://www.ericcarlson.net/project-25-callsign.html
 */

#include <dsd-neo/protocol/p25/p25_callsign.h>

#include <stdio.h>
#include <string.h>

/* P25 Radix-50 character table (40 characters):
 * Index 0: space
 * Index 1-26: A-Z
 * Index 27: $
 * Index 28: .
 * Index 29: ?
 * Index 30-39: 0-9
 */
static const char p25_radix50[41] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789";

/*
 * Known manufacturer/generic WACNs where the callsign decode is meaningless.
 *
 * Per APCO P25, WACNs were intended to be derived from FCC callsigns, but in
 * practice many manufacturers use generic WACNs:
 *
 * - 0xBEE00: Motorola's default WACN used for most ASTRO 25/Smartzone systems
 * - 0xA4xxx: Harris systems often use WACNs starting with A4
 *
 * For these WACNs, the Radix-50 decode produces meaningless strings like
 * "0UX..." (for BEE00) that don't correspond to actual FCC callsigns.
 */
static int
p25_is_generic_wacn(uint32_t wacn) {
    /* Motorola BEE00 - the most common generic WACN */
    if (wacn == 0xBEE00) {
        return 1;
    }

    /* Harris systems commonly use A4xxx range */
    if ((wacn & 0xFF000) == 0xA4000) {
        return 1;
    }

    return 0;
}

void
p25_wacn_sysid_to_callsign(uint32_t wacn, uint16_t sysid, char out[7]) {
    /* Clamp to valid ranges */
    wacn &= 0xFFFFF; /* 20 bits */
    sysid &= 0xFFF;  /* 12 bits */

    /* Skip callsign decode for known generic/manufacturer WACNs */
    if (p25_is_generic_wacn(wacn)) {
        out[0] = '\0';
        return;
    }

    /* Compute intermediate values per the APCO algorithm */
    uint32_t n1 = wacn / 16;
    uint32_t n2 = 4096 * (wacn % 16) + sysid;

    /* Extract 6 base-40 digits with bounds checking */
    /* Characters 1-3 from n1 */
    uint32_t idx0 = n1 / 1600;
    uint32_t idx1 = (n1 / 40) % 40;
    uint32_t idx2 = n1 % 40;
    out[0] = (idx0 < 40) ? p25_radix50[idx0] : '?';
    out[1] = (idx1 < 40) ? p25_radix50[idx1] : '?';
    out[2] = (idx2 < 40) ? p25_radix50[idx2] : '?';

    /* Characters 4-6 from n2 */
    uint32_t idx3 = n2 / 1600;
    uint32_t idx4 = (n2 / 40) % 40;
    uint32_t idx5 = n2 % 40;
    out[3] = (idx3 < 40) ? p25_radix50[idx3] : '?';
    out[4] = (idx4 < 40) ? p25_radix50[idx4] : '?';
    out[5] = (idx5 < 40) ? p25_radix50[idx5] : '?';

    out[6] = '\0';
}

int
p25_format_wacn_sysid(uint32_t wacn, uint16_t sysid, char* out, int out_len) {
    if (!out || out_len < 1) {
        return 0;
    }

    char callsign[7];
    p25_wacn_sysid_to_callsign(wacn, sysid, callsign);

    /* Short-circuit if no callsign was produced (generic WACN) */
    if (callsign[0] == '\0') {
        int n = snprintf(out, (size_t)out_len, "WACN [%05X] SYSID [%03X]", wacn, sysid);
        return (n > 0 && n < out_len) ? n : 0;
    }

    /* Check if callsign is meaningful (not all spaces or special chars) */
    int has_alpha = 0;
    for (int i = 0; i < 6; i++) {
        if ((callsign[i] >= 'A' && callsign[i] <= 'Z') || (callsign[i] >= '0' && callsign[i] <= '9')) {
            has_alpha = 1;
            break;
        }
    }

    /* Trim trailing spaces from callsign for cleaner display */
    int len = 6;
    while (len > 0 && callsign[len - 1] == ' ') {
        len--;
    }
    callsign[len] = '\0';

    int n;
    if (has_alpha && len > 0) {
        n = snprintf(out, (size_t)out_len, "WACN [%05X] SYSID [%03X] (%s)", wacn, sysid, callsign);
    } else {
        n = snprintf(out, (size_t)out_len, "WACN [%05X] SYSID [%03X]", wacn, sysid);
    }

    return (n > 0 && n < out_len) ? n : 0;
}
