// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Symbol/sample acquisition helpers.
 *
 * Declares symbol acquisition entrypoints implemented in `src/dsp/dsd_symbol.c`.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

float getSymbol(dsd_opts* opts, dsd_state* state, int have_sync);

#ifdef __cplusplus
}
#endif
