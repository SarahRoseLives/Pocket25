// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/state_ext.h>

#include <dsd-neo/core/state.h>

static int
dsd_state_ext_id_is_valid(dsd_state_ext_id id) {
    return (int)id >= 0 && (int)id < DSD_STATE_EXT_MAX;
}

void*
dsd_state_ext_get(dsd_state* state, dsd_state_ext_id id) {
    if (!state || !dsd_state_ext_id_is_valid(id)) {
        return NULL;
    }
    return state->state_ext[(int)id];
}

int
dsd_state_ext_set(dsd_state* state, dsd_state_ext_id id, void* ptr, dsd_state_ext_cleanup_fn cleanup) {
    if (!state || !dsd_state_ext_id_is_valid(id)) {
        return -1;
    }

    int idx = (int)id;
    void* old_ptr = state->state_ext[idx];
    dsd_state_ext_cleanup_fn old_cleanup = state->state_ext_cleanup[idx];

    if (old_ptr && old_ptr != ptr && old_cleanup) {
        old_cleanup(old_ptr);
    }

    state->state_ext[idx] = ptr;
    state->state_ext_cleanup[idx] = ptr ? cleanup : NULL;
    return 0;
}

void
dsd_state_ext_free_all(dsd_state* state) {
    if (!state) {
        return;
    }

    for (int i = 0; i < DSD_STATE_EXT_MAX; i++) {
        void* ptr = state->state_ext[i];
        dsd_state_ext_cleanup_fn cleanup = state->state_ext_cleanup[i];
        if (ptr && cleanup) {
            cleanup(ptr);
        }
        state->state_ext[i] = NULL;
        state->state_ext_cleanup[i] = NULL;
    }
}
