// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 channel suffix formatting for denom=4.
 */

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
    int id = 5;
    st.p25_chan_tdma[id] = 1;
    st.p25_chan_type[id] = 4;                      // denom=4 per table
    uint16_t ch = (uint16_t)((id << 12) | 0x0007); // raw 7 -> fdma 1, slot 3 (S4)
    char buf[32] = {0};
    p25_format_chan_suffix(&st, ch, -1, buf, sizeof buf);
    rc |= expect_eq_str("denom4 suffix", buf, " (FDMA 0001 S4)");
    // Hint override to slot 0 -> S1
    memset(buf, 0, sizeof buf);
    p25_format_chan_suffix(&st, ch, 0, buf, sizeof buf);
    rc |= expect_eq_str("denom4 hint", buf, " (FDMA 0001 S1)");
    return rc;
}
