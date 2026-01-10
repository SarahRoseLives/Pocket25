// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/udp_audio_hooks.h>

#include <dsd-neo/io/udp_audio.h>

static void
dsd_engine_udp_audio_blast(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    udp_socket_blaster(opts, state, nsam, data);
}

static void
dsd_engine_udp_audio_blast_analog(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    udp_socket_blasterA(opts, state, nsam, data);
}

void
dsd_engine_udp_audio_hooks_install(void) {
    dsd_udp_audio_hooks hooks = {0};
    hooks.blast = dsd_engine_udp_audio_blast;
    hooks.blast_analog = dsd_engine_udp_audio_blast_analog;
    dsd_udp_audio_hooks_set(hooks);
}
