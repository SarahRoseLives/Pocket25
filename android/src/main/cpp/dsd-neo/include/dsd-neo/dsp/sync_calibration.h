// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Generic sync calibration module for threshold warm-start.
 *
 * Provides protocol-agnostic APIs for:
 * - Symbol history buffer management
 * - Threshold warm-start from outer-only sync patterns
 *
 * This module enables any FSK4 protocol (DMR, P25, YSF, NXDN, dPMR, M17) to
 * immediately calibrate slicer thresholds at sync detection, improving
 * first-frame decode accuracy.
 *
 * The warm-start API leverages the property that many sync patterns use only
 * outer symbols (+3/-3 in 4-level FSK), which allows direct min/max estimation
 * from the sync pattern alone.
 *
 * @see dmr_sync.h for DMR-specific resample-on-sync features
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct dsd_opts;
struct dsd_state;

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Default symbol history buffer size.
 *
 * Stores symbol-rate floats (one per dibit decision), not raw audio samples.
 * At 4800 sym/s, 2048 symbols covers ~427ms which is sufficient for any
 * protocol's sync + pre-sync window.
 */
#define DSD_SYMBOL_HISTORY_SIZE 2048

/**
 * @brief Minimum span between positive and negative symbol means.
 *
 * If the span is smaller than this value, warm-start is skipped as the signal
 * is likely degenerate or the sync pattern was not detected correctly.
 */
#define DSD_WARM_START_MIN_SPAN 1.0f

/* ─────────────────────────────────────────────────────────────────────────────
 * Symbol History Management
 *
 * Generic APIs for managing a circular buffer of recent symbol values.
 * These wrap the existing state->dmr_sample_history infrastructure but
 * provide a cleaner, protocol-agnostic interface.
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialize symbol history buffer.
 *
 * Allocates a circular buffer to store recent symbol values for retrospective
 * analysis at sync detection.
 *
 * @param state Decoder state
 * @param symbols Buffer size in symbols (use DSD_SYMBOL_HISTORY_SIZE for default)
 * @return 0 on success, -1 on allocation failure
 */
int dsd_symbol_history_init(struct dsd_state* state, int symbols);

/**
 * @brief Free symbol history buffer.
 * @param state Decoder state
 */
void dsd_symbol_history_free(struct dsd_state* state);

/**
 * @brief Reset symbol history (clear buffer, reset indices).
 * @param state Decoder state
 */
void dsd_symbol_history_reset(struct dsd_state* state);

/**
 * @brief Store a symbol in the history buffer.
 *
 * Should be called exactly once per symbol/dibit decision from getSymbol().
 *
 * @param state Decoder state
 * @param symbol Symbol value (typically in range -3 to +3)
 */
void dsd_symbol_history_push(struct dsd_state* state, float symbol);

/**
 * @brief Get a symbol from history at given offset from current position.
 *
 * @param state Decoder state
 * @param back Offset from most recent symbol (0 = newest, 1 = one before, etc.)
 * @return Symbol value at offset, or 0.0f if unavailable
 */
float dsd_symbol_history_get_back(const struct dsd_state* state, int back);

/**
 * @brief Get number of symbols currently in history buffer.
 * @param state Decoder state
 * @return Number of symbols stored (up to buffer size)
 */
int dsd_symbol_history_count(const struct dsd_state* state);

/* ─────────────────────────────────────────────────────────────────────────────
 * Threshold Warm-Start
 *
 * Protocol-agnostic warm-start from outer-only sync patterns.
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Result codes for warm-start operations.
 */
typedef enum {
    DSD_WARM_START_OK = 0,         /**< Warm-start applied successfully */
    DSD_WARM_START_DISABLED = 1,   /**< Warm-start disabled via env var */
    DSD_WARM_START_NO_HISTORY = 2, /**< Not enough symbols in history */
    DSD_WARM_START_DEGENERATE = 3, /**< Span too small (degenerate signal) */
    DSD_WARM_START_NULL_STATE = 4, /**< NULL state pointer */
} dsd_warm_start_result_t;

/**
 * @brief Warm-start slicer thresholds from outer-only sync pattern.
 *
 * Extracts the last `sync_len` symbols from history, assuming they represent
 * an outer-only sync pattern (only +3 and -3 symbols). Computes mean positive
 * and negative values to estimate actual signal levels, then initializes
 * slicer thresholds:
 *
 *   - state->min = mean of negative symbols
 *   - state->max = mean of positive symbols
 *   - state->center = (min + max) / 2
 *   - state->umid = center + 0.625 * (max - center)
 *   - state->lmid = center + 0.625 * (min - center)
 *   - state->minref = min * 0.80
 *   - state->maxref = max * 0.80
 *
 * Also pre-fills state->minbuf/maxbuf to skip warmup period.
 *
 * This function is protocol-agnostic and works with any outer-only sync:
 *   - DMR: sync_len = 24
 *   - P25p1: sync_len = 24
 *   - YSF: sync_len = 20
 *   - NXDN: sync_len = 10
 *   - dPMR: sync_len = 12
 *   - M17: sync_len = 8
 *
 * @param opts Decoder options (for msize; may be NULL)
 * @param state Decoder state to update
 * @param sync_len Number of symbols in sync pattern
 * @return Result code indicating success or reason for skip
 *
 * @note Can be disabled at runtime via DSD_NEO_SYNC_WARMSTART=0 env var.
 */
dsd_warm_start_result_t dsd_sync_warm_start_thresholds_outer_only(struct dsd_opts* opts, struct dsd_state* state,
                                                                  int sync_len);

/**
 * @brief Warm-start only the DC center estimate from an outer-only sync.
 *
 * CQPSK in dsd-neo uses a fixed-threshold slicer (see cqpsk_slice in
 * src/core/frames/dsd_dibit.c) and subtracts state->center to remove DC bias
 * before slicing. For this path, a safe sync-time calibration is to update
 * only state->center, leaving the CQPSK decision thresholds unchanged.
 *
 * This function estimates the two outer symbol clusters from the last
 * `sync_len` history entries and sets:
 *   - state->center = (mean_low + mean_high) / 2
 *
 * It does not modify state->min/max/lmid/umid or reference values.
 *
 * @param opts Decoder options (unused today; may be NULL)
 * @param state Decoder state to update
 * @param sync_len Number of symbols in sync pattern
 * @return Result code indicating success or reason for skip
 */
dsd_warm_start_result_t dsd_sync_warm_start_center_outer_only(struct dsd_opts* opts, struct dsd_state* state,
                                                              int sync_len);

/**
 * @brief Check if warm-start is enabled.
 *
 * Warm-start can be disabled via DSD_NEO_SYNC_WARMSTART=0 environment variable
 * for debugging and safe rollout.
 *
 * @return 1 if enabled (default), 0 if disabled
 */
int dsd_sync_warm_start_enabled(void);

#ifdef __cplusplus
}
#endif
