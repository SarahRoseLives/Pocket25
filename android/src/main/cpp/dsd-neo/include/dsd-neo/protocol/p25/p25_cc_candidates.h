// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief P25 control channel candidate list helpers.
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Control-channel candidates and neighbor list helpers for P25.
 * Provides small utilities to track announced neighbors, load/persist a
 * per-system candidate list, and expire stale entries.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build a per-system cache path for CC candidates.
 *
 * @param state Decoder state containing system identifiers.
 * @param out Destination buffer for the path.
 * @param out_len Capacity of the destination buffer.
 * @return 1 on success; 0 on error.
 */
int p25_cc_build_cache_path(const dsd_state* state, char* out, size_t out_len);

/**
 * @brief Add a control channel candidate (Hz) with deduplication and FIFO rollover.
 *
 * @param state Decoder state containing candidate list.
 * @param freq_hz Candidate control channel frequency in Hz.
 * @param bump_added Whether to increment the added counter/stat.
 * @return 1 if added; 0 if skipped.
 */
int p25_cc_add_candidate(dsd_state* state, long freq_hz, int bump_added);

/**
 * @brief Attempt to load a persisted candidate CC list (best-effort).
 *
 * @param opts Decoder options (used for logging/context).
 * @param state Decoder state to populate.
 */
void p25_cc_try_load_cache(dsd_opts* opts, dsd_state* state);
/**
 * @brief Persist the current candidate CC list (best-effort).
 *
 * @param opts Decoder options (used for logging/context).
 * @param state Decoder state containing candidates.
 */
void p25_cc_persist_cache(dsd_opts* opts, dsd_state* state);

/**
 * @brief Add a neighbor control channel candidate (Hz) to the in-memory list.
 *
 * @param state Decoder state containing neighbor list.
 * @param freq_hz Candidate control channel frequency in Hz.
 */
void p25_nb_add(dsd_state* state, long freq_hz);
/**
 * @brief Age/expire neighbor control channel candidates (call periodically).
 *
 * @param state Decoder state containing neighbor list.
 */
void p25_nb_tick(dsd_state* state);

#ifdef __cplusplus
}
#endif
