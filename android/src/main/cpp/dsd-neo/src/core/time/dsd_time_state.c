// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/dsd_time.h>

#include <dsd-neo/core/state.h>

#include <time.h>

void
dsd_mark_cc_sync(dsd_state* state) {
    if (!state) {
        return;
    }
    state->last_cc_sync_time = time(NULL);
    state->last_cc_sync_time_m = dsd_time_now_monotonic_s();
}

void
dsd_mark_vc_sync(dsd_state* state) {
    if (!state) {
        return;
    }
    state->last_vc_sync_time = time(NULL);
    state->last_vc_sync_time_m = dsd_time_now_monotonic_s();
}

void
dsd_clear_cc_sync(dsd_state* state) {
    if (!state) {
        return;
    }
    state->last_cc_sync_time = 0;
    state->last_cc_sync_time_m = 0.0;
}

void
dsd_clear_vc_sync(dsd_state* state) {
    if (!state) {
        return;
    }
    state->last_vc_sync_time = 0;
    state->last_vc_sync_time_m = 0.0;
}
