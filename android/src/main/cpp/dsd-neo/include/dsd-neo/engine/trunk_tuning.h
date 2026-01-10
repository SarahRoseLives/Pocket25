// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Engine-owned trunk tuning policy entry points.
 *
 * These functions implement retune policy and bookkeeping and are installed
 * into the runtime trunk tuning hook table during engine startup.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

void dsd_engine_trunk_tune_to_freq(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps);
void dsd_engine_trunk_tune_to_cc(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps);
void dsd_engine_return_to_cc(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
