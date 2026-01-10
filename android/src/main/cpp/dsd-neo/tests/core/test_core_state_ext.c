// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/state.h>

#include <stdlib.h>

static int s_cleanup_calls = 0;

static void
test_cleanup_free(void* ptr) {
    s_cleanup_calls++;
    free(ptr);
}

int
main(void) {
    // dsd_state is a multi-megabyte struct; avoid Windows' default ~1MB stack.
    dsd_state* state = calloc(1, sizeof(*state));
    if (!state) {
        return 100;
    }

    void* p1 = malloc(1);
    if (!p1) {
        free(state);
        return 1;
    }

    if (dsd_state_ext_set(state, DSD_STATE_EXT_ENGINE_START_MS, p1, test_cleanup_free) != 0) {
        free(p1);
        free(state);
        return 2;
    }
    if (dsd_state_ext_get(state, DSD_STATE_EXT_ENGINE_START_MS) != p1) {
        dsd_state_ext_free_all(state);
        free(state);
        return 3;
    }
    if (s_cleanup_calls != 0) {
        dsd_state_ext_free_all(state);
        free(state);
        return 4;
    }

    void* p2 = malloc(1);
    if (!p2) {
        dsd_state_ext_free_all(state);
        free(state);
        return 5;
    }

    if (dsd_state_ext_set(state, DSD_STATE_EXT_ENGINE_START_MS, p2, test_cleanup_free) != 0) {
        free(p2);
        dsd_state_ext_free_all(state);
        free(state);
        return 6;
    }
    if (dsd_state_ext_get(state, DSD_STATE_EXT_ENGINE_START_MS) != p2) {
        dsd_state_ext_free_all(state);
        free(state);
        return 7;
    }
    if (s_cleanup_calls != 1) {
        dsd_state_ext_free_all(state);
        free(state);
        return 8;
    }

    dsd_state_ext_free_all(state);
    if (s_cleanup_calls != 2) {
        free(state);
        return 9;
    }
    if (dsd_state_ext_get(state, DSD_STATE_EXT_ENGINE_START_MS) != NULL) {
        free(state);
        return 10;
    }

    void* p3 = malloc(1);
    if (!p3) {
        free(state);
        return 11;
    }
    if (dsd_state_ext_set(state, (dsd_state_ext_id)DSD_STATE_EXT_MAX, p3, test_cleanup_free) != -1) {
        free(p3);
        free(state);
        return 12;
    }
    free(p3);

    dsd_state_ext_free_all(NULL);

    free(state);
    return 0;
}
