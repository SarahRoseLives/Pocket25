// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/runtime/rigctl_query_hooks.h>

static int g_calls = 0;
static const dsd_opts* g_last_opts = NULL;
static long int g_return_value = 0;

static long int
fake_get_current_freq_hz(const dsd_opts* opts) {
    g_calls++;
    g_last_opts = opts;
    return g_return_value;
}

int
main(void) {
    static dsd_opts opts;

    // Default behavior with hooks unset: wrappers must be safe fallbacks.
    dsd_rigctl_query_hooks_set((dsd_rigctl_query_hooks){0});
    assert(dsd_rigctl_query_hook_get_current_freq_hz(NULL) == 0);
    assert(dsd_rigctl_query_hook_get_current_freq_hz(&opts) == 0);

    // Installed hooks should be invoked through wrappers.
    dsd_rigctl_query_hooks hooks = {0};
    hooks.get_current_freq_hz = fake_get_current_freq_hz;
    dsd_rigctl_query_hooks_set(hooks);

    g_calls = 0;
    g_last_opts = NULL;
    g_return_value = 123456789L;

    assert(dsd_rigctl_query_hook_get_current_freq_hz(&opts) == 123456789L);
    assert(g_calls == 1);
    assert(g_last_opts == &opts);

    return 0;
}
