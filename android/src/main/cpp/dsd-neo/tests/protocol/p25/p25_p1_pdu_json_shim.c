// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <stdint.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

void p25_decode_pdu_data(dsd_opts* opts, dsd_state* state, uint8_t* input, int len);

void
p25_test_p1_pdu_data_decode(const unsigned char* input, int len) {
    static dsd_opts opts;
    static dsd_state state;
    memset(&opts, 0, sizeof(opts));
    memset(&state, 0, sizeof(state));
    p25_decode_pdu_data(&opts, &state, (uint8_t*)input, len);
}
