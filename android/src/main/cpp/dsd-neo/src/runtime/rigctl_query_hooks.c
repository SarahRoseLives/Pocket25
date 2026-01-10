// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/rigctl_query_hooks.h>

static dsd_rigctl_query_hooks g_rigctl_query_hooks = {0};

void
dsd_rigctl_query_hooks_set(dsd_rigctl_query_hooks hooks) {
    g_rigctl_query_hooks = hooks;
}

long int
dsd_rigctl_query_hook_get_current_freq_hz(const dsd_opts* opts) {
    if (g_rigctl_query_hooks.get_current_freq_hz) {
        return g_rigctl_query_hooks.get_current_freq_hz(opts);
    }
    return 0;
}
