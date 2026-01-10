// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief P25 protocol decode entrypoints.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

void processHDU(dsd_opts* opts, dsd_state* state);
void processLDU1(dsd_opts* opts, dsd_state* state);
void processLDU2(dsd_opts* opts, dsd_state* state);
void processTDU(dsd_opts* opts, dsd_state* state);
void processTDULC(dsd_opts* opts, dsd_state* state);
void processTSBK(dsd_opts* opts, dsd_state* state);
void processMPDU(dsd_opts* opts, dsd_state* state);
void processP2(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
