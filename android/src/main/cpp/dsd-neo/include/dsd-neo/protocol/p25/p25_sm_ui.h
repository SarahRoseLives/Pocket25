// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief P25 UI-facing state-machine helpers and event surface.
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Small helpers for tagging/logging P25 state-machine status in the shared
 * dsd_state structure and stderr (when verbose).
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Record a short status tag and optionally log a concise line.
 *
 * When verbosity > 1, logs the tag to stderr. Tags are also pushed into a
 * small ring buffer for UI display.
 *
 * @param opts Decoder options (controls verbosity).
 * @param state Decoder state carrying UI history.
 * @param tag Short tag string to record.
 */
void p25_sm_log_status(dsd_opts* opts, dsd_state* state, const char* tag);

#ifdef __cplusplus
}
#endif
