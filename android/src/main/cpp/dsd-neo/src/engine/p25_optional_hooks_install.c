// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/p25_optional_hooks.h>

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/events.h>

void
dsd_engine_p25_optional_hooks_install(void) {
    dsd_p25_optional_hooks hooks = {0};
    hooks.watchdog_event_current = watchdog_event_current;
    hooks.write_event_to_log_file = write_event_to_log_file;
    hooks.push_event_history = push_event_history;
    hooks.init_event_history = init_event_history;
    hooks.p25p2_flush_partial_audio = dsd_p25p2_flush_partial_audio;
    dsd_p25_optional_hooks_set(hooks);
}
