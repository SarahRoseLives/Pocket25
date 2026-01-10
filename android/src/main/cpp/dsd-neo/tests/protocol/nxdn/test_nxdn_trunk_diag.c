// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * NXDN trunking diagnostics: missing channel->frequency mapping tracking.
 */

#include <dsd-neo/core/state.h>
#include <dsd-neo/core/state_ext.h>
#include <dsd-neo/protocol/nxdn/nxdn_trunk_diag.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int
expect_eq_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

static int
expect_eq_size(const char* tag, size_t got, size_t want) {
    if (got != want) {
        fprintf(stderr, "%s: got %zu want %zu\n", tag, got, want);
        return 1;
    }
    return 0;
}

static int
expect_eq_u16(const char* tag, uint16_t got, uint16_t want) {
    if (got != want) {
        fprintf(stderr, "%s: got %u want %u\n", tag, (unsigned)got, (unsigned)want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;

    static dsd_state state;
    uint16_t out[8];

    memset(out, 0, sizeof out);
    rc |= expect_eq_size("empty-total", nxdn_trunk_diag_collect_unmapped_channels(&state, out, 8), 0);

    rc |= expect_eq_int("note-ch12-first", nxdn_trunk_diag_note_missing_channel(&state, 12), 1);
    rc |= expect_eq_int("note-ch12-again", nxdn_trunk_diag_note_missing_channel(&state, 12), 0);
    rc |= expect_eq_int("note-ch13-first", nxdn_trunk_diag_note_missing_channel(&state, 13), 1);

    memset(out, 0, sizeof out);
    rc |= expect_eq_size("total-2", nxdn_trunk_diag_collect_unmapped_channels(&state, out, 8), 2);
    rc |= expect_eq_u16("out0-ch12", out[0], 12);
    rc |= expect_eq_u16("out1-ch13", out[1], 13);

    // If a channel becomes mapped later in the run, the summary should no longer report it.
    state.trunk_chan_map[12] = 851000000;
    memset(out, 0, sizeof out);
    rc |= expect_eq_size("total-1-after-map", nxdn_trunk_diag_collect_unmapped_channels(&state, out, 8), 1);
    rc |= expect_eq_u16("out0-ch13-after-map", out[0], 13);

    dsd_state_ext_free_all(&state);
    return rc;
}
