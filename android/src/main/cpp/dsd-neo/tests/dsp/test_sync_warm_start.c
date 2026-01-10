// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Unit tests for DMR sync pattern threshold initialization (warm start).
 *
 * Tests dmr_init_thresholds_from_sync() which derives initial slicer thresholds
 * from the known +3/-3 structure of DMR sync patterns. This enables fast warmup
 * for first-frame decoding without requiring the standard rolling average
 * accumulation period.
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
check_float(const char* name, float expected, float actual, float tol) {
    g_test_count++;
    if (fabsf(expected - actual) > tol) {
        printf("FAIL: %s: expected %.4f, got %.4f\n", name, (double)expected, (double)actual);
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
 * @brief Test threshold initialization with ideal sync pattern.
 *
 * Uses ideal +3.0/-3.0 symbol values to verify threshold calculation.
 * Expected results:
 *   - max = +3.0, min = -3.0
 *   - center = 0.0
 *   - umid = +1.875 (center + 0.625 * (max - center))
 *   - lmid = -1.875
 */
static void
test_ideal_sync_pattern(void) {
    printf("=== test_ideal_sync_pattern ===\n");

    /* BS_VOICE pattern: 12 x +3.0, 12 x -3.0 */
    float sync_symbols[DMR_SYNC_SYMBOLS] = {+3.0f, -3.0f, +3.0f, +3.0f, +3.0f, +3.0f, -3.0f, -3.0f,
                                            +3.0f, -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, +3.0f, -3.0f,
                                            +3.0f, -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f};

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 128;

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    dmr_init_thresholds_from_sync(&opts, &state, sync_symbols);

    /* Verify thresholds */
    check_float("max", 3.0f, state.max, FLOAT_TOL);
    check_float("min", -3.0f, state.min, FLOAT_TOL);
    check_float("center", 0.0f, state.center, FLOAT_TOL);

    /* Mid thresholds: 62.5% from center toward extremes */
    float expected_umid = 0.0f + (3.0f - 0.0f) * 0.625f;  /* 1.875 */
    float expected_lmid = 0.0f + (-3.0f - 0.0f) * 0.625f; /* -1.875 */
    check_float("umid", expected_umid, state.umid, FLOAT_TOL);
    check_float("lmid", expected_lmid, state.lmid, FLOAT_TOL);

    /* Reference values: 80% of extremes */
    check_float("maxref", 3.0f * 0.80f, state.maxref, FLOAT_TOL);
    check_float("minref", -3.0f * 0.80f, state.minref, FLOAT_TOL);

    printf("test_ideal_sync_pattern: passed\n\n");
}

/**
 * @brief Test threshold initialization with DC-offset sync pattern.
 *
 * Simulates a signal with +0.5 DC offset. The sync pattern should still
 * yield correct relative thresholds.
 */
static void
test_dc_offset_pattern(void) {
    printf("=== test_dc_offset_pattern ===\n");

    /* DC offset of +0.5: +3.5/-2.5 instead of +3.0/-3.0 */
    float dc_offset = 0.5f;
    float sync_symbols[DMR_SYNC_SYMBOLS] = {+3.0f + dc_offset, -3.0f + dc_offset, +3.0f + dc_offset, +3.0f + dc_offset,
                                            +3.0f + dc_offset, +3.0f + dc_offset, -3.0f + dc_offset, -3.0f + dc_offset,
                                            +3.0f + dc_offset, -3.0f + dc_offset, +3.0f + dc_offset, +3.0f + dc_offset,
                                            -3.0f + dc_offset, +3.0f + dc_offset, +3.0f + dc_offset, -3.0f + dc_offset,
                                            +3.0f + dc_offset, -3.0f + dc_offset, +3.0f + dc_offset, +3.0f + dc_offset,
                                            -3.0f + dc_offset, +3.0f + dc_offset, -3.0f + dc_offset, +3.0f + dc_offset};

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 128;

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    dmr_init_thresholds_from_sync(&opts, &state, sync_symbols);

    /* Verify thresholds reflect offset */
    check_float("max", 3.5f, state.max, FLOAT_TOL);
    check_float("min", -2.5f, state.min, FLOAT_TOL);
    check_float("center", 0.5f, state.center, FLOAT_TOL);

    printf("test_dc_offset_pattern: passed\n\n");
}

/**
 * @brief Test threshold initialization with amplitude-scaled sync pattern.
 *
 * Simulates a signal at 80% amplitude. Thresholds should scale accordingly.
 */
static void
test_scaled_amplitude_pattern(void) {
    printf("=== test_scaled_amplitude_pattern ===\n");

    /* 80% amplitude: ±2.4 instead of ±3.0 */
    float scale = 0.8f;
    float sync_symbols[DMR_SYNC_SYMBOLS] = {+3.0f * scale, -3.0f * scale, +3.0f * scale, +3.0f * scale, +3.0f * scale,
                                            +3.0f * scale, -3.0f * scale, -3.0f * scale, +3.0f * scale, -3.0f * scale,
                                            +3.0f * scale, +3.0f * scale, -3.0f * scale, +3.0f * scale, +3.0f * scale,
                                            -3.0f * scale, +3.0f * scale, -3.0f * scale, +3.0f * scale, +3.0f * scale,
                                            -3.0f * scale, +3.0f * scale, -3.0f * scale, +3.0f * scale};

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 128;

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    dmr_init_thresholds_from_sync(&opts, &state, sync_symbols);

    /* Verify thresholds reflect scaling */
    check_float("max", 2.4f, state.max, FLOAT_TOL);
    check_float("min", -2.4f, state.min, FLOAT_TOL);
    check_float("center", 0.0f, state.center, FLOAT_TOL);

    printf("test_scaled_amplitude_pattern: passed\n\n");
}

/**
 * @brief Test null pointer handling.
 */
static void
test_null_handling(void) {
    printf("=== test_null_handling ===\n");

    float sync_symbols[DMR_SYNC_SYMBOLS] = {0};

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    /* Initialize to known values */
    state.max = 999.0f;
    state.min = -999.0f;

    /* NULL state should be no-op */
    dmr_init_thresholds_from_sync(&opts, NULL, sync_symbols);

    /* NULL sync_symbols should be no-op */
    dmr_init_thresholds_from_sync(&opts, &state, NULL);
    check_float("unchanged max", 999.0f, state.max, FLOAT_TOL);
    check_float("unchanged min", -999.0f, state.min, FLOAT_TOL);

    printf("test_null_handling: passed\n\n");
}

/**
 * @brief Test rolling buffer pre-fill.
 *
 * Verifies that maxbuf/minbuf are pre-filled to avoid warmup artifacts.
 */
static void
test_buffer_prefill(void) {
    printf("=== test_buffer_prefill ===\n");

    float sync_symbols[DMR_SYNC_SYMBOLS] = {+3.0f, -3.0f, +3.0f, +3.0f, +3.0f, +3.0f, -3.0f, -3.0f,
                                            +3.0f, -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, +3.0f, -3.0f,
                                            +3.0f, -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f};

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 64; /* Smaller than 1024 for test */

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    dmr_init_thresholds_from_sync(&opts, &state, sync_symbols);

    /* Check buffer is pre-filled */
    int buffers_filled = 1;
    for (int i = 0; i < opts.msize && i < 1024; i++) {
        if (fabsf(state.maxbuf[i] - 3.0f) > FLOAT_TOL) {
            buffers_filled = 0;
            printf("FAIL: maxbuf[%d] = %.4f, expected 3.0\n", i, (double)state.maxbuf[i]);
            break;
        }
        if (fabsf(state.minbuf[i] - (-3.0f)) > FLOAT_TOL) {
            buffers_filled = 0;
            printf("FAIL: minbuf[%d] = %.4f, expected -3.0\n", i, (double)state.minbuf[i]);
            break;
        }
    }

    g_test_count++;
    if (!buffers_filled) {
        g_fail_count++;
    }

    printf("test_buffer_prefill: passed\n\n");
}

/**
 * @brief Test with noisy sync pattern (realistic scenario).
 *
 * Adds Gaussian-like noise to sync symbols to simulate real-world conditions.
 */
static void
test_noisy_pattern(void) {
    printf("=== test_noisy_pattern ===\n");

    /* Add small noise to each symbol (±0.3 max) */
    float noise[] = {0.1f, -0.2f, 0.15f, -0.05f, 0.2f,  -0.1f,  0.05f, -0.15f, 0.3f, -0.25f, 0.1f,  -0.3f,
                     0.2f, -0.2f, 0.15f, -0.1f,  0.05f, -0.05f, 0.1f,  -0.1f,  0.2f, -0.2f,  0.25f, -0.25f};

    float sync_symbols[DMR_SYNC_SYMBOLS];
    /* BS_VOICE pattern */
    float ideal[] = {+3.0f, -3.0f, +3.0f, +3.0f, +3.0f, +3.0f, -3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f,
                     -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f, +3.0f, -3.0f, +3.0f, -3.0f, +3.0f};

    for (int i = 0; i < DMR_SYNC_SYMBOLS; i++) {
        sync_symbols[i] = ideal[i] + noise[i];
    }

    static struct dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.msize = 128;

    static struct dsd_state state;
    memset(&state, 0, sizeof(state));

    dmr_init_thresholds_from_sync(&opts, &state, sync_symbols);

    /* With noise, thresholds should still be close to ideal (within noise bounds) */
    check_float_range("max (noisy)", 2.7f, 3.3f, state.max);
    check_float_range("min (noisy)", -3.3f, -2.7f, state.min);
    check_float_range("center (noisy)", -0.3f, 0.3f, state.center);

    printf("test_noisy_pattern: passed\n\n");
}

int
main(void) {
    printf("DMR Sync Warm Start Threshold Tests\n");
    printf("====================================\n\n");

    test_ideal_sync_pattern();
    test_dc_offset_pattern();
    test_scaled_amplitude_pattern();
    test_null_handling();
    test_buffer_prefill();
    test_noisy_pattern();

    printf("====================================\n");
    printf("Tests: %d, Failures: %d\n", g_test_count, g_fail_count);

    if (g_fail_count > 0) {
        printf("FAILED\n");
        return 1;
    }

    printf("PASSED: All sync warm start tests passed\n");
    return 0;
}
