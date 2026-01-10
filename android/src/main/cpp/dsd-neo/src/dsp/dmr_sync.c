// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief DMR sync pattern correlation and resample-on-sync implementation.
 *
 * Implements SDRTrunk-style resample-on-sync for DMR to improve first-frame
 * decode accuracy. This module provides:
 * - Symbol history buffer management
 * - Sync pattern correlation scoring
 * - Threshold initialization from sync patterns
 * - CACH re-digitization with corrected thresholds
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/dsp/dmr_sync.h>
#include <dsd-neo/dsp/sync_calibration.h>

#include <stdlib.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * DMR Sync Pattern Templates
 *
 * Each DMR sync pattern is 24 symbols. The patterns use only +3 and -3 levels,
 * with exactly 12 of each (balanced). This property is used for threshold
 * initialization.
 *
 * Pattern hex values from ETSI TS 102 361-1:
 *   BS_DATA:  0xDFF57D75DF5D
 *   BS_VOICE: 0x755FD7DF75F7
 *   MS_DATA:  0xD5D7F77FD757
 *   MS_VOICE: 0x7F7D5DD57DFD
 *   DM_TS1_DATA:  0xF7FDD5DDFD55
 *   DM_TS2_DATA:  0xD7557F5FF7F5
 *   DM_TS1_VOICE: 0x5D577F7757FF
 *   DM_TS2_VOICE: 0x7DFFD5F55D5F
 *
 * Dibit encoding: 01 = +3, 11 = -3 (only these two used in sync)
 * ───────────────────────────────────────────────────────────────────────────── */

/* Sync patterns as ideal symbol values (+3.0 or -3.0) */
static const float DMR_SYNC_PATTERNS[DMR_SYNC_PATTERN_COUNT][DMR_SYNC_SYMBOLS] = {
    /* DMR_SYNC_BS_DATA: 0xDFF57D75DF5D */
    {-3, +3, -3, -3, -3, -3, +3, +3, -3, +3, -3, -3, +3, -3, -3, +3, -3, +3, -3, -3, +3, -3, +3, -3},

    /* DMR_SYNC_BS_VOICE: 0x755FD7DF75F7 */
    {+3, -3, +3, +3, +3, +3, -3, -3, +3, -3, +3, +3, -3, +3, +3, -3, +3, -3, +3, +3, -3, +3, -3, +3},

    /* DMR_SYNC_MS_DATA: 0xD5D7F77FD757 */
    {-3, +3, +3, -3, -3, +3, +3, -3, -3, -3, +3, -3, +3, -3, -3, -3, -3, +3, +3, -3, +3, +3, -3, +3},

    /* DMR_SYNC_MS_VOICE: 0x7F7D5DD57DFD */
    {+3, -3, -3, +3, +3, -3, -3, +3, +3, +3, -3, +3, -3, +3, +3, +3, +3, -3, -3, +3, -3, -3, +3, -3},

    /* DMR_SYNC_DM_TS1_DATA: 0xF7FDD5DDFD55 */
    {-3, -3, +3, -3, -3, -3, +3, -3, -3, +3, +3, -3, -3, +3, -3, -3, -3, -3, +3, -3, +3, +3, +3, +3},

    /* DMR_SYNC_DM_TS2_DATA: 0xD7557F5FF7F5 */
    {-3, +3, -3, +3, +3, +3, +3, -3, +3, -3, -3, +3, +3, -3, -3, -3, -3, +3, -3, -3, +3, -3, +3, +3},

    /* DMR_SYNC_DM_TS1_VOICE: 0x5D577F7757FF */
    {+3, +3, -3, +3, +3, -3, +3, -3, +3, -3, -3, +3, -3, +3, -3, +3, +3, -3, +3, -3, -3, -3, -3, -3},

    /* DMR_SYNC_DM_TS2_VOICE: 0x7DFFD5F55D5F */
    {+3, -3, +3, -3, -3, -3, -3, +3, -3, +3, +3, -3, -3, +3, +3, +3, +3, -3, +3, +3, -3, +3, -3, -3},
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Sample History Management
 *
 * DMR history functions delegate to the generic sync_calibration module.
 * This provides a consistent API while allowing DMR-specific consumers
 * (like CACH resampling) to continue using the familiar dmr_* API.
 * ───────────────────────────────────────────────────────────────────────────── */

int
dmr_sample_history_init(dsd_state* state) {
    return dsd_symbol_history_init(state, DMR_SAMPLE_HISTORY_SIZE);
}

void
dmr_sample_history_free(dsd_state* state) {
    dsd_symbol_history_free(state);
}

void
dmr_sample_history_reset(dsd_state* state) {
    dsd_symbol_history_reset(state);
}

void
dmr_sample_history_push(dsd_state* state, float sample) {
    dsd_symbol_history_push(state, sample);
}

float
dmr_sample_history_get(dsd_state* state, int offset) {
    /* DMR API uses offset convention where 0 = most recent, negative = older.
     * Generic API uses 'back' where 0 = most recent, positive = older.
     * Convert: back = -offset when offset <= 0 */
    if (offset > 0) {
        return 0.0f; /* Invalid: DMR API doesn't support positive offsets */
    }
    return dsd_symbol_history_get_back(state, -offset);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Sync Correlation
 * ───────────────────────────────────────────────────────────────────────────── */

float
dmr_sync_score(dsd_state* state, float offset, float sps, dmr_sync_pattern_t pattern) {
    if (state == NULL || state->dmr_sample_history == NULL) {
        return 0.0f;
    }
    if (pattern < 0 || pattern >= DMR_SYNC_PATTERN_COUNT) {
        return 0.0f;
    }

    const float* ideal = DMR_SYNC_PATTERNS[pattern];
    float score = 0.0f;

    /* Calculate correlation: sum of (received * ideal) for each symbol */
    for (int i = 0; i < DMR_SYNC_SYMBOLS; i++) {
        /* Symbol position relative to current (offset is negative for past) */
        int sym_offset = (int)(offset - (DMR_SYNC_SYMBOLS - 1 - i) * sps);
        float symbol = dmr_sample_history_get(state, sym_offset);
        score += symbol * ideal[i];
    }

    return score;
}

void
dmr_extract_sync_symbols(dsd_state* state, float offset, float sps, float symbols[DMR_SYNC_SYMBOLS]) {
    if (state == NULL || state->dmr_sample_history == NULL || symbols == NULL) {
        return;
    }

    /* Extract 24 symbols from history, oldest first */
    for (int i = 0; i < DMR_SYNC_SYMBOLS; i++) {
        /* Symbol position: offset points to end of sync, go back 23..0 symbols */
        int sym_offset = (int)(offset - (DMR_SYNC_SYMBOLS - 1 - i) * sps);
        symbols[i] = dmr_sample_history_get(state, sym_offset);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Threshold Initialization
 * ───────────────────────────────────────────────────────────────────────────── */

void
dmr_init_thresholds_from_sync(dsd_opts* opts, dsd_state* state, const float sync_symbols[DMR_SYNC_SYMBOLS]) {
    if (state == NULL || sync_symbols == NULL) {
        return;
    }

    float sum_pos = 0.0f, sum_neg = 0.0f;
    int count_pos = 0, count_neg = 0;

    /* DMR sync patterns have exactly 12 positive (+3) and 12 negative (-3) symbols.
     * Use this to calculate average levels for threshold initialization. */
    for (int i = 0; i < DMR_SYNC_SYMBOLS; i++) {
        if (sync_symbols[i] > 0.0f) {
            sum_pos += sync_symbols[i];
            count_pos++;
        } else {
            sum_neg += sync_symbols[i];
            count_neg++;
        }
    }

    /* Avoid division by zero (shouldn't happen with valid sync) */
    if (count_pos == 0 || count_neg == 0) {
        return;
    }

    /* Calculate actual symbol levels from sync pattern */
    float actual_plus3 = sum_pos / (float)count_pos;
    float actual_minus3 = sum_neg / (float)count_neg;

    /* Initialize thresholds */
    state->max = actual_plus3;
    state->min = actual_minus3;
    state->center = (state->max + state->min) / 2.0f;

    /* Calculate mid-thresholds (62.5% toward extremes from center) */
    state->umid = state->center + (state->max - state->center) * 0.625f;
    state->lmid = state->center + (state->min - state->center) * 0.625f;

    /* Reference values (80% of extremes) */
    state->maxref = state->max * 0.80f;
    state->minref = state->min * 0.80f;

    /* Pre-fill rolling buffers to skip warmup period */
    if (opts != NULL) {
        for (int i = 0; i < opts->msize && i < 1024; i++) {
            state->maxbuf[i] = state->max;
            state->minbuf[i] = state->min;
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * CACH Resampling (Re-digitization)
 *
 * After sync detection, re-digitize the CACH and message prefix symbols
 * using the corrected thresholds from the sync pattern.
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Digitize a symbol using current thresholds.
 * @param state Decoder state with thresholds
 * @param symbol Raw symbol value
 * @return Dibit value (0-3)
 */
static int
dmr_digitize_symbol(dsd_state* state, float symbol) {
    /* 4-level slicer using calibrated thresholds */
    if (symbol > state->center) {
        if (symbol > state->umid) {
            return 1; /* +3 */
        } else {
            return 0; /* +1 */
        }
    } else {
        if (symbol < state->lmid) {
            return 3; /* -3 */
        } else {
            return 2; /* -1 */
        }
    }
}

void
dmr_resample_cach(dsd_opts* opts, dsd_state* state, int sync_sample_offset) {
    (void)opts; /* Currently unused */

    if (state == NULL || state->dmr_sample_history == NULL) {
        return;
    }

    if (state->dmr_payload_buf == NULL || state->dmr_payload_p == NULL) {
        return;
    }

    /* Check we have enough history */
    if (state->dmr_sample_history_count < DMR_RESAMPLE_SYMBOLS + DMR_SYNC_SYMBOLS) {
        return;
    }

    /* Re-digitize 66 symbols before the sync pattern (CACH + message prefix).
     * The sync pattern is at symbols [-24..-1] from current position.
     * CACH + prefix is at symbols [-90..-25] from current position.
     *
     * Note: sync_sample_offset is relative to current head, typically 0.
     */
    int start_offset = sync_sample_offset - DMR_SYNC_SYMBOLS - DMR_RESAMPLE_SYMBOLS + 1;

    /* The DMR payload buffer is a rolling append-only buffer; dmr_payload_p points
     * one past the most recently written dibit. At sync detection time, the
     * current (last) sync symbol is at (dmr_payload_p - 1). Therefore, the 66
     * symbols preceding the sync pattern map to:
     *   [dmr_payload_p - (DMR_SYNC_SYMBOLS + DMR_RESAMPLE_SYMBOLS) .. dmr_payload_p - DMR_SYNC_SYMBOLS - 1]
     */
    int* out_dibits = state->dmr_payload_p - (DMR_SYNC_SYMBOLS + DMR_RESAMPLE_SYMBOLS);

    for (int i = 0; i < DMR_RESAMPLE_SYMBOLS; i++) {
        /* Get symbol from history */
        float symbol = dmr_sample_history_get(state, start_offset + i);

        /* Re-digitize with corrected thresholds */
        int dibit = dmr_digitize_symbol(state, symbol);

        out_dibits[i] = dibit;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Main Resample-on-Sync Entry Point
 * ───────────────────────────────────────────────────────────────────────────── */

int
dmr_resample_on_sync(dsd_opts* opts, dsd_state* state) {
    if (state == NULL) {
        return -1;
    }

    /* Check sample history is available */
    if (state->dmr_sample_history == NULL || state->dmr_sample_history_count < DMR_SYNC_SYMBOLS) {
        return -1;
    }

    /* 1. Extract sync symbols from history (most recent 24 symbols) */
    float sync_symbols[DMR_SYNC_SYMBOLS];
    dmr_extract_sync_symbols(state, 0, 1.0f, sync_symbols);

    /* 2. Initialize thresholds from sync pattern */
    dmr_init_thresholds_from_sync(opts, state, sync_symbols);

    /* 3. Re-digitize CACH with corrected thresholds */
    dmr_resample_cach(opts, state, 0);

    return 0;
}
