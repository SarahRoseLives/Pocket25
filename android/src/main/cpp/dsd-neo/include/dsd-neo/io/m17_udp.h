// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int udp_socket_connectM17(dsd_opts* opts, dsd_state* state);
int m17_socket_receiver(dsd_opts* opts, void* data);
int m17_socket_blaster(dsd_opts* opts, dsd_state* state, size_t nsam, void* data);

#ifdef __cplusplus
}
#endif
