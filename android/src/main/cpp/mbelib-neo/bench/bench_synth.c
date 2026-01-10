// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Micro-benchmark for synthesis hot paths.
 *
 * Measures `mbe_synthesizeSpeechf` with a deterministic parameter stream
 * and reports elapsed CPU time across several repeated runs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbelib-neo/mbelib.h"

/**
 * @brief Return elapsed CPU seconds using `clock()`.
 */
static double
secs(void) {
    clock_t c = clock();
    return (double)c / (double)CLOCKS_PER_SEC;
}

/**
 * @brief Benchmark entry: runs repeated synthesis loops and prints timing.
 */
int
main(void) {
    const int iters = 2000; // frames per run
    const int runs = 10;    // repeats
    float out[160];
    mbe_parms cur, prev, prev_enh;

    mbe_setThreadRngSeed(0x123456u);
    mbe_initMbeParms(&cur, &prev, &prev_enh);

    // Create a moderately voiced/unvoiced mix
    cur.w0 = 0.09378f; // ~8k/ (something reasonable)
    cur.L = 40;        // harmonics
    for (int l = 1; l <= cur.L; ++l) {
        cur.Vl[l] = (l % 3) != 0;       // mix of voiced/unvoiced
        cur.Ml[l] = 0.05f + 0.002f * l; // gentle slope
        cur.log2Ml[l] = 0.0f;
        cur.PHIl[l] = (float)l * 0.1f; // varying phases
        cur.PSIl[l] = (float)l * 0.05f;
    }
    prev = cur; // start with same state

    double total = 0.0, best = 1e9, worst = 0.0;
    for (int r = 0; r < runs; ++r) {
        double t0 = secs();
        for (int i = 0; i < iters; ++i) {
            // Oscillate some parameters to exercise both paths
            cur.w0 = (i & 1) ? 0.09f : 0.11f;
            for (int l = 1; l <= cur.L; ++l) {
                cur.Vl[l] = ((i + l) % 5) ? 1 : 0;
                cur.Ml[l] = 0.04f + 0.003f * (float)((i + l) % 7);
            }
            mbe_synthesizeSpeechf(out, &cur, &prev, 8);
            mbe_moveMbeParms(&cur, &prev);
        }
        double dt = secs() - t0;
        total += dt;
        if (dt < best) {
            best = dt;
        }
        if (dt > worst) {
            worst = dt;
        }
        printf("run %d: %.6f s\n", r + 1, dt);
    }
    printf("avg: %.6f s, best: %.6f s, worst: %.6f s (frames=%d)\n", total / runs, best, worst, iters * runs);
    return 0;
}
