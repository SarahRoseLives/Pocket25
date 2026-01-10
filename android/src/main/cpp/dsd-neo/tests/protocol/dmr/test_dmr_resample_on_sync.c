// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Unit tests for DMR resample-on-sync CACH re-digitization.
 *
 * Tests the complete resample-on-sync flow:
 * 1. Symbol history buffer push/get operations
 * 2. Sync pattern correlation scoring
 * 3. CACH re-digitization with corrected thresholds
 *
 * Verifies that re-digitization produces expected dibits in the correct
 * ring-buffer-relative positions.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/dsp/dmr_sync.h>

/* Tolerance for floating point comparisons */
#define FLOAT_TOL 0.01f

static int g_test_count = 0;
static int g_fail_count = 0;

static void
check_int(const char* name, int expected, int actual) {
    g_test_count++;
    if (expected != actual) {
        printf("FAIL: %s: expected %d, got %d\n", name, expected, actual);
        g_fail_count++;
    }
}

static void
check_float(const char* name, float expected, float actual, float tol) {
    g_test_count++;
    if (fabsf(expected - actual) > tol) {
        printf("FAIL: %s: expected %.4f, got %.4f\n", name, (double)expected, (double)actual);
        g_fail_count++;
    }
}

/**
 * @brief Test history buffer push and get operations.
 */
static void
test_history_buffer_ops(void) {
    printf("=== test_history_buffer_ops ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    /* Initialize history buffer */
    int ret = dmr_sample_history_init(&state);
    check_int("init return", 0, ret);
    check_int("buffer allocated", 1, state.dmr_sample_history != NULL);
    check_int("size", DMR_SAMPLE_HISTORY_SIZE, state.dmr_sample_history_size);
    check_int("head", 0, state.dmr_sample_history_head);
    check_int("count", 0, state.dmr_sample_history_count);

    /* Push some values */
    dmr_sample_history_push(&state, 1.0f);
    dmr_sample_history_push(&state, 2.0f);
    dmr_sample_history_push(&state, 3.0f);

    check_int("count after push", 3, state.dmr_sample_history_count);
    check_int("head after push", 3, state.dmr_sample_history_head);

    /* Get values: offset 0 is most recent, -1 is one before, etc. */
    check_float("get 0", 3.0f, dmr_sample_history_get(&state, 0), FLOAT_TOL);
    check_float("get -1", 2.0f, dmr_sample_history_get(&state, -1), FLOAT_TOL);
    check_float("get -2", 1.0f, dmr_sample_history_get(&state, -2), FLOAT_TOL);

    /* Reset */
    dmr_sample_history_reset(&state);
    check_int("count after reset", 0, state.dmr_sample_history_count);
    check_int("head after reset", 0, state.dmr_sample_history_head);

    /* Cleanup */
    dmr_sample_history_free(&state);
    check_int("buffer freed", 1, state.dmr_sample_history == NULL);

    printf("test_history_buffer_ops: passed\n\n");
}

/**
 * @brief Test history buffer wrap-around.
 */
static void
test_history_buffer_wrap(void) {
    printf("=== test_history_buffer_wrap ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    /* Use small buffer for wrap test */
    state.dmr_sample_history_size = 4;
    state.dmr_sample_history = (float*)malloc(sizeof(float) * 4);
    memset(state.dmr_sample_history, 0, sizeof(float) * 4);
    state.dmr_sample_history_head = 0;
    state.dmr_sample_history_count = 0;

    /* Push 6 values into size-4 buffer */
    for (int i = 1; i <= 6; i++) {
        dmr_sample_history_push(&state, (float)i);
    }

    /* Count should be clamped at buffer size */
    check_int("count clamped", 4, state.dmr_sample_history_count);

    /* Head should have wrapped */
    check_int("head wrapped", 2, state.dmr_sample_history_head);

    /* Buffer should contain [5, 6, 3, 4] with head at 2 */
    /* Most recent is 6, then 5, then 4, then 3 */
    check_float("get 0 (most recent)", 6.0f, dmr_sample_history_get(&state, 0), FLOAT_TOL);
    check_float("get -1", 5.0f, dmr_sample_history_get(&state, -1), FLOAT_TOL);
    check_float("get -2", 4.0f, dmr_sample_history_get(&state, -2), FLOAT_TOL);
    check_float("get -3", 3.0f, dmr_sample_history_get(&state, -3), FLOAT_TOL);

    free(state.dmr_sample_history);
    printf("test_history_buffer_wrap: passed\n\n");
}

/**
 * @brief Test sync correlation scoring.
 */
static void
test_sync_correlation(void) {
    printf("=== test_sync_correlation ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    /* Initialize history */
    dmr_sample_history_init(&state);

    /* Push ideal BS_VOICE sync pattern: +3/-3 alternating pattern */
    /* Pattern: {+3, -3, +3, +3, +3, +3, -3, -3, +3, -3, +3, +3, -3, +3, +3, -3, +3, -3, +3, +3, -3, +3, -3, +3} */
    float bs_voice[] = {+3.0f, -3.0f, +3.0f, +3.0f, +3.0f, +3.0f, -3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f,
                        -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f};

    for (int i = 0; i < DMR_SYNC_SYMBOLS; i++) {
        dmr_sample_history_push(&state, bs_voice[i]);
    }

    /* Score should be high for matching pattern */
    float score_match = dmr_sync_score(&state, 0, 1.0f, DMR_SYNC_BS_VOICE);
    /* Perfect match: sum of (±3)^2 for 24 symbols = 24 * 9 = 216 */
    check_float("score match", 216.0f, score_match, 1.0f);

    /* Score for wrong pattern should be lower or negative */
    float score_wrong = dmr_sync_score(&state, 0, 1.0f, DMR_SYNC_BS_DATA);
    g_test_count++;
    if (score_wrong >= score_match) {
        printf("FAIL: wrong pattern score (%.1f) should be less than match (%.1f)\n", (double)score_wrong,
               (double)score_match);
        g_fail_count++;
    }

    dmr_sample_history_free(&state);
    printf("test_sync_correlation: passed\n\n");
}

/**
 * @brief Test symbol extraction from history.
 */
static void
test_symbol_extraction(void) {
    printf("=== test_symbol_extraction ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    dmr_sample_history_init(&state);

    /* Push known pattern */
    float pattern[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    for (int i = 0; i < 6; i++) {
        dmr_sample_history_push(&state, pattern[i]);
    }

    /* Push padding to simulate sync at end of buffer */
    for (int i = 0; i < DMR_SYNC_SYMBOLS - 6; i++) {
        dmr_sample_history_push(&state, 0.0f);
    }

    /* Push sync pattern (BS_VOICE) */
    float bs_voice[] = {+3.0f, -3.0f, +3.0f, +3.0f, +3.0f, +3.0f, -3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f,
                        -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f};
    for (int i = 0; i < DMR_SYNC_SYMBOLS; i++) {
        dmr_sample_history_push(&state, bs_voice[i]);
    }

    /* Extract sync symbols */
    float extracted[DMR_SYNC_SYMBOLS];
    dmr_extract_sync_symbols(&state, 0, 1.0f, extracted);

    /* Verify extracted symbols match what we pushed */
    for (int i = 0; i < DMR_SYNC_SYMBOLS; i++) {
        char name[32];
        snprintf(name, sizeof(name), "extracted[%d]", i);
        check_float(name, bs_voice[i], extracted[i], FLOAT_TOL);
    }

    dmr_sample_history_free(&state);
    printf("test_symbol_extraction: passed\n\n");
}

/**
 * @brief Test CACH re-digitization with ideal thresholds.
 *
 * This test verifies that re-digitization produces expected dibits
 * in the payload buffer.
 */
static void
test_cach_redigitize(void) {
    printf("=== test_cach_redigitize ===\n");

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    /* Initialize history buffer */
    dmr_sample_history_init(&state);

    /* Set up ideal thresholds for ±3/±1 symbol levels */
    state.max = 3.0f;
    state.min = -3.0f;
    state.center = 0.0f;
    state.umid = 1.875f;  /* 0.625 * 3 */
    state.lmid = -1.875f; /* 0.625 * -3 */
    state.maxref = 2.4f;
    state.minref = -2.4f;

    /* Create test pattern: known symbol values that map to known dibits
     * Symbol levels: +3 -> dibit 1, +1 -> dibit 0, -1 -> dibit 2, -3 -> dibit 3
     */
    float test_symbols[DMR_RESAMPLE_SYMBOLS + DMR_SYNC_SYMBOLS];

    /* Fill CACH region (66 symbols) with known pattern */
    for (int i = 0; i < DMR_RESAMPLE_SYMBOLS; i++) {
        /* Cycle through all 4 symbol levels */
        switch (i % 4) {
            case 0: test_symbols[i] = 3.0f; break;  /* -> dibit 1 */
            case 1: test_symbols[i] = 1.0f; break;  /* -> dibit 0 */
            case 2: test_symbols[i] = -1.0f; break; /* -> dibit 2 */
            case 3: test_symbols[i] = -3.0f; break; /* -> dibit 3 */
            default: test_symbols[i] = 0.0f; break;
        }
    }

    /* Fill sync region (24 symbols) with BS_VOICE pattern */
    float bs_voice[] = {+3.0f, -3.0f, +3.0f, +3.0f, +3.0f, +3.0f, -3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f,
                        -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f};
    for (int i = 0; i < DMR_SYNC_SYMBOLS; i++) {
        test_symbols[DMR_RESAMPLE_SYMBOLS + i] = bs_voice[i];
    }

    /* Push all symbols into history */
    for (int i = 0; i < DMR_RESAMPLE_SYMBOLS + DMR_SYNC_SYMBOLS; i++) {
        dmr_sample_history_push(&state, test_symbols[i]);
    }

    /* Allocate payload buffer */
    const int payload_len = DMR_RESAMPLE_SYMBOLS + DMR_SYNC_SYMBOLS;
    state.dmr_payload_buf = (int*)malloc(sizeof(int) * payload_len);
    memset(state.dmr_payload_buf, 0xFF, sizeof(int) * payload_len);
    /* Mimic real decoder state: dmr_payload_p points one past the most recent dibit. */
    state.dmr_payload_p = state.dmr_payload_buf + payload_len;

    /* Call CACH resample */
    dmr_resample_cach(&opts, &state, 0);

    /* Verify re-digitized dibits */
    int all_correct = 1;
    for (int i = 0; i < DMR_RESAMPLE_SYMBOLS; i++) {
        int expected_dibit;
        switch (i % 4) {
            case 0: expected_dibit = 1; break; /* +3 */
            case 1: expected_dibit = 0; break; /* +1 */
            case 2: expected_dibit = 2; break; /* -1 */
            case 3: expected_dibit = 3; break; /* -3 */
            default: expected_dibit = -1; break;
        }

        if (state.dmr_payload_buf[i] != expected_dibit) {
            if (all_correct) {
                printf("FAIL: dibit mismatch at index %d: expected %d, got %d\n", i, expected_dibit,
                       state.dmr_payload_buf[i]);
            }
            all_correct = 0;
        }
    }

    g_test_count++;
    if (!all_correct) {
        g_fail_count++;
    }

    free(state.dmr_payload_buf);
    dmr_sample_history_free(&state);
    printf("test_cach_redigitize: passed\n\n");
}

/**
 * @brief Test full resample_on_sync flow.
 */
static void
test_full_resample_on_sync(void) {
    printf("=== test_full_resample_on_sync ===\n");

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 128;

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    /* Initialize history buffer */
    dmr_sample_history_init(&state);

    /* Allocate payload buffer */
    state.dmr_payload_buf = (int*)malloc(sizeof(int) * DMR_RESAMPLE_SYMBOLS);
    memset(state.dmr_payload_buf, 0xFF, sizeof(int) * DMR_RESAMPLE_SYMBOLS);

    /* Push CACH + sync worth of symbols */
    /* CACH with mild DC offset */
    float dc_offset = 0.2f;
    for (int i = 0; i < DMR_RESAMPLE_SYMBOLS; i++) {
        float sym;
        switch (i % 4) {
            case 0: sym = 3.0f + dc_offset; break;
            case 1: sym = 1.0f + dc_offset; break;
            case 2: sym = -1.0f + dc_offset; break;
            case 3: sym = -3.0f + dc_offset; break;
            default: sym = 0.0f; break;
        }
        dmr_sample_history_push(&state, sym);
    }

    /* Sync pattern with same DC offset */
    float bs_voice[] = {+3.0f, -3.0f, +3.0f, +3.0f, +3.0f, +3.0f, -3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f,
                        -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f};
    for (int i = 0; i < DMR_SYNC_SYMBOLS; i++) {
        dmr_sample_history_push(&state, bs_voice[i] + dc_offset);
    }

    /* Call full resample_on_sync */
    int ret = dmr_resample_on_sync(&opts, &state);

    check_int("resample_on_sync return", 0, ret);

    /* Thresholds should be initialized */
    g_test_count++;
    if (state.max < 2.5f || state.max > 3.5f) {
        printf("FAIL: max threshold (%.3f) out of range\n", (double)state.max);
        g_fail_count++;
    }

    free(state.dmr_payload_buf);
    dmr_sample_history_free(&state);
    printf("test_full_resample_on_sync: passed\n\n");
}

int
main(void) {
    printf("DMR Resample-on-Sync Tests\n");
    printf("==========================\n\n");

    test_history_buffer_ops();
    test_history_buffer_wrap();
    test_sync_correlation();
    test_symbol_extraction();
    test_cach_redigitize();
    test_full_resample_on_sync();

    printf("==========================\n");
    printf("Tests: %d, Failures: %d\n", g_test_count, g_fail_count);

    if (g_fail_count > 0) {
        printf("FAILED\n");
        return 1;
    }

    printf("PASSED: All resample-on-sync tests passed\n");
    return 0;
}
