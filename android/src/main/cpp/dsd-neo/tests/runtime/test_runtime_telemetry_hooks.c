// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dsd-neo/runtime/telemetry.h>

static int g_publish_snapshot_calls = 0;
static int g_publish_opts_snapshot_calls = 0;
static int g_request_redraw_calls = 0;
static const dsd_state* g_last_state = NULL;
static const dsd_opts* g_last_opts = NULL;

static void
fake_publish_snapshot(const dsd_state* state) {
    g_publish_snapshot_calls++;
    g_last_state = state;
}

static void
fake_publish_opts_snapshot(const dsd_opts* opts) {
    g_publish_opts_snapshot_calls++;
    g_last_opts = opts;
}

static void
fake_request_redraw(void) {
    g_request_redraw_calls++;
}

int
main(void) {
    dsd_telemetry_hooks_set((dsd_telemetry_hooks){0});
    ui_publish_snapshot(NULL);
    ui_publish_opts_snapshot(NULL);
    ui_request_redraw();
    ui_publish_both_and_redraw(NULL, NULL);

    dsd_opts* opts = (dsd_opts*)calloc(1, 1);
    dsd_state* state = (dsd_state*)calloc(1, 1);
    assert(opts != NULL);
    assert(state != NULL);

    g_publish_snapshot_calls = 0;
    g_publish_opts_snapshot_calls = 0;
    g_request_redraw_calls = 0;
    g_last_state = NULL;
    g_last_opts = NULL;

    dsd_telemetry_hooks_set((dsd_telemetry_hooks){
        .publish_snapshot = fake_publish_snapshot,
        .publish_opts_snapshot = fake_publish_opts_snapshot,
        .request_redraw = fake_request_redraw,
    });

    ui_publish_snapshot(state);
    assert(g_publish_snapshot_calls == 1);
    assert(g_publish_opts_snapshot_calls == 0);
    assert(g_request_redraw_calls == 0);
    assert(g_last_state == state);

    ui_publish_opts_snapshot(opts);
    assert(g_publish_snapshot_calls == 1);
    assert(g_publish_opts_snapshot_calls == 1);
    assert(g_request_redraw_calls == 0);
    assert(g_last_opts == opts);

    ui_request_redraw();
    assert(g_publish_snapshot_calls == 1);
    assert(g_publish_opts_snapshot_calls == 1);
    assert(g_request_redraw_calls == 1);

    ui_publish_both_and_redraw(opts, state);
    assert(g_publish_snapshot_calls == 2);
    assert(g_publish_opts_snapshot_calls == 2);
    assert(g_request_redraw_calls == 2);

    free(state);
    free(opts);
    return 0;
}
