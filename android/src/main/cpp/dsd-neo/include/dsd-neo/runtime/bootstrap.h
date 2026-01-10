// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime bootstrap orchestration for the CLI frontend.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Return codes for dsd_runtime_bootstrap (not process exit codes). */
#define DSD_BOOTSTRAP_CONTINUE 0
#define DSD_BOOTSTRAP_EXIT     1
#define DSD_BOOTSTRAP_ERROR    2

/**
 * @brief Orchestrate startup policy for the CLI frontend.
 *
 * Handles user config discovery/loading, CLI parsing/compaction, env mapping,
 * one-shot flows, and interactive bootstrap decisions.
 *
 * The function return value indicates whether to continue into the engine. It
 * is not the process exit code; when returning EXIT/ERROR, out_exit_rc (when
 * non-NULL) receives the desired process exit code.
 *
 * @param argc Original argument count.
 * @param argv Original argument vector (may be compacted in-place).
 * @param opts Decoder options (must be initialized by caller).
 * @param state Decoder state (must be initialized by caller).
 * @param out_argc_effective [out] Effective argc after compaction (may be NULL).
 * @param out_exit_rc [out] Desired process exit code when returning EXIT/ERROR (may be NULL).
 * @return DSD_BOOTSTRAP_CONTINUE, DSD_BOOTSTRAP_EXIT, or DSD_BOOTSTRAP_ERROR.
 */
int dsd_runtime_bootstrap(int argc, char** argv, dsd_opts* opts, dsd_state* state, int* out_argc_effective,
                          int* out_exit_rc);

#ifdef __cplusplus
}
#endif
