// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Focused checks for P25 Phase 2 MAC opcode length table and vendor overrides. */

#include <stdint.h>
#include <stdio.h>

#include <dsd-neo/protocol/p25/p25p2_mac_tables.h>

static int
expect_eq(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;

    // A few core opcodes (standard MFID 0/1)
    rc |= expect_eq("OP 0x40 (GRP_V_CH_GRANT)", p25p2_mac_len_for(0x01, 0x40), 9);
    rc |= expect_eq("OP 0x48 (UU_V_CH_GRANT)", p25p2_mac_len_for(0x01, 0x48), 10);
    rc |= expect_eq("OP 0x71 (AUTH_DEMAND)", p25p2_mac_len_for(0x01, 0x71), 29);

    // Extended variant set (filled to reduce unknowns)
    rc |= expect_eq("OP 0xF1 (AUTH_DEMAND_EXT)", p25p2_mac_len_for(0x01, 0xF1), 29);

    // Vendor overrides
    rc |= expect_eq("Moto 0x91", p25p2_mac_len_for(0x90, 0x91), 17);
    rc |= expect_eq("Moto 0x95", p25p2_mac_len_for(0x90, 0x95), 17);
    rc |= expect_eq("Harris generic", p25p2_mac_len_for(0xB0, 0x12), 17);
    rc |= expect_eq("Tait generic", p25p2_mac_len_for(0xB5, 0x34), 5);
    rc |= expect_eq("Harris extra 0x81", p25p2_mac_len_for(0x81, 0x20), 7);
    rc |= expect_eq("Harris extra 0x8F", p25p2_mac_len_for(0x8F, 0x20), 7);

    return rc;
}
