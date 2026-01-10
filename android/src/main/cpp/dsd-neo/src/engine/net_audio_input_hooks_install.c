// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/net_audio_input_hooks.h>

#include <dsd-neo/io/tcp_input.h>
#include <dsd-neo/io/udp_input.h>

void
dsd_engine_net_audio_input_hooks_install(void) {
    dsd_net_audio_input_hooks hooks = {0};

    hooks.tcp_open = tcp_input_open;
    hooks.tcp_close = tcp_input_close;
    hooks.tcp_read_sample = tcp_input_read_sample;
    hooks.tcp_is_valid = tcp_input_is_valid;
    hooks.tcp_get_socket = tcp_input_get_socket;

    hooks.udp_start = udp_input_start;
    hooks.udp_stop = udp_input_stop;
    hooks.udp_read_sample = udp_input_read_sample;

    dsd_net_audio_input_hooks_set(hooks);
}
