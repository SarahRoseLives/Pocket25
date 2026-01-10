// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/p25_optional_hooks.h>

static dsd_p25_optional_hooks g_p25_optional_hooks = {0};

void
dsd_p25_optional_hooks_set(dsd_p25_optional_hooks hooks) {
    g_p25_optional_hooks = hooks;
}

void
dsd_p25_optional_hook_watchdog_event_current(dsd_opts* opts, dsd_state* state, uint8_t slot) {
    if (!g_p25_optional_hooks.watchdog_event_current) {
        return;
    }
    g_p25_optional_hooks.watchdog_event_current(opts, state, slot);
}

void
dsd_p25_optional_hook_write_event_to_log_file(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t swrite,
                                              char* event_string) {
    if (!g_p25_optional_hooks.write_event_to_log_file) {
        return;
    }
    g_p25_optional_hooks.write_event_to_log_file(opts, state, slot, swrite, event_string);
}

void
dsd_p25_optional_hook_push_event_history(Event_History_I* event_struct) {
    if (!g_p25_optional_hooks.push_event_history) {
        return;
    }
    g_p25_optional_hooks.push_event_history(event_struct);
}

void
dsd_p25_optional_hook_init_event_history(Event_History_I* event_struct, uint8_t start, uint8_t stop) {
    if (!g_p25_optional_hooks.init_event_history) {
        return;
    }
    g_p25_optional_hooks.init_event_history(event_struct, start, stop);
}

void
dsd_p25_optional_hook_p25p2_flush_partial_audio(dsd_opts* opts, dsd_state* state) {
    if (!g_p25_optional_hooks.p25p2_flush_partial_audio) {
        return;
    }
    g_p25_optional_hooks.p25p2_flush_partial_audio(opts, state);
}
