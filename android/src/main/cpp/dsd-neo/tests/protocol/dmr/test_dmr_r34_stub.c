// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/dmr/r34_viterbi.h>

int
main(void) {
    uint8_t dibits[98];
    memset(dibits, 0, sizeof(dibits));
    uint8_t out[18];
    memset(out, 0xAA, sizeof(out));
    int rc = dmr_r34_viterbi_decode(dibits, out);
    assert(rc == 0);
    // no strong assertion on content yet; this is a smoke test for the stub
    printf("DMR R3/4 stub: OK\n");
    return 0;
}
