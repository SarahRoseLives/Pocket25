// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/p25_optional_hooks.h>

static int g_watchdog_calls = 0;
static dsd_opts* g_watchdog_opts = NULL;
static dsd_state* g_watchdog_state = NULL;
static uint8_t g_watchdog_slot = 0;

static void
fake_watchdog_event_current(dsd_opts* opts, dsd_state* state, uint8_t slot) {
    g_watchdog_calls++;
    g_watchdog_opts = opts;
    g_watchdog_state = state;
    g_watchdog_slot = slot;
}

static int g_write_calls = 0;
static dsd_opts* g_write_opts = NULL;
static dsd_state* g_write_state = NULL;
static uint8_t g_write_slot = 0;
static uint8_t g_write_swrite = 0;
static char* g_write_event_string = NULL;

static void
fake_write_event_to_log_file(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t swrite, char* event_string) {
    g_write_calls++;
    g_write_opts = opts;
    g_write_state = state;
    g_write_slot = slot;
    g_write_swrite = swrite;
    g_write_event_string = event_string;
}

static int g_push_calls = 0;
static Event_History_I* g_push_event_struct = NULL;

static void
fake_push_event_history(Event_History_I* event_struct) {
    g_push_calls++;
    g_push_event_struct = event_struct;
}

static int g_init_calls = 0;
static Event_History_I* g_init_event_struct = NULL;
static uint8_t g_init_start = 0;
static uint8_t g_init_stop = 0;

static void
fake_init_event_history(Event_History_I* event_struct, uint8_t start, uint8_t stop) {
    g_init_calls++;
    g_init_event_struct = event_struct;
    g_init_start = start;
    g_init_stop = stop;
}

static int g_flush_calls = 0;
static dsd_opts* g_flush_opts = NULL;
static dsd_state* g_flush_state = NULL;

static void
fake_p25p2_flush_partial_audio(dsd_opts* opts, dsd_state* state) {
    g_flush_calls++;
    g_flush_opts = opts;
    g_flush_state = state;
}

int
main(void) {
    static dsd_opts opts;
    static dsd_state state;
    static Event_History_I eh;
    static char event_string[] = "hello";

    // Default behavior with hooks unset: wrappers must be safe no-ops.
    dsd_p25_optional_hooks_set((dsd_p25_optional_hooks){0});

    g_watchdog_calls = 0;
    g_write_calls = 0;
    g_push_calls = 0;
    g_init_calls = 0;
    g_flush_calls = 0;

    dsd_p25_optional_hook_watchdog_event_current(&opts, &state, 1);
    dsd_p25_optional_hook_write_event_to_log_file(&opts, &state, 1, 2, event_string);
    dsd_p25_optional_hook_push_event_history(&eh);
    dsd_p25_optional_hook_init_event_history(&eh, 3, 4);
    dsd_p25_optional_hook_p25p2_flush_partial_audio(&opts, &state);

    assert(g_watchdog_calls == 0);
    assert(g_write_calls == 0);
    assert(g_push_calls == 0);
    assert(g_init_calls == 0);
    assert(g_flush_calls == 0);

    // Installed hooks should be invoked through wrappers.
    dsd_p25_optional_hooks hooks = {0};
    hooks.watchdog_event_current = fake_watchdog_event_current;
    hooks.write_event_to_log_file = fake_write_event_to_log_file;
    hooks.push_event_history = fake_push_event_history;
    hooks.init_event_history = fake_init_event_history;
    hooks.p25p2_flush_partial_audio = fake_p25p2_flush_partial_audio;
    dsd_p25_optional_hooks_set(hooks);

    g_watchdog_calls = 0;
    g_write_calls = 0;
    g_push_calls = 0;
    g_init_calls = 0;
    g_flush_calls = 0;

    g_watchdog_opts = NULL;
    g_watchdog_state = NULL;
    g_watchdog_slot = 0;

    g_write_opts = NULL;
    g_write_state = NULL;
    g_write_slot = 0;
    g_write_swrite = 0;
    g_write_event_string = NULL;

    g_push_event_struct = NULL;
    g_init_event_struct = NULL;
    g_init_start = 0;
    g_init_stop = 0;

    g_flush_opts = NULL;
    g_flush_state = NULL;

    dsd_p25_optional_hook_watchdog_event_current(&opts, &state, 7);
    assert(g_watchdog_calls == 1);
    assert(g_watchdog_opts == &opts);
    assert(g_watchdog_state == &state);
    assert(g_watchdog_slot == 7);

    dsd_p25_optional_hook_write_event_to_log_file(&opts, &state, 8, 9, event_string);
    assert(g_write_calls == 1);
    assert(g_write_opts == &opts);
    assert(g_write_state == &state);
    assert(g_write_slot == 8);
    assert(g_write_swrite == 9);
    assert(g_write_event_string == event_string);

    dsd_p25_optional_hook_push_event_history(&eh);
    assert(g_push_calls == 1);
    assert(g_push_event_struct == &eh);

    dsd_p25_optional_hook_init_event_history(&eh, 10, 11);
    assert(g_init_calls == 1);
    assert(g_init_event_struct == &eh);
    assert(g_init_start == 10);
    assert(g_init_stop == 11);

    dsd_p25_optional_hook_p25p2_flush_partial_audio(&opts, &state);
    assert(g_flush_calls == 1);
    assert(g_flush_opts == &opts);
    assert(g_flush_state == &state);

    return 0;
}
