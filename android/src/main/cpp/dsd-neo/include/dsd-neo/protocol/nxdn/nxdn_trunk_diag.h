// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Record that a trunked NXDN channel had no known frequency mapping.
 *
 * This is deduplicated per-channel for the lifetime of @p state.
 *
 * @return 1 if the channel was newly recorded, 0 otherwise.
 */
int nxdn_trunk_diag_note_missing_channel(dsd_state* state, uint16_t channel);

/**
 * @brief Collect channels that were observed missing and are still unmapped.
 *
 * @param state    Decoder state.
 * @param out      Optional output array (may be NULL).
 * @param out_cap  Capacity of @p out (0 allowed).
 *
 * @return Total number of channels still unmapped (may be > out_cap).
 */
size_t nxdn_trunk_diag_collect_unmapped_channels(const dsd_state* state, uint16_t* out, size_t out_cap);

/**
 * @brief Log a rate-limited notice for a missing channel mapping.
 *
 * Logs only once per channel for the lifetime of @p state, and only when
 * a `chan_csv` was configured.
 */
void nxdn_trunk_diag_log_missing_channel_once(const dsd_opts* opts, dsd_state* state, uint16_t channel,
                                              const char* context);

/**
 * @brief Log an exit summary of channels still missing from the channel map.
 */
void nxdn_trunk_diag_log_summary(const dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
} /* extern "C" */
#endif
