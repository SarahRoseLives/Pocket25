// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/rigctl_query_hooks.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/io/rigctl_client.h>

static long int
dsd_engine_rigctl_get_current_freq_hz(const dsd_opts* opts) {
    if (!opts) {
        return 0;
    }
    if (opts->use_rigctl != 1) {
        return 0;
    }
    if (opts->rigctl_sockfd == DSD_INVALID_SOCKET) {
        return 0;
    }
    return GetCurrentFreq(opts->rigctl_sockfd);
}

void
dsd_engine_rigctl_query_hooks_install(void) {
    dsd_rigctl_query_hooks hooks = {0};
    hooks.get_current_freq_hz = dsd_engine_rigctl_get_current_freq_hz;
    dsd_rigctl_query_hooks_set(hooks);
}
