// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief IO control and tuning helpers.
 *
 * These functions provide low-level tuning backends and radio control helpers.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

int io_control_set_freq(dsd_opts* opts, dsd_state* state, long int freq);
void resumeScan(dsd_opts* opts, dsd_state* state);
void openSerial(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
