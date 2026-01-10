// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Focused unit test for Gardner TED timing adjustment - OP25 compatible implementation.
 *
 * The OP25-compatible Gardner outputs at symbol rate (decimated), not at input sample rate.
 * With N input samples and sps samples-per-symbol, we expect approximately N/sps output
 * symbols after the timing recovery loop converges.
 */

#include <dsd-neo/dsp/ted.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Test TED with a specific SPS value.
 *
 * The MMSE 8-tap interpolator requires the delay line to be sized correctly
 * to avoid reading past the end. This is especially critical for low SPS
 * values (2, 3) where the naive formula `2*ceil(omega)` would be too small.
 */
static int
test_ted_sps(int sps) {
    const int num_symbols = 40;       /* more symbols for low SPS */
    const int N0 = num_symbols * sps; /* total complex samples */

    /* Allocate buffers - use larger size to accommodate various SPS values */
    float x[2 * 500]; /* max 500 complex samples */
    float y[2 * 500];

    if (N0 > 500) {
        fprintf(stderr, "TED sps=%d: buffer too small\n", sps);
        return 1;
    }

    /* Generate a simple QPSK-like signal: symbols at ±1, ±j */
    const float symbols[4][2] = {{1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}};
    for (int sym = 0; sym < num_symbols; sym++) {
        int level = sym % 4;
        for (int s = 0; s < sps; s++) {
            int idx = sym * sps + s;
            x[2 * idx + 0] = symbols[level][0];
            x[2 * idx + 1] = symbols[level][1];
        }
    }

    int N = 2 * N0;

    ted_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = 1;
    cfg.force = 1; /* ensure it runs regardless of sps */
    cfg.sps = sps;
    cfg.gain_mu = 0.025f;        /* OP25 default */
    cfg.gain_omega = 0.0000625f; /* OP25 default: 0.1 * 0.025^2 */
    cfg.omega_rel = 0.005f;      /* wider range for low SPS */

    ted_state_t st;
    ted_init_state(&st);

    gardner_timing_adjust(&cfg, &st, x, &N, y);

    /* OP25-compatible Gardner outputs at symbol rate (decimated).
     * Expected: approximately N0/sps = num_symbols output symbols.
     * The actual count may be less due to loop startup, especially for low SPS. */
    int expected_min_symbols = num_symbols / 2; /* allow significant startup overhead */
    int output_symbols = N / 2;
    if (output_symbols < expected_min_symbols) {
        fprintf(stderr, "TED sps=%d: output_symbols=%d expected>=%d\n", sps, output_symbols, expected_min_symbols);
        return 1;
    }

    /* omega should be near the nominal sps value (allow wider tolerance for low SPS) */
    float omega_tol = (sps <= 3) ? 0.5f : 0.1f;
    if (fabsf(st.omega - (float)sps) > omega_tol) {
        fprintf(stderr, "TED sps=%d: omega=%f expected~%d\n", sps, st.omega, sps);
        return 1;
    }

    /* Verify twice_sps was sized correctly for MMSE interpolator.
     * The 8-tap MMSE filter needs: twice_sps >= ceil(omega/2) + NTAPS + 1 = ceil(omega/2) + 9
     * where NTAPS=8 for the interpolator, +1 for safety margin. */
    const int kMmseNtapsPlus1 = 8 + 1; /* MMSE_NTAPS + 1 from ted.cpp */
    int min_twice_sps = (int)ceilf((float)sps / 2.0f) + kMmseNtapsPlus1;
    if (st.twice_sps < min_twice_sps) {
        fprintf(stderr, "TED sps=%d: twice_sps=%d too small (need>=%d for MMSE)\n", sps, st.twice_sps, min_twice_sps);
        return 1;
    }

    fprintf(stderr, "TED sps=%d: output_symbols=%d omega=%.3f twice_sps=%d e_ema=%.4f OK\n", sps, output_symbols,
            st.omega, st.twice_sps, st.e_ema);

    return 0;
}

/**
 * @brief Verify that OP25-style TED reinitializes when SPS changes.
 *
 * The delay-line sizing and omega bounds must be recomputed when config->sps
 * changes at runtime; otherwise the loop will continue running with stale
 * omega_mid/twice_sps from the previous symbol rate.
 */
static int
test_ted_reinit_on_sps_change(void) {
    const int num_symbols = 40;
    const int sps1 = 10;
    const int sps2 = 5;
    const int N0 = num_symbols * sps1; /* enough samples for first run */

    float x[2 * 500];
    float y[2 * 500];

    if (N0 > 500) {
        fprintf(stderr, "TED reinit: buffer too small\n");
        return 1;
    }

    /* Simple constant signal is sufficient to drive initialization. */
    for (int i = 0; i < N0; i++) {
        x[2 * i + 0] = 0.1f;
        x[2 * i + 1] = -0.05f;
    }

    ted_state_t st;
    ted_init_state(&st);

    ted_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = 1;
    cfg.force = 1;
    cfg.gain_mu = 0.025f;
    cfg.omega_rel = 0.002f;

    /* First run at higher SPS (sps1) */
    int N = 2 * N0;
    cfg.sps = sps1;
    gardner_timing_adjust(&cfg, &st, x, &N, y);
    int twice_sps_1 = st.twice_sps;
    float omega_mid_1 = st.omega_mid;

    if (twice_sps_1 <= 0 || omega_mid_1 <= 0.0f) {
        fprintf(stderr, "TED reinit: initial omega/twice_sps not initialized\n");
        return 1;
    }

    /* Second run at lower SPS (sps2) without calling ted_init_state again.
     * Implementation should detect SPS change and reinitialize internally. */
    cfg.sps = sps2;
    N = 2 * N0;
    gardner_timing_adjust(&cfg, &st, x, &N, y);

    if (st.sps != sps2) {
        fprintf(stderr, "TED reinit: state.sps=%d expected %d\n", st.sps, sps2);
        return 1;
    }
    if (fabsf(st.omega_mid - (float)sps2) > 0.25f) {
        fprintf(stderr, "TED reinit: omega_mid=%f expected~%d\n", st.omega_mid, sps2);
        return 1;
    }
    if (st.twice_sps >= twice_sps_1) {
        fprintf(stderr, "TED reinit: twice_sps=%d did not shrink from %d after SPS decrease\n", st.twice_sps,
                twice_sps_1);
        return 1;
    }

    fprintf(stderr, "TED reinit: sps1=%d -> sps2=%d omega_mid=%.3f twice_sps=%d OK\n", sps1, sps2, st.omega_mid,
            st.twice_sps);

    return 0;
}

int
main(void) {
    /* Test standard SPS value (sps=5, typical for P25 at 24kHz) */
    if (test_ted_sps(5) != 0) {
        return 1;
    }

    /* Test low SPS values that stress the MMSE delay line sizing.
     * These would fail with the naive `twice_sps = 2*ceil(omega)` formula
     * because the 8-tap interpolator needs more lookahead than provided. */
    if (test_ted_sps(2) != 0) {
        return 1;
    }

    if (test_ted_sps(3) != 0) {
        return 1;
    }

    /* Test higher SPS value */
    if (test_ted_sps(10) != 0) {
        return 1;
    }

    if (test_ted_reinit_on_sps_change() != 0) {
        return 1;
    }

    return 0;
}
