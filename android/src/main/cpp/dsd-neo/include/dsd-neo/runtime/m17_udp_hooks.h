// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime hook table for optional M17 UDP helpers.
 *
 * Protocol code should not depend on IO backend headers directly. The engine
 * installs real hook functions at startup; the runtime provides safe wrappers
 * with defaults when hooks are not installed.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>
#include <dsd-neo/platform/sockets.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    dsd_socket_t (*udp_bind)(char* hostname, int portno);
    int (*connect)(dsd_opts* opts, dsd_state* state);
    int (*receiver)(dsd_opts* opts, void* data);
    int (*blaster)(dsd_opts* opts, dsd_state* state, size_t nsam, void* data);
} dsd_m17_udp_hooks;

void dsd_m17_udp_hooks_set(dsd_m17_udp_hooks hooks);

dsd_socket_t dsd_m17_udp_hook_udp_bind(char* hostname, int portno);
int dsd_m17_udp_hook_connect(dsd_opts* opts, dsd_state* state);
int dsd_m17_udp_hook_receiver(dsd_opts* opts, void* data);
int dsd_m17_udp_hook_blaster(dsd_opts* opts, dsd_state* state, size_t nsam, void* data);

#ifdef __cplusplus
}
#endif
