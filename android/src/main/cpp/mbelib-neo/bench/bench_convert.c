// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Micro-benchmark for the float→int16 conversion path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbelib-neo/mbelib.h"

/**
 * @brief Return elapsed CPU seconds using the C clock.
 */
static double
secs(void) {
    clock_t c = clock();
    return (double)c / (double)CLOCKS_PER_SEC;
}

/**
 * @brief Benchmark entry: runs repeated conversions and prints timing.
 */
int
main(int argc, char** argv) {
    int loops = 200000; // convert 200k frames -> ~32M samples
    if (argc > 1) {
        loops = atoi(argv[1]);
    }
    float f[160];
    short s[160];
    for (int i = 0; i < 160; ++i) {
        f[i] = (float)i * 0.01f - 0.8f; // deterministic pattern
    }
    double t0 = secs();
    for (int i = 0; i < loops; ++i) {
        mbe_floattoshort(f, s);
        // mutate one value to avoid over-aggressive dead code elimination
        f[0] += (float)(s[0] & 1) * 1e-6f;
    }
    double dt = secs() - t0;
    printf("convert loops=%d -> %.6f s\n", loops, dt);
    // consume a value
    volatile short sink = s[0];
    (void)sink;
    return 0;
}
