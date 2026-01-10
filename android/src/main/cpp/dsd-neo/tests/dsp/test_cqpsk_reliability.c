// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Unit tests for CQPSK angle-based reliability metric.
 *
 * This test file contains an inline copy of the CQPSK reliability algorithm
 * from dmr_compute_reliability() to validate the metric independently.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * CQPSK reliability calculation - matches the CQPSK branch of
 * dmr_compute_reliability() in src/core/frames/dsd_dibit.c
 *
 * The symbol value is the output of qpsk_differential_demod():
 *   sym = atan2(Q, I) * (4/pi)
 *
 * Ideal levels: +1, +3, -1, -3
 * Decision boundaries: 0, +2, -2
 */
static uint8_t
dsd_test_compute_cqpsk_reliability(float sym) {
    /* Determine which ideal level this symbol is closest to */
    float ideal;
    if (sym >= 2.0f) {
        ideal = 3.0f;
    } else if (sym >= 0.0f) {
        ideal = 1.0f;
    } else if (sym >= -2.0f) {
        ideal = -1.0f;
    } else {
        ideal = -3.0f;
    }

    /* Compute error as distance from ideal (max 1.0 at boundary) */
    float error = fabsf(sym - ideal);
    if (error > 1.0f) {
        error = 1.0f;
    }

    /* Map error to reliability: 0 error -> 255, 1.0 error -> 0 */
    int rel = (int)((1.0f - error) * 255.0f + 0.5f);
    if (rel < 0) {
        rel = 0;
    }
    if (rel > 255) {
        rel = 255;
    }

    return (uint8_t)rel;
}

/* Test case structure */
struct test_case {
    float sym;            /* Input symbol value */
    uint8_t expected_min; /* Minimum acceptable reliability */
    uint8_t expected_max; /* Maximum acceptable reliability */
    const char* desc;     /* Test description */
};

int
main(void) {
    int failures = 0;

    /* Test cases: symbol value -> expected reliability range */
    struct test_case cases[] = {
        /* Perfect symbols at ideal levels */
        {1.0f, 250, 255, "Perfect +1 symbol"},
        {3.0f, 250, 255, "Perfect +3 symbol"},
        {-1.0f, 250, 255, "Perfect -1 symbol"},
        {-3.0f, 250, 255, "Perfect -3 symbol"},

        /* Symbols with small error (high reliability) */
        {1.1f, 220, 240, "+1 symbol with 0.1 error"},
        {0.9f, 220, 240, "+1 symbol with -0.1 error"},
        {2.9f, 220, 240, "+3 symbol with -0.1 error"},
        {3.1f, 220, 240, "+3 symbol with +0.1 error"},

        /* Symbols near decision boundary (low reliability) */
        {1.9f, 10, 40, "+1 near +2 boundary"},
        {2.1f, 10, 40, "+3 near +2 boundary"},
        {0.1f, 10, 40, "+1 near 0 boundary"},
        {-0.1f, 10, 40, "-1 near 0 boundary"},

        /* Symbols at decision boundaries (minimum reliability) */
        {2.0f, 0, 5, "At +2 boundary"},
        {0.0f, 0, 5, "At 0 boundary"},
        {-2.0f, 0, 5, "At -2 boundary"},

        /* Extreme/clipped symbols */
        {4.0f, 0, 5, "Clipped +3 (sym=4)"},
        {-4.0f, 0, 5, "Clipped -3 (sym=-4)"},
    };

    int n_cases = sizeof(cases) / sizeof(cases[0]);

    printf("CQPSK Reliability Unit Tests\n");
    printf("=============================\n\n");

    for (int i = 0; i < n_cases; i++) {
        uint8_t rel = dsd_test_compute_cqpsk_reliability(cases[i].sym);
        int pass = (rel >= cases[i].expected_min && rel <= cases[i].expected_max);

        printf("Test %2d: %-30s sym=%+5.2f -> rel=%3u [%3u,%3u] %s\n", i + 1, cases[i].desc, cases[i].sym, rel,
               cases[i].expected_min, cases[i].expected_max, pass ? "PASS" : "FAIL");

        if (!pass) {
            failures++;
        }
    }

    printf("\n%d/%d tests passed\n", n_cases - failures, n_cases);
    return failures > 0 ? 1 : 0;
}
