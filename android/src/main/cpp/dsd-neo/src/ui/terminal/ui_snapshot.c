// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/atomic_compat.h>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/ui/ui_snapshot.h>
#include <string.h>
#include "telemetry_hooks_impl.h"

static dsd_state g_pub;     // latest published by demod thread
static dsd_state g_consume; // last copied out for UI
// Deep-copied backing for pointer-backed members the UI dereferences.
// Currently, only event history is pointer-backed in dsd_state for UI reads.
static Event_History_I g_pub_eh[2];
static Event_History_I g_consume_eh[2];
static int g_have = 0;
static dsd_mutex_t g_mu;
static atomic_int g_mu_init = 0;

static void
ensure_mu_init(void) {
    int expected = 0;
    if (atomic_compare_exchange_strong(&g_mu_init, &expected, 1)) {
        dsd_mutex_init(&g_mu);
    }
}

void
ui_terminal_telemetry_publish_snapshot(const dsd_state* state) {
    if (!state) {
        return;
    }
    ensure_mu_init();
    dsd_mutex_lock(&g_mu);
    // Coarse copy of the entire struct first
    memcpy(&g_pub, state, sizeof(dsd_state));
    // Deep copy pointer-backed UI data (event history for 2 slots)
    if (state->event_history_s != NULL) {
        memcpy(&g_pub_eh[0], &state->event_history_s[0], sizeof(Event_History_I));
        memcpy(&g_pub_eh[1], &state->event_history_s[1], sizeof(Event_History_I));
        g_pub.event_history_s = g_pub_eh;
    } else {
        g_pub.event_history_s = NULL;
    }
    g_have = 1;
    dsd_mutex_unlock(&g_mu);
}

const dsd_state*
ui_get_latest_snapshot(void) {
    ensure_mu_init();
    dsd_mutex_lock(&g_mu);
    if (!g_have) {
        dsd_mutex_unlock(&g_mu);
        return NULL;
    }
    // Copy the coarse snapshot
    memcpy(&g_consume, &g_pub, sizeof(dsd_state));
    // Deep copy the event history so the UI holds a stable buffer
    if (g_pub.event_history_s != NULL) {
        memcpy(&g_consume_eh[0], &g_pub_eh[0], sizeof(Event_History_I));
        memcpy(&g_consume_eh[1], &g_pub_eh[1], sizeof(Event_History_I));
        g_consume.event_history_s = g_consume_eh;
    } else {
        g_consume.event_history_s = NULL;
    }
    dsd_mutex_unlock(&g_mu);
    return &g_consume;
}
