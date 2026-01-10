// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Lightweight test-only helper to invoke LCW decoder with minimal state.
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_lcw.h>

#include <stdlib.h>
#include <string.h>

void
p25_test_invoke_lcw(const unsigned char* lcw_bits, int len, int enable_retune, long cc_freq) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(*opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(*state));
    if (!opts || !state) {
        free(opts);
        free(state);
        return;
    }

    opts->p25_trunk = 1;
    opts->p25_lcw_retune = (enable_retune != 0) ? 1 : 0;
    opts->trunk_tune_group_calls = 1;
    opts->trunk_tune_enc_calls = 0;

    state->p25_cc_freq = cc_freq;

    uint8_t buf[72];
    memset(buf, 0, sizeof(buf));
    int n = len;
    if (n > 72) {
        n = 72;
    }
    for (int i = 0; i < n; i++) {
        buf[i] = (uint8_t)(lcw_bits[i] & 1);
    }
    p25_lcw(opts, state, buf, 0);
    free(opts);
    free(state);
}
