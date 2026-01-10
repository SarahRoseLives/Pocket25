// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 channel suffix formatting tests.
 * Ensures TDMA/FDMA suffix formatting and CC-TDMA fallback denom behavior.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>

static int
expect_eq_str(const char* tag, const char* got, const char* want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "%s: got '%s' want '%s'\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    static dsd_state st;
    memset(&st, 0, sizeof st);

    char buf[32];

    // Case 1: Known TDMA (type=3 -> denom=2). Raw ch=0x2005 -> FDMA 0x0002, slot 2
    int id = 2;
    st.p25_chan_tdma[id] = 1;
    st.p25_chan_type[id] = 3; // slots_per_carrier[3] = 2
    uint16_t ch = (uint16_t)((id << 12) | 0x0005);
    memset(buf, 0, sizeof buf);
    p25_format_chan_suffix(&st, ch, -1, buf, sizeof buf);
    rc |= expect_eq_str("tdma suffix", buf, " (FDMA 0002 S2)");
    // Override slot via hint
    memset(buf, 0, sizeof buf);
    p25_format_chan_suffix(&st, ch, 0, buf, sizeof buf);
    rc |= expect_eq_str("tdma slot hint", buf, " (FDMA 0002 S1)");

    // Case 2: FDMA (denom=1) → empty suffix
    static dsd_state st2;
    memset(&st2, 0, sizeof st2);
    st2.p25_chan_tdma[id] = 0;
    st2.p25_cc_is_tdma = 0;
    ch = (uint16_t)((id << 12) | 0x000A);
    memset(buf, 0, sizeof buf);
    p25_format_chan_suffix(&st2, ch, -1, buf, sizeof buf);
    rc |= expect_eq_str("fdma suffix empty", buf, "");

    // Case 3: System has Phase 2 TDMA voice but IDEN TDMA unknown → fallback denom=2
    static dsd_state st3;
    memset(&st3, 0, sizeof st3);
    st3.p25_chan_tdma[id] = 0; // unknown
    st3.p25_sys_is_tdma = 1;
    ch = (uint16_t)((id << 12) | 0x0007); // raw 7 -> FDMA 3, slot 2 (1-based)
    memset(buf, 0, sizeof buf);
    p25_format_chan_suffix(&st3, ch, -1, buf, sizeof buf);
    rc |= expect_eq_str("fallback denom=2", buf, " (FDMA 0003 S2)");

    return rc;
}
