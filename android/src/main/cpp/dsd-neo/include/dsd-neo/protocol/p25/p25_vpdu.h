// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief P25 MAC VPDU handler interfaces.
 *
 * Declares VPDU processing entrypoints implemented in the P25 protocol code.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Process a P25 MAC VPDU block of the given type. */
void process_MAC_VPDU(dsd_opts* opts, dsd_state* state, int type, unsigned long long int mac[24]);

#ifdef __cplusplus
}
#endif
