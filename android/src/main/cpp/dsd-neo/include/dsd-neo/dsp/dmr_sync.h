// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief DMR sync pattern correlation and resample-on-sync support.
 *
 * Implements SDRTrunk-style resample-on-sync for DMR to improve first-frame
 * decode accuracy. When sync is detected, this module:
 * 1. Correlates against known sync patterns to find optimal timing
 * 2. Initializes symbol thresholds from the sync pattern
 * 3. Resamples CACH and message prefix with corrected timing
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

#define DMR_SAMPLE_HISTORY_SIZE 2048 /* ~427ms at 4800 sym/s, covers CACH + sync + margin */
#define DMR_SYNC_SYMBOLS        24   /* Sync pattern length in symbols */
#define DMR_CACH_DIBITS         12   /* CACH length (6 dibits × 2 for interleave) */
#define DMR_RESAMPLE_SYMBOLS    66   /* CACH + message prefix to resample */

/* ─────────────────────────────────────────────────────────────────────────────
 * Types
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief DMR sync pattern identifiers.
 */
typedef enum {
    DMR_SYNC_BS_DATA = 0,      /**< Base station data sync */
    DMR_SYNC_BS_VOICE = 1,     /**< Base station voice sync */
    DMR_SYNC_MS_DATA = 2,      /**< Mobile station data sync */
    DMR_SYNC_MS_VOICE = 3,     /**< Mobile station voice sync */
    DMR_SYNC_DM_TS1_DATA = 4,  /**< Direct mode TS1 data */
    DMR_SYNC_DM_TS2_DATA = 5,  /**< Direct mode TS2 data */
    DMR_SYNC_DM_TS1_VOICE = 6, /**< Direct mode TS1 voice */
    DMR_SYNC_DM_TS2_VOICE = 7, /**< Direct mode TS2 voice */
    DMR_SYNC_PATTERN_COUNT = 8
} dmr_sync_pattern_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Sample History Management
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialize DMR sample history buffer.
 * @param state Decoder state
 * @return 0 on success, -1 on allocation failure
 */
int dmr_sample_history_init(struct dsd_state* state);

/**
 * @brief Free DMR sample history buffer.
 * @param state Decoder state
 */
void dmr_sample_history_free(struct dsd_state* state);

/**
 * @brief Reset DMR sample history (clear buffer, reset indices).
 * @param state Decoder state
 */
void dmr_sample_history_reset(struct dsd_state* state);

/**
 * @brief Store a sample in the history buffer.
 * @param state Decoder state
 * @param sample Raw sample value
 */
void dmr_sample_history_push(struct dsd_state* state, float sample);

/**
 * @brief Get a sample from history at given offset from current position.
 * @param state Decoder state
 * @param offset Negative offset from current position (0 = most recent)
 * @return Sample value at offset
 */
float dmr_sample_history_get(struct dsd_state* state, int offset);

/* ─────────────────────────────────────────────────────────────────────────────
 * Sync Correlation
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Calculate correlation score against a sync pattern at given timing.
 *
 * Uses linear interpolation for fractional sample positions.
 *
 * @param state Decoder state with sample history
 * @param offset Sample offset from current position (negative = past)
 * @param sps Samples per symbol
 * @param pattern Sync pattern identifier
 * @return Correlation score (higher = better match)
 */
float dmr_sync_score(struct dsd_state* state, float offset, float sps, dmr_sync_pattern_t pattern);

/**
 * @brief Extract sync symbols from sample history using linear interpolation.
 *
 * @param state Decoder state with sample history
 * @param offset Sample offset to sync pattern end
 * @param sps Samples per symbol
 * @param[out] symbols Array of 24 symbols to fill
 */
void dmr_extract_sync_symbols(struct dsd_state* state, float offset, float sps, float symbols[DMR_SYNC_SYMBOLS]);

/* ─────────────────────────────────────────────────────────────────────────────
 * Threshold Initialization
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialize symbol thresholds from detected sync pattern.
 *
 * Uses the fact that DMR sync patterns have equal +3 and -3 symbols to
 * immediately calibrate min/max/center/lmid/umid thresholds.
 *
 * @param opts Decoder options (for msize)
 * @param state Decoder state to update
 * @param sync_symbols 24 extracted sync symbol values
 */
void dmr_init_thresholds_from_sync(struct dsd_opts* opts, struct dsd_state* state,
                                   const float sync_symbols[DMR_SYNC_SYMBOLS]);

/* ─────────────────────────────────────────────────────────────────────────────
 * CACH Resampling
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Resample CACH and message prefix after sync detection.
 *
 * Goes back through sample history and re-digitizes the 66 symbols before
 * the sync pattern using calibrated timing and thresholds. Overwrites the
 * stale dibits in dmr_payload_buf.
 *
 * @param opts Decoder options
 * @param state Decoder state
 * @param sync_sample_offset Sample offset where sync was detected
 */
void dmr_resample_cach(struct dsd_opts* opts, struct dsd_state* state, int sync_sample_offset);

/**
 * @brief Perform full resample-on-sync sequence for DMR.
 *
 * Called after DMR sync detection. Performs:
 * 1. Extract sync symbols from history
 * 2. Initialize thresholds from sync pattern
 * 3. Resample CACH with corrected parameters
 *
 * @param opts Decoder options
 * @param state Decoder state
 * @return 0 on success, -1 if sample history unavailable
 */
int dmr_resample_on_sync(struct dsd_opts* opts, struct dsd_state* state);

#ifdef __cplusplus
}
#endif
