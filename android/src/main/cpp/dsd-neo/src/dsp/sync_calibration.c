// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Generic sync calibration module implementation.
 *
 * Provides protocol-agnostic warm-start threshold initialization from
 * outer-only sync patterns. This module wraps the existing DMR sample
 * history infrastructure to provide a clean API for all supported protocols.
 */

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/dsp/sync_calibration.h>
#include <dsd-neo/runtime/config.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int
float_compare_asc(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    if (fa < fb) {
        return -1;
    }
    if (fa > fb) {
        return 1;
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Environment Variable Kill-Switch
 *
 * DSD_NEO_SYNC_WARMSTART=0 disables warm-start for safe rollout/debugging.
 * ───────────────────────────────────────────────────────────────────────────── */

int
dsd_sync_warm_start_enabled(void) {
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (!cfg) {
        dsd_neo_config_init(NULL);
        cfg = dsd_neo_get_config();
    }
    return (cfg && cfg->sync_warmstart_enable) ? 1 : 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Symbol History Management
 *
 * These functions wrap the existing DMR sample history infrastructure
 * (state->dmr_sample_history*) to provide a protocol-agnostic API.
 * ───────────────────────────────────────────────────────────────────────────── */

int
dsd_symbol_history_init(dsd_state* state, int symbols) {
    if (state == NULL || symbols <= 0) {
        return -1;
    }

    /* Free existing buffer if present */
    if (state->dmr_sample_history != NULL) {
        free(state->dmr_sample_history);
        state->dmr_sample_history = NULL;
    }

    state->dmr_sample_history_size = symbols;
    state->dmr_sample_history = (float*)malloc(sizeof(float) * (size_t)symbols);
    if (state->dmr_sample_history == NULL) {
        state->dmr_sample_history_size = 0;
        return -1;
    }

    memset(state->dmr_sample_history, 0, sizeof(float) * (size_t)symbols);
    state->dmr_sample_history_head = 0;
    state->dmr_sample_history_count = 0;

    return 0;
}

void
dsd_symbol_history_free(dsd_state* state) {
    if (state == NULL) {
        return;
    }

    if (state->dmr_sample_history != NULL) {
        free(state->dmr_sample_history);
        state->dmr_sample_history = NULL;
    }
    state->dmr_sample_history_size = 0;
    state->dmr_sample_history_head = 0;
    state->dmr_sample_history_count = 0;
}

void
dsd_symbol_history_reset(dsd_state* state) {
    if (state == NULL || state->dmr_sample_history == NULL) {
        return;
    }

    memset(state->dmr_sample_history, 0, sizeof(float) * (size_t)state->dmr_sample_history_size);
    state->dmr_sample_history_head = 0;
    state->dmr_sample_history_count = 0;
}

void
dsd_symbol_history_push(dsd_state* state, float symbol) {
    if (state == NULL || state->dmr_sample_history == NULL) {
        return;
    }

    state->dmr_sample_history[state->dmr_sample_history_head] = symbol;
    state->dmr_sample_history_head = (state->dmr_sample_history_head + 1) % state->dmr_sample_history_size;
    if (state->dmr_sample_history_count < state->dmr_sample_history_size) {
        state->dmr_sample_history_count++;
    }
}

float
dsd_symbol_history_get_back(const dsd_state* state, int back) {
    if (state == NULL || state->dmr_sample_history == NULL) {
        return 0.0f;
    }

    if (back < 0 || back >= state->dmr_sample_history_count) {
        return 0.0f;
    }

    /* back=0 is most recent, which is at head-1 */
    int idx = state->dmr_sample_history_head - 1 - back;
    while (idx < 0) {
        idx += state->dmr_sample_history_size;
    }
    idx = idx % state->dmr_sample_history_size;

    return state->dmr_sample_history[idx];
}

int
dsd_symbol_history_count(const dsd_state* state) {
    if (state == NULL) {
        return 0;
    }
    return state->dmr_sample_history_count;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Threshold Warm-Start
 * ───────────────────────────────────────────────────────────────────────────── */

dsd_warm_start_result_t
dsd_sync_warm_start_thresholds_outer_only(dsd_opts* opts, dsd_state* state, int sync_len) {
    /* Check kill-switch */
    if (!dsd_sync_warm_start_enabled()) {
        return DSD_WARM_START_DISABLED;
    }

    /* Validate state */
    if (state == NULL) {
        return DSD_WARM_START_NULL_STATE;
    }

    /* Check history availability */
    if (state->dmr_sample_history == NULL || state->dmr_sample_history_count < sync_len) {
        return DSD_WARM_START_NO_HISTORY;
    }

    /* Extract sync symbols from history and compute means */
    float sum_pos = 0.0f, sum_neg = 0.0f;
    int count_pos = 0, count_neg = 0;

    for (int i = 0; i < sync_len; i++) {
        /* back=0 is newest symbol (last symbol of sync)
         * We want oldest first, so back = sync_len - 1 - i for sequential order,
         * but for computing means the order doesn't matter. */
        float symbol = dsd_symbol_history_get_back(state, i);

        if (symbol > 0.0f) {
            sum_pos += symbol;
            count_pos++;
        } else {
            sum_neg += symbol;
            count_neg++;
        }
    }

    /* Require both positive and negative symbols for outer-only pattern */
    if (count_pos == 0 || count_neg == 0) {
        return DSD_WARM_START_DEGENERATE;
    }

    /* Calculate actual symbol levels from sync pattern */
    float mean_pos = sum_pos / (float)count_pos;
    float mean_neg = sum_neg / (float)count_neg;

    /* Check for degenerate span (signal too weak or invalid) */
    float span = mean_pos - mean_neg;
    if (fabsf(span) < DSD_WARM_START_MIN_SPAN) {
        return DSD_WARM_START_DEGENERATE;
    }

    /* Initialize thresholds */
    state->max = mean_pos;
    state->min = mean_neg;
    state->center = (state->max + state->min) / 2.0f;

    /* Calculate mid-thresholds (62.5% toward extremes from center) */
    state->umid = state->center + (state->max - state->center) * 0.625f;
    state->lmid = state->center + (state->min - state->center) * 0.625f;

    /* Reference values (80% of extremes) */
    state->maxref = state->max * 0.80f;
    state->minref = state->min * 0.80f;

    /* Pre-fill rolling buffers to skip warmup period */
    if (opts != NULL) {
        int fill_count = opts->msize;
        if (fill_count > 1024) {
            fill_count = 1024;
        }
        for (int i = 0; i < fill_count; i++) {
            state->maxbuf[i] = state->max;
            state->minbuf[i] = state->min;
        }
    }

    return DSD_WARM_START_OK;
}

dsd_warm_start_result_t
dsd_sync_warm_start_center_outer_only(dsd_opts* opts, dsd_state* state, int sync_len) {
    UNUSED(opts);

    if (!dsd_sync_warm_start_enabled()) {
        return DSD_WARM_START_DISABLED;
    }

    if (state == NULL) {
        return DSD_WARM_START_NULL_STATE;
    }

    if (sync_len <= 1) {
        return DSD_WARM_START_DEGENERATE;
    }

    if (state->dmr_sample_history == NULL || state->dmr_sample_history_count < sync_len) {
        return DSD_WARM_START_NO_HISTORY;
    }

    float stack_syms[64];
    float* syms = stack_syms;
    int free_syms = 0;

    if (sync_len > (int)(sizeof(stack_syms) / sizeof(stack_syms[0]))) {
        syms = (float*)malloc(sizeof(float) * (size_t)sync_len);
        if (syms == NULL) {
            return DSD_WARM_START_DEGENERATE;
        }
        free_syms = 1;
    }

    for (int i = 0; i < sync_len; i++) {
        syms[i] = dsd_symbol_history_get_back(state, i);
    }

    qsort(syms, (size_t)sync_len, sizeof(float), float_compare_asc);

    /* Split the bimodal distribution using the largest inter-sample gap. */
    int split = -1;
    float max_gap = -1.0f;
    for (int i = 0; i < (sync_len - 1); i++) {
        float gap = syms[i + 1] - syms[i];
        if (gap > max_gap) {
            max_gap = gap;
            split = i;
        }
    }

    if (split < 0 || split >= (sync_len - 1)) {
        if (free_syms) {
            free(syms);
        }
        return DSD_WARM_START_DEGENERATE;
    }

    int n_low = split + 1;
    int n_high = sync_len - n_low;
    if (n_low < 2 || n_high < 2) {
        if (free_syms) {
            free(syms);
        }
        return DSD_WARM_START_DEGENERATE;
    }

    float sum_low = 0.0f;
    for (int i = 0; i < n_low; i++) {
        sum_low += syms[i];
    }
    float sum_high = 0.0f;
    for (int i = n_low; i < sync_len; i++) {
        sum_high += syms[i];
    }

    float mean_low = sum_low / (float)n_low;
    float mean_high = sum_high / (float)n_high;

    float span = mean_high - mean_low;
    if (fabsf(span) < DSD_WARM_START_MIN_SPAN) {
        if (free_syms) {
            free(syms);
        }
        return DSD_WARM_START_DEGENERATE;
    }

    state->center = (mean_low + mean_high) / 2.0f;

    if (free_syms) {
        free(syms);
    }

    return DSD_WARM_START_OK;
}
