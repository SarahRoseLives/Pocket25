// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/net_audio_input_hooks.h>

static dsd_net_audio_input_hooks g_net_audio_input_hooks = {0};

void
dsd_net_audio_input_hooks_set(dsd_net_audio_input_hooks hooks) {
    g_net_audio_input_hooks = hooks;
}

tcp_input_ctx*
dsd_net_audio_input_hook_tcp_open(dsd_socket_t sockfd, int samplerate) {
    if (!g_net_audio_input_hooks.tcp_open) {
        return NULL;
    }
    return g_net_audio_input_hooks.tcp_open(sockfd, samplerate);
}

void
dsd_net_audio_input_hook_tcp_close(tcp_input_ctx* ctx) {
    if (g_net_audio_input_hooks.tcp_close) {
        g_net_audio_input_hooks.tcp_close(ctx);
    }
}

int
dsd_net_audio_input_hook_tcp_read_sample(tcp_input_ctx* ctx, int16_t* out) {
    if (!g_net_audio_input_hooks.tcp_read_sample) {
        return 0;
    }
    return g_net_audio_input_hooks.tcp_read_sample(ctx, out);
}

int
dsd_net_audio_input_hook_tcp_is_valid(tcp_input_ctx* ctx) {
    if (!g_net_audio_input_hooks.tcp_is_valid) {
        return 0;
    }
    return g_net_audio_input_hooks.tcp_is_valid(ctx);
}

dsd_socket_t
dsd_net_audio_input_hook_tcp_get_socket(tcp_input_ctx* ctx) {
    if (!g_net_audio_input_hooks.tcp_get_socket) {
        return DSD_INVALID_SOCKET;
    }
    return g_net_audio_input_hooks.tcp_get_socket(ctx);
}

int
dsd_net_audio_input_hook_udp_start(dsd_opts* opts, const char* bindaddr, int port, int samplerate) {
    if (!g_net_audio_input_hooks.udp_start) {
        return -1;
    }
    return g_net_audio_input_hooks.udp_start(opts, bindaddr, port, samplerate);
}

void
dsd_net_audio_input_hook_udp_stop(dsd_opts* opts) {
    if (g_net_audio_input_hooks.udp_stop) {
        g_net_audio_input_hooks.udp_stop(opts);
    }
}

int
dsd_net_audio_input_hook_udp_read_sample(dsd_opts* opts, int16_t* out) {
    if (!g_net_audio_input_hooks.udp_read_sample) {
        return 0;
    }
    return g_net_audio_input_hooks.udp_read_sample(opts, out);
}
