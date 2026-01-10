// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Verify that the P25P1 heuristics circular history behaves as a proper ring:
 * - count saturates at HEURISTICS_SIZE
 * - sum tracks a sliding window over the most recent HEURISTICS_SIZE values
 * - index advances modulo HEURISTICS_SIZE
 */

#include <math.h>
#include <stdio.h>

#include <dsd-neo/dsp/p25p1_heuristics.h>

static int
expect_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

static int
expect_float_close(const char* tag, float got, float want, float eps) {
    float diff = fabsf(got - want);
    if (diff > eps) {
        fprintf(stderr, "%s: got %.6f want %.6f (diff=%.6f)\n", tag, got, want, diff);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;

    P25Heuristics h;
    initialize_p25_heuristics(&h);

    /* Drive a single SymbolHeuristics bucket (prev=0, dibit=0) past capacity. */
    enum { N = HEURISTICS_SIZE + 6 };

    AnalogSignal sig[N];
    for (int i = 0; i < N; i++) {
        sig[i].value = i; /* distinct, monotonically increasing samples */
        sig[i].dibit = 0;
        sig[i].corrected_dibit = 0;
        sig[i].sequence_broken = (i == 0) ? 1 : 0; /* first element skipped when using previous dibit */
    }

    /* rf_mod=0 => C4FM, enables USE_PREVIOUS_DIBIT path */
    int rf_mod = 0;
    contribute_to_heuristics(rf_mod, &h, sig, N);

    SymbolHeuristics* sh = &h.symbols[0][0];

    int updates = N - 1; /* first AnalogSignal skipped due to sequence_broken */
    int expected_count = (updates < HEURISTICS_SIZE) ? updates : HEURISTICS_SIZE;
    rc |= expect_int("count", sh->count, expected_count);

    int expected_index = updates % HEURISTICS_SIZE;
    rc |= expect_int("index", sh->index, expected_index);

    /* Verify that sum tracks a sliding window over the most recent HEURISTICS_SIZE values. */
    if (updates <= HEURISTICS_SIZE) {
        /* No eviction yet: sum of values 1..updates */
        float want_sum = 0.0f;
        for (int v = 1; v <= updates; v++) {
            want_sum += (float)v;
        }
        rc |= expect_float_close("sum (no wrap)", sh->sum, want_sum, 1e-3f);
    } else {
        /* Eviction path: last HEURISTICS_SIZE values, i.e., start..updates */
        int start = updates - HEURISTICS_SIZE + 1;
        float want_sum = 0.0f;
        for (int v = start; v <= updates; v++) {
            want_sum += (float)v;
        }
        rc |= expect_float_close("sum (sliding window)", sh->sum, want_sum, 1e-2f);
    }

    return rc;
}
