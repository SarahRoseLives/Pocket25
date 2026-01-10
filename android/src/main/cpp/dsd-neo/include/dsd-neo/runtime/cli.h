// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief CLI surface for parsing args/env into dsd_opts/dsd_state.
 *
 * For high-level startup orchestration (config discovery/one-shots/interactive
 * bootstrap decisions), see `include/dsd-neo/runtime/bootstrap.h`.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Return codes for dsd_parse_args. */
#define DSD_PARSE_CONTINUE 0
#define DSD_PARSE_ONE_SHOT 1
#define DSD_PARSE_ERROR    2

/**
 * @brief Parse CLI arguments and environment into opts/state.
 *
 * Populates opts/state, compacts argv for downstream processing, and handles
 * one-shot helpers (calculators/listings). Returns DSD_PARSE_ONE_SHOT when a
 * one-shot was executed and DSD_PARSE_ERROR when an error occurred. When
 * returning ONE_SHOT or ERROR, out_exit_rc (when non-NULL) receives the desired
 * process exit code.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param opts Decoder options to populate.
 * @param state Decoder state to populate.
 * @param out_argc [out] Effective argc after compaction (may be NULL).
 * @param out_exit_rc [out] Process exit code when returning ONE_SHOT or ERROR (may be NULL).
 * @return DSD_PARSE_CONTINUE to run decoder; DSD_PARSE_ONE_SHOT when handled; DSD_PARSE_ERROR on failure.
 */
int dsd_parse_args(int argc, char** argv, dsd_opts* opts, dsd_state* state, int* out_argc, int* out_exit_rc);

/**
 * @brief Compact argv in-place by removing recognized long options.
 *
 * Removes runtime long options (including `--config` and related flags) so that
 * short-option parsing can be handled by `getopt()` without tripping on unknown
 * `--*` tokens. Always keeps `argv[0]` as the program name and terminates the
 * resulting argv list with a NULL pointer.
 *
 * @param argc Argument count.
 * @param argv Argument vector (modified in-place).
 * @return New effective argc (including argv[0]).
 */
int dsd_cli_compact_args(int argc, char** argv);

/** @brief Print the CLI usage/help text. */
void dsd_cli_usage(void);

/**
 * @brief Run the DMR TIII LCN calculator one-shot utility.
 *
 * Parses frequencies from a CSV file and emits an LCN mapping to stdout.
 * Uses environment variables for optional configuration:
 *   - DSD_NEO_DMR_T3_STEP_HZ: Override inferred channel step
 *   - DSD_NEO_DMR_T3_CC_FREQ: Anchor CC frequency
 *   - DSD_NEO_DMR_T3_CC_LCN: Anchor CC LCN
 *   - DSD_NEO_DMR_T3_START_LCN: Starting LCN when no anchor
 *
 * @param path Path to CSV file containing frequencies.
 * @return 0 on success, non-zero on error.
 */
int dsd_cli_calc_dmr_t3_lcn_from_csv(const char* path);

/** @brief Enable FTZ/DAZ CPU modes when requested via env. */
void dsd_bootstrap_enable_ftz_daz_if_enabled(void);
/** @brief Select default audio output device when not explicitly set. */
void dsd_bootstrap_choose_audio_output(dsd_opts* opts);
/** @brief Select default audio input device when not explicitly set. */
void dsd_bootstrap_choose_audio_input(dsd_opts* opts);
/** @brief Run the interactive bootstrap wizard when no CLI args/config are present. */
void dsd_bootstrap_interactive(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
