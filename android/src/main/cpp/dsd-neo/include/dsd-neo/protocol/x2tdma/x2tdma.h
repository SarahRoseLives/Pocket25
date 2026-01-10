// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief X2-TDMA protocol decode entrypoints.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

void processX2TDMAdata(dsd_opts* opts, dsd_state* state);
void processX2TDMAvoice(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
