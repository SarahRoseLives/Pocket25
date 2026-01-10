// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Test-side shim to decode P25p1 PDU data with minimal Event History backing. */

#include <stdint.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

void p25_decode_pdu_data(dsd_opts* opts, dsd_state* state, uint8_t* input, int len);

void
p25_test_p1_pdu_data_decode_with_evh(const unsigned char* input, int len) {
    dsd_opts opts;
    dsd_state state;
    memset(&opts, 0, sizeof(opts));
    memset(&state, 0, sizeof(state));

    static Event_History_I evh_store[2];
    memset(evh_store, 0, sizeof(evh_store));
    state.event_history_s = evh_store;

    p25_decode_pdu_data(&opts, &state, (uint8_t*)input, len);
}
