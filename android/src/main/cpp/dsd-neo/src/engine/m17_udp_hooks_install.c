// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/m17_udp_hooks.h>

#include <dsd-neo/io/m17_udp.h>
#include <dsd-neo/io/udp_bind.h>

void
dsd_engine_m17_udp_hooks_install(void) {
    dsd_m17_udp_hooks hooks = {0};
    hooks.udp_bind = UDPBind;
    hooks.connect = udp_socket_connectM17;
    hooks.receiver = m17_socket_receiver;
    hooks.blaster = m17_socket_blaster;
    dsd_m17_udp_hooks_set(hooks);
}
