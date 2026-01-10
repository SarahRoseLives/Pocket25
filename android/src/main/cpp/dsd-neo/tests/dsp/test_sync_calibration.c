// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Unit tests for generic sync calibration module.
 *
 * Tests the protocol-agnostic symbol history and warm-start APIs
 * provided by sync_calibration.h.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/dsp/sync_calibration.h>

#define FLOAT_TOL 0.01f

static int g_test_count = 0;
static int g_fail_count = 0;

static void
check_float(const char* name, float expected, float actual, float tol) {
    g_test_count++;
    if (fabsf(expected - actual) > tol) {
        printf("FAIL: %s: expected %.4f, got %.4f\n", name, (double)expected, (double)actual);
        g_fail_count++;
    }
}

static void
check_int(const char* name, int expected, int actual) {
    g_test_count++;
    if (expected != actual) {
        printf("FAIL: %s: expected %d, got %d\n", name, expected, actual);
        g_fail_count++;
    }
}

static void
check_float_range(const char* name, float min, float max, float actual) {
    g_test_count++;
    if (actual < min || actual > max) {
        printf("FAIL: %s: expected [%.4f, %.4f], got %.4f\n", name, (double)min, (double)max, (double)actual);
        g_fail_count++;
    }
}

/**
 * @brief Test symbol history initialization and basic operations.
 */
static void
test_history_basic_ops(void) {
    printf("=== test_history_basic_ops ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    /* Initialize history */
    int rc = dsd_symbol_history_init(&state, 64);
    check_int("init return", 0, rc);
    check_int("initial count", 0, dsd_symbol_history_count(&state));

    /* Push some symbols */
    dsd_symbol_history_push(&state, 1.0f);
    dsd_symbol_history_push(&state, 2.0f);
    dsd_symbol_history_push(&state, 3.0f);
    check_int("count after push", 3, dsd_symbol_history_count(&state));

    /* Get symbols back */
    check_float("get_back(0)", 3.0f, dsd_symbol_history_get_back(&state, 0), FLOAT_TOL);
    check_float("get_back(1)", 2.0f, dsd_symbol_history_get_back(&state, 1), FLOAT_TOL);
    check_float("get_back(2)", 1.0f, dsd_symbol_history_get_back(&state, 2), FLOAT_TOL);

    /* Reset and verify empty */
    dsd_symbol_history_reset(&state);
    check_int("count after reset", 0, dsd_symbol_history_count(&state));

    /* Free */
    dsd_symbol_history_free(&state);
    check_int("count after free", 0, state.dmr_sample_history_count);

    printf("test_history_basic_ops: passed\n\n");
}

/**
 * @brief Test history buffer wrap-around.
 */
static void
test_history_wraparound(void) {
    printf("=== test_history_wraparound ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    /* Small buffer for easy wrap testing */
    int rc = dsd_symbol_history_init(&state, 4);
    check_int("init return", 0, rc);

    /* Push 6 symbols to force wrap */
    for (int i = 1; i <= 6; i++) {
        dsd_symbol_history_push(&state, (float)i);
    }

    /* Count should be capped at buffer size */
    check_int("count capped", 4, dsd_symbol_history_count(&state));

    /* Most recent 4 symbols should be 6, 5, 4, 3 */
    check_float("get_back(0)", 6.0f, dsd_symbol_history_get_back(&state, 0), FLOAT_TOL);
    check_float("get_back(1)", 5.0f, dsd_symbol_history_get_back(&state, 1), FLOAT_TOL);
    check_float("get_back(2)", 4.0f, dsd_symbol_history_get_back(&state, 2), FLOAT_TOL);
    check_float("get_back(3)", 3.0f, dsd_symbol_history_get_back(&state, 3), FLOAT_TOL);

    /* Out of range should return 0 */
    check_float("get_back(4) oob", 0.0f, dsd_symbol_history_get_back(&state, 4), FLOAT_TOL);

    dsd_symbol_history_free(&state);
    printf("test_history_wraparound: passed\n\n");
}

/**
 * @brief Test warm-start with ideal outer-only sync pattern.
 */
static void
test_warm_start_ideal(void) {
    printf("=== test_warm_start_ideal ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 64;

    /* Initialize history */
    dsd_symbol_history_init(&state, 64);

    /* Push a 24-symbol outer-only sync pattern (12 x +3, 12 x -3) */
    float pattern[] = {+3.0f, -3.0f, +3.0f, +3.0f, +3.0f, +3.0f, -3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f,
                       -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f};
    for (int i = 0; i < 24; i++) {
        dsd_symbol_history_push(&state, pattern[i]);
    }

    /* Apply warm-start */
    dsd_warm_start_result_t result = dsd_sync_warm_start_thresholds_outer_only(&opts, &state, 24);
    check_int("warm_start result", DSD_WARM_START_OK, result);

    /* Verify thresholds */
    check_float("max", 3.0f, state.max, FLOAT_TOL);
    check_float("min", -3.0f, state.min, FLOAT_TOL);
    check_float("center", 0.0f, state.center, FLOAT_TOL);

    /* Mid thresholds: 62.5% from center toward extremes */
    check_float("umid", 1.875f, state.umid, FLOAT_TOL);
    check_float("lmid", -1.875f, state.lmid, FLOAT_TOL);

    dsd_symbol_history_free(&state);
    printf("test_warm_start_ideal: passed\n\n");
}

/**
 * @brief Test warm-start with DC offset.
 */
static void
test_warm_start_dc_offset(void) {
    printf("=== test_warm_start_dc_offset ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 64;

    dsd_symbol_history_init(&state, 64);

    /* Sync pattern with +0.5 DC offset */
    float dc = 0.5f;
    float pattern[] = {+3.0f + dc, -3.0f + dc, +3.0f + dc, +3.0f + dc, +3.0f + dc, +3.0f + dc, -3.0f + dc, -3.0f + dc,
                       +3.0f + dc, -3.0f + dc, +3.0f + dc, +3.0f + dc, -3.0f + dc, +3.0f + dc, +3.0f + dc, -3.0f + dc,
                       +3.0f + dc, -3.0f + dc, +3.0f + dc, +3.0f + dc, -3.0f + dc, +3.0f + dc, -3.0f + dc, +3.0f + dc};
    for (int i = 0; i < 24; i++) {
        dsd_symbol_history_push(&state, pattern[i]);
    }

    dsd_warm_start_result_t result = dsd_sync_warm_start_thresholds_outer_only(&opts, &state, 24);
    check_int("warm_start result", DSD_WARM_START_OK, result);

    check_float("max", 3.5f, state.max, FLOAT_TOL);
    check_float("min", -2.5f, state.min, FLOAT_TOL);
    check_float("center", 0.5f, state.center, FLOAT_TOL);

    dsd_symbol_history_free(&state);
    printf("test_warm_start_dc_offset: passed\n\n");
}

/**
 * @brief Test CQPSK-safe "center-only" warm-start.
 *
 * This should update only state.center (DC bias estimate) and leave other
 * thresholds unchanged.
 */
static void
test_center_only_warm_start(void) {
    printf("=== test_center_only_warm_start ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 64;

    dsd_symbol_history_init(&state, 64);

    /* Seed state with sentinel values to ensure only center changes. */
    state.center = 123.0f;
    state.max = 9.0f;
    state.min = -9.0f;
    state.umid = 7.0f;
    state.lmid = -7.0f;
    state.maxref = 8.0f;
    state.minref = -8.0f;

    /* Unbalanced outer-only sync (+3/-3) with DC offset (matches P25p1 characteristic imbalance). */
    float dc = 0.5f;
    for (int i = 0; i < 11; i++) {
        dsd_symbol_history_push(&state, 3.0f + dc);
    }
    for (int i = 0; i < 13; i++) {
        dsd_symbol_history_push(&state, -3.0f + dc);
    }

    dsd_warm_start_result_t result = dsd_sync_warm_start_center_outer_only(&opts, &state, 24);
    check_int("center_only result", DSD_WARM_START_OK, result);
    check_float("center", dc, state.center, FLOAT_TOL);

    /* Verify other thresholds are untouched. */
    check_float("max unchanged", 9.0f, state.max, FLOAT_TOL);
    check_float("min unchanged", -9.0f, state.min, FLOAT_TOL);
    check_float("umid unchanged", 7.0f, state.umid, FLOAT_TOL);
    check_float("lmid unchanged", -7.0f, state.lmid, FLOAT_TOL);
    check_float("maxref unchanged", 8.0f, state.maxref, FLOAT_TOL);
    check_float("minref unchanged", -8.0f, state.minref, FLOAT_TOL);

    dsd_symbol_history_free(&state);
    printf("test_center_only_warm_start: passed\n\n");
}

/**
 * @brief Test center-only warm-start remains robust under a large DC bias.
 */
static void
test_center_only_large_bias(void) {
    printf("=== test_center_only_large_bias ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 64;

    dsd_symbol_history_init(&state, 64);

    /* DC bias large enough that both clusters are positive. */
    float dc = 10.0f;
    for (int i = 0; i < 10; i++) {
        dsd_symbol_history_push(&state, 3.0f + dc);
        dsd_symbol_history_push(&state, -3.0f + dc);
    }

    dsd_warm_start_result_t result = dsd_sync_warm_start_center_outer_only(&opts, &state, 20);
    check_int("center_only result", DSD_WARM_START_OK, result);
    check_float("center", dc, state.center, FLOAT_TOL);

    dsd_symbol_history_free(&state);
    printf("test_center_only_large_bias: passed\n\n");
}

/**
 * @brief Test warm-start returns appropriate error when history is insufficient.
 */
static void
test_warm_start_insufficient_history(void) {
    printf("=== test_warm_start_insufficient_history ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 64;

    dsd_symbol_history_init(&state, 64);

    /* Push only 10 symbols but request 24 */
    for (int i = 0; i < 10; i++) {
        dsd_symbol_history_push(&state, 3.0f);
    }

    dsd_warm_start_result_t result = dsd_sync_warm_start_thresholds_outer_only(&opts, &state, 24);
    check_int("warm_start result", DSD_WARM_START_NO_HISTORY, result);

    dsd_symbol_history_free(&state);
    printf("test_warm_start_insufficient_history: passed\n\n");
}

/**
 * @brief Test warm-start returns appropriate error for degenerate signal.
 */
static void
test_warm_start_degenerate(void) {
    printf("=== test_warm_start_degenerate ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 64;

    dsd_symbol_history_init(&state, 64);

    /* Push all positive symbols (no negative) */
    for (int i = 0; i < 24; i++) {
        dsd_symbol_history_push(&state, 3.0f);
    }

    dsd_warm_start_result_t result = dsd_sync_warm_start_thresholds_outer_only(&opts, &state, 24);
    check_int("warm_start result (all pos)", DSD_WARM_START_DEGENERATE, result);

    /* Now test very small span */
    dsd_symbol_history_reset(&state);
    for (int i = 0; i < 12; i++) {
        dsd_symbol_history_push(&state, 0.3f);
        dsd_symbol_history_push(&state, -0.3f);
    }

    result = dsd_sync_warm_start_thresholds_outer_only(&opts, &state, 24);
    check_int("warm_start result (small span)", DSD_WARM_START_DEGENERATE, result);

    dsd_symbol_history_free(&state);
    printf("test_warm_start_degenerate: passed\n\n");
}

/**
 * @brief Test warm-start with different sync lengths.
 *
 * Verifies the generic API works with various protocol sync lengths.
 */
static void
test_warm_start_various_sync_lengths(void) {
    printf("=== test_warm_start_various_sync_lengths ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 64;

    /* Test with sync lengths from different protocols */
    int sync_lengths[] = {8, 10, 12, 20, 24}; /* M17, NXDN, dPMR, YSF, DMR/P25 */
    int num_lengths = sizeof(sync_lengths) / sizeof(sync_lengths[0]);

    for (int t = 0; t < num_lengths; t++) {
        int sync_len = sync_lengths[t];
        dsd_symbol_history_init(&state, 64);

        /* Push alternating +3/-3 pattern */
        for (int i = 0; i < sync_len; i++) {
            float sym = (i % 2 == 0) ? 3.0f : -3.0f;
            dsd_symbol_history_push(&state, sym);
        }

        dsd_warm_start_result_t result = dsd_sync_warm_start_thresholds_outer_only(&opts, &state, sync_len);
        char buf[64];
        snprintf(buf, sizeof(buf), "sync_len=%d result", sync_len);
        check_int(buf, DSD_WARM_START_OK, result);

        snprintf(buf, sizeof(buf), "sync_len=%d max", sync_len);
        check_float(buf, 3.0f, state.max, FLOAT_TOL);

        snprintf(buf, sizeof(buf), "sync_len=%d min", sync_len);
        check_float(buf, -3.0f, state.min, FLOAT_TOL);

        dsd_symbol_history_free(&state);
    }

    printf("test_warm_start_various_sync_lengths: passed\n\n");
}

/**
 * @brief Test null pointer handling.
 */
static void
test_null_handling(void) {
    printf("=== test_null_handling ===\n");

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 64;

    /* NULL state */
    dsd_warm_start_result_t result = dsd_sync_warm_start_thresholds_outer_only(&opts, NULL, 24);
    check_int("null state", DSD_WARM_START_NULL_STATE, result);

    /* NULL state for history functions should not crash */
    dsd_symbol_history_push(NULL, 3.0f);
    check_float("push to null", 0.0f, dsd_symbol_history_get_back(NULL, 0), FLOAT_TOL);
    check_int("count from null", 0, dsd_symbol_history_count(NULL));

    printf("test_null_handling: passed\n\n");
}

/**
 * @brief Test buffer pre-fill during warm-start.
 */
static void
test_buffer_prefill(void) {
    printf("=== test_buffer_prefill ===\n");

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 32;

    dsd_symbol_history_init(&state, 64);

    /* Push sync pattern */
    for (int i = 0; i < 24; i++) {
        float sym = (i % 2 == 0) ? 3.0f : -3.0f;
        dsd_symbol_history_push(&state, sym);
    }

    dsd_sync_warm_start_thresholds_outer_only(&opts, &state, 24);

    /* Check buffers are pre-filled */
    int all_filled = 1;
    for (int i = 0; i < opts.msize; i++) {
        if (fabsf(state.maxbuf[i] - 3.0f) > FLOAT_TOL) {
            all_filled = 0;
            printf("FAIL: maxbuf[%d] = %.4f\n", i, (double)state.maxbuf[i]);
            break;
        }
        if (fabsf(state.minbuf[i] - (-3.0f)) > FLOAT_TOL) {
            all_filled = 0;
            printf("FAIL: minbuf[%d] = %.4f\n", i, (double)state.minbuf[i]);
            break;
        }
    }

    g_test_count++;
    if (!all_filled) {
        g_fail_count++;
    }

    dsd_symbol_history_free(&state);
    printf("test_buffer_prefill: passed\n\n");
}

int
main(void) {
    printf("Generic Sync Calibration Module Tests\n");
    printf("======================================\n\n");

    test_history_basic_ops();
    test_history_wraparound();
    test_warm_start_ideal();
    test_warm_start_dc_offset();
    test_center_only_warm_start();
    test_center_only_large_bias();
    test_warm_start_insufficient_history();
    test_warm_start_degenerate();
    test_warm_start_various_sync_lengths();
    test_null_handling();
    test_buffer_prefill();

    printf("======================================\n");
    printf("Tests: %d, Failures: %d\n", g_test_count, g_fail_count);

    if (g_fail_count > 0) {
        printf("FAILED\n");
        return 1;
    }

    printf("PASSED: All generic sync calibration tests passed\n");
    return 0;
}
