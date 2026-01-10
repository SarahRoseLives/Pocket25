// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/m17_udp_hooks.h>

static dsd_m17_udp_hooks g_m17_udp_hooks = {0};

void
dsd_m17_udp_hooks_set(dsd_m17_udp_hooks hooks) {
    g_m17_udp_hooks = hooks;
}

dsd_socket_t
dsd_m17_udp_hook_udp_bind(char* hostname, int portno) {
    if (!g_m17_udp_hooks.udp_bind) {
        return DSD_INVALID_SOCKET;
    }
    return g_m17_udp_hooks.udp_bind(hostname, portno);
}

int
dsd_m17_udp_hook_connect(dsd_opts* opts, dsd_state* state) {
    if (!g_m17_udp_hooks.connect) {
        return -1;
    }
    return g_m17_udp_hooks.connect(opts, state);
}

int
dsd_m17_udp_hook_receiver(dsd_opts* opts, void* data) {
    if (!g_m17_udp_hooks.receiver) {
        return -1;
    }
    return g_m17_udp_hooks.receiver(opts, data);
}

int
dsd_m17_udp_hook_blaster(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    if (!g_m17_udp_hooks.blaster) {
        return -1;
    }
    return g_m17_udp_hooks.blaster(opts, state, nsam, data);
}
