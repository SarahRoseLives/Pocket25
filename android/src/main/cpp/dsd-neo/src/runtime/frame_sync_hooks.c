// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/frame_sync_hooks.h>

#include <stddef.h>

static dsd_frame_sync_hooks g_frame_sync_hooks = {0};

void
dsd_frame_sync_hooks_set(dsd_frame_sync_hooks hooks) {
    g_frame_sync_hooks = hooks;
}

void
dsd_frame_sync_hook_p25_sm_try_tick(dsd_opts* opts, dsd_state* state) {
    if (!g_frame_sync_hooks.p25_sm_try_tick) {
        return;
    }
    g_frame_sync_hooks.p25_sm_try_tick(opts, state);
}

void
dsd_frame_sync_hook_p25_sm_on_release(dsd_opts* opts, dsd_state* state) {
    if (!g_frame_sync_hooks.p25_sm_on_release) {
        return;
    }
    g_frame_sync_hooks.p25_sm_on_release(opts, state);
}

void
dsd_frame_sync_hook_eot_cc(dsd_opts* opts, dsd_state* state) {
    if (!g_frame_sync_hooks.eot_cc) {
        return;
    }
    g_frame_sync_hooks.eot_cc(opts, state);
}

void
dsd_frame_sync_hook_no_carrier(dsd_opts* opts, dsd_state* state) {
    if (!g_frame_sync_hooks.no_carrier) {
        return;
    }
    g_frame_sync_hooks.no_carrier(opts, state);
}
