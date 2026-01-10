// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/engine/frame_processing.h>
#include <dsd-neo/runtime/frame_sync_hooks.h>

#include <dsd-neo/protocol/edacs/edacs.h>
#include <dsd-neo/protocol/p25/p25_sm_watchdog.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>

void
dsd_engine_frame_sync_hooks_install(void) {
    dsd_frame_sync_hooks hooks = {0};
    hooks.p25_sm_try_tick = p25_sm_try_tick;
    hooks.p25_sm_on_release = p25_sm_on_release;
    hooks.eot_cc = eot_cc;
    hooks.no_carrier = noCarrier;
    dsd_frame_sync_hooks_set(hooks);
}
