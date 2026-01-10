// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/platform/atomic_compat.h>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/ui/ui_opts_snapshot.h>
#include <string.h>
#include "telemetry_hooks_impl.h"

static dsd_opts g_pub_opts;     // latest published
static dsd_opts g_consume_opts; // last copied out for UI
static int g_have_opts = 0;
static dsd_mutex_t g_opts_mu;
static atomic_int g_opts_mu_init = 0;

static void
ensure_opts_mu_init(void) {
    int expected = 0;
    if (atomic_compare_exchange_strong(&g_opts_mu_init, &expected, 1)) {
        dsd_mutex_init(&g_opts_mu);
    }
}

void
ui_terminal_telemetry_publish_opts_snapshot(const dsd_opts* opts) {
    if (!opts) {
        return;
    }
    ensure_opts_mu_init();
    dsd_mutex_lock(&g_opts_mu);
    memcpy(&g_pub_opts, opts, sizeof(dsd_opts));
    g_have_opts = 1;
    dsd_mutex_unlock(&g_opts_mu);
}

const dsd_opts*
ui_get_latest_opts_snapshot(void) {
    ensure_opts_mu_init();
    dsd_mutex_lock(&g_opts_mu);
    if (!g_have_opts) {
        dsd_mutex_unlock(&g_opts_mu);
        return NULL;
    }
    memcpy(&g_consume_opts, &g_pub_opts, sizeof(dsd_opts));
    dsd_mutex_unlock(&g_opts_mu);
    return &g_consume_opts;
}
