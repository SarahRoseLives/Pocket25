// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Cleanup/exit helpers.
 *
 * Declares the legacy cleanup entrypoint so lower-level modules can request an
 * orderly shutdown.
 *
 * Note: despite the name, `cleanupAndExit()` is a non-exiting shutdown request
 * helper. Callers must return/break out after invoking it.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

void cleanupAndExit(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
