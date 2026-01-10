// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/telemetry.h>

#include <dsd-neo/platform/atomic_compat.h>
#include <dsd-neo/platform/threading.h>

#include <stddef.h>

static dsd_telemetry_hooks g_telemetry_hooks = {0};
static dsd_mutex_t g_telemetry_hooks_mu;
static atomic_int g_telemetry_hooks_mu_state = 0; // 0=uninit, 1=initing, 2=init

static void
ensure_hooks_mu_init(void) {
    if (atomic_load(&g_telemetry_hooks_mu_state) == 2) {
        return;
    }

    int expected = 0;
    if (atomic_compare_exchange_strong(&g_telemetry_hooks_mu_state, &expected, 1)) {
        (void)dsd_mutex_init(&g_telemetry_hooks_mu);
        atomic_store(&g_telemetry_hooks_mu_state, 2);
        return;
    }

    while (atomic_load(&g_telemetry_hooks_mu_state) != 2) {}
}

static dsd_telemetry_hooks
dsd_telemetry_hooks_snapshot(void) {
    ensure_hooks_mu_init();
    dsd_mutex_lock(&g_telemetry_hooks_mu);
    dsd_telemetry_hooks hooks = g_telemetry_hooks;
    dsd_mutex_unlock(&g_telemetry_hooks_mu);
    return hooks;
}

void
dsd_telemetry_hooks_set(dsd_telemetry_hooks hooks) {
    ensure_hooks_mu_init();
    dsd_mutex_lock(&g_telemetry_hooks_mu);
    g_telemetry_hooks = hooks;
    dsd_mutex_unlock(&g_telemetry_hooks_mu);
}

void
ui_publish_snapshot(const dsd_state* state) {
    dsd_telemetry_hooks hooks = dsd_telemetry_hooks_snapshot();
    if (!hooks.publish_snapshot) {
        return;
    }
    hooks.publish_snapshot(state);
}

void
ui_publish_opts_snapshot(const dsd_opts* opts) {
    dsd_telemetry_hooks hooks = dsd_telemetry_hooks_snapshot();
    if (!hooks.publish_opts_snapshot) {
        return;
    }
    hooks.publish_opts_snapshot(opts);
}

void
ui_request_redraw(void) {
    dsd_telemetry_hooks hooks = dsd_telemetry_hooks_snapshot();
    if (!hooks.request_redraw) {
        return;
    }
    hooks.request_redraw();
}

void
ui_publish_both_and_redraw(const dsd_opts* opts, const dsd_state* state) {
    dsd_telemetry_hooks hooks = dsd_telemetry_hooks_snapshot();
    if (opts) {
        if (hooks.publish_opts_snapshot) {
            hooks.publish_opts_snapshot(opts);
        }
    }
    if (state) {
        if (hooks.publish_snapshot) {
            hooks.publish_snapshot(state);
        }
    }
    if (hooks.request_redraw) {
        hooks.request_redraw();
    }
}
