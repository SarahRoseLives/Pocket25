// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/state.h>

#include <stdlib.h>

static int
expect_cc_zero(const dsd_state* state) {
    return state && state->last_cc_sync_time == 0 && state->last_cc_sync_time_m == 0.0;
}

static int
expect_vc_zero(const dsd_state* state) {
    return state && state->last_vc_sync_time == 0 && state->last_vc_sync_time_m == 0.0;
}

int
main(void) {
    // dsd_state is a multi-megabyte struct; avoid Windows' default ~1MB stack.
    dsd_state* state = calloc(1, sizeof(*state));
    if (!state) {
        return 100;
    }

    if (!expect_cc_zero(state) || !expect_vc_zero(state)) {
        free(state);
        return 1;
    }

    dsd_mark_cc_sync(state);
    if (state->last_cc_sync_time == 0) {
        free(state);
        return 2;
    }
    if (state->last_cc_sync_time_m == 0.0) {
        dsd_sleep_ms(1);
        dsd_mark_cc_sync(state);
        if (state->last_cc_sync_time_m == 0.0) {
            free(state);
            return 3;
        }
    }

    dsd_clear_cc_sync(state);
    if (!expect_cc_zero(state)) {
        free(state);
        return 4;
    }

    dsd_mark_vc_sync(state);
    if (state->last_vc_sync_time == 0) {
        free(state);
        return 5;
    }
    if (state->last_vc_sync_time_m == 0.0) {
        dsd_sleep_ms(1);
        dsd_mark_vc_sync(state);
        if (state->last_vc_sync_time_m == 0.0) {
            free(state);
            return 6;
        }
    }

    dsd_clear_vc_sync(state);
    if (!expect_vc_zero(state)) {
        free(state);
        return 7;
    }

    dsd_mark_cc_sync(NULL);
    dsd_mark_vc_sync(NULL);
    dsd_clear_cc_sync(NULL);
    dsd_clear_vc_sync(NULL);

    free(state);
    return 0;
}
