// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief UDP audio output helpers.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void udp_socket_blaster(dsd_opts* opts, dsd_state* state, size_t nsam, void* data);
void udp_socket_blasterA(dsd_opts* opts, dsd_state* state, size_t nsam, void* data);

#ifdef __cplusplus
}
#endif
