// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 2 MAC VPDU parsing helpers.
 */

#include <stddef.h>

#include <dsd-neo/protocol/p25/p25p2_mac_parse.h>
#include <dsd-neo/protocol/p25/p25p2_mac_tables.h>

int
p25p2_mac_parse(int type, const unsigned long long mac[24], struct p25p2_mac_result* out) {
    if (out == NULL || mac == NULL) {
        return -1;
    }

    out->type = type;
    out->len_a = 0;
    out->mfid = (uint8_t)mac[2];
    out->opcode = (uint8_t)mac[1];

    int len_a = 0;
    int len_b = p25p2_mac_len_for(out->mfid, out->opcode);
    int len_c = 0;

    /* Compute per-channel capacity for message-carrying octets (excludes opcode byte itself). */
    const int capacity = (type == 1) ? 19 : 16;

    /* If table/override gives no guidance, try deriving from MCO when header is present. */
    if (len_b == 0 || len_b > capacity) {
        int mco = (int)(mac[1] & 0x3F);
        if ((mac[0] != 0 || type == 1) && mco > 0) {
            int guess = mco - 1;
            if (guess > capacity) {
                guess = capacity;
            }
            if (guess < 0) {
                guess = 0;
            }
            len_b = guess;
        }
    }

    /* Derive second message length when possible using the same table. */
    if (type == 1 && len_b < 19) {
        len_c = p25p2_mac_len_for((uint8_t)mac[3 + len_a], (uint8_t)mac[1 + len_b]);
    }
    if (type == 0 && len_b < 16) {
        len_c = p25p2_mac_len_for((uint8_t)mac[3 + len_a], (uint8_t)mac[1 + len_b]);
    }

    /* If the second message length is unknown, fill with remaining capacity as a last resort. */
    if ((type == 1 && len_b < 19 && len_c == 0) || (type == 0 && len_b < 16 && len_c == 0)) {
        int remain = capacity - len_b;
        if (remain > 0) {
            len_c = remain;
        }
    }

    out->len_a = len_a;
    out->len_b = len_b;
    out->len_c = len_c;

    return 0;
}
