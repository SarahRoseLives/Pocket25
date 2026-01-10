// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/udp_audio_hooks.h>

static dsd_udp_audio_hooks g_udp_audio_hooks = {0};

void
dsd_udp_audio_hooks_set(dsd_udp_audio_hooks hooks) {
    g_udp_audio_hooks = hooks;
}

void
dsd_udp_audio_hook_blast(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    if (g_udp_audio_hooks.blast) {
        g_udp_audio_hooks.blast(opts, state, nsam, data);
    }
}

void
dsd_udp_audio_hook_blast_analog(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    if (g_udp_audio_hooks.blast_analog) {
        g_udp_audio_hooks.blast_analog(opts, state, nsam, data);
    }
}
