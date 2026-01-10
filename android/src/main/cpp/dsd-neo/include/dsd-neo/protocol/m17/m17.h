// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief M17 protocol decode entrypoints.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

void processM17STR(dsd_opts* opts, dsd_state* state);
void processM17PKT(dsd_opts* opts, dsd_state* state);
void processM17LSF(dsd_opts* opts, dsd_state* state);
void processM17IPF(dsd_opts* opts, dsd_state* state);
void encodeM17STR(dsd_opts* opts, dsd_state* state);
void encodeM17BRT(dsd_opts* opts, dsd_state* state);
void encodeM17PKT(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
