// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief P25 Link Control Word (LCW) decoder interfaces.
 *
 * Declares the LCW decoder entrypoint implemented in the P25 protocol library
 * as a standalone API.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void p25_lcw(dsd_opts* opts, dsd_state* state, uint8_t lcw_bits[], uint8_t irrecoverable_errors);

#ifdef __cplusplus
}
#endif
