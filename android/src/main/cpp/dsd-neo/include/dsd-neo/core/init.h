// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Initialization helpers for core option/state structures.
 *
 * Declares default-initialization helpers implemented in core.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize decoder options to defaults. */
void initOpts(dsd_opts* opts);
/** @brief Initialize decoder runtime state to defaults. */
void initState(dsd_state* state);
/** @brief Free dynamic allocations owned by @p state (does not free @p state itself). */
void freeState(dsd_state* state);

#ifdef __cplusplus
}
#endif
