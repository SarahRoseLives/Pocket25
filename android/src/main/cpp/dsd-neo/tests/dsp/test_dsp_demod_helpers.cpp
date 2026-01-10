// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Unit tests: demod pipeline helpers (low_pass_simple, mean_power). */

#include <dsd-neo/dsp/demod_pipeline.h>
#include <math.h>

#include <stdio.h>

int
main(void) {
    // low_pass_simple: step=2 should average adjacent pairs (normalized by step)
    {
        float x[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        int out_len = low_pass_simple(x, 8, 2);
        if (out_len != 4) {
            fprintf(stderr, "low_pass_simple: out_len=%d want 4\n", out_len);
            return 1;
        }
        // Float pipeline normalizes: (1+2)/2=1.5, (3+4)/2=3.5, (5+6)/2=5.5, (7+8)/2=7.5
        if (!(x[0] == 1.5f && x[1] == 3.5f && x[2] == 5.5f && x[3] == 7.5f)) {
            fprintf(stderr, "low_pass_simple: values mismatch %.1f %.1f %.1f %.1f\n", x[0], x[1], x[2], x[3]);
            return 1;
        }
    }

    // mean_power: DC-corrected mean
    {
        float a[4] = {1, 1, 1, 1};
        float mp = mean_power(a, 4, 1);
        if (fabsf(mp) > 1e-3f) {
            fprintf(stderr, "mean_power: DC vector expected 0 got %.6f\n", mp);
            return 1;
        }
        float b[4] = {1, -1, 1, -1};
        mp = mean_power(b, 4, 1);
        if (fabsf(mp - 1.0f) > 1e-3f) {
            fprintf(stderr, "mean_power: alt signs expected 1 got %.6f\n", mp);
            return 1;
        }
    }

    return 0;
}
