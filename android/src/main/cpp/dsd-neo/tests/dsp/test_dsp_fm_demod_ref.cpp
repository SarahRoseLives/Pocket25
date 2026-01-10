// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Unit test: dsd_fm_demod (phase-diff path) returns constant for constant dphi. */

#include <cmath>
#include <cstdlib>
#include <dsd-neo/dsp/demod_pipeline.h>
#include <dsd-neo/dsp/demod_state.h>
#include <dsd-neo/dsp/math_utils.h>
#include <stdio.h>
#include <string.h>

int
main(void) {
    demod_state* s = (demod_state*)malloc(sizeof(demod_state));
    if (!s) {
        return 1;
    }
    memset(s, 0, sizeof(*s));

    // Build a complex tone that advances by constant phase per sample
    const int N = 256; // complex pairs
    static float iq[(size_t)N * 2];
    double Fs = 48000.0;
    double f_dev = 3000.0; // radians per second mapped to dphi = 2*pi*f/Fs
    double dphi = 2.0 * 3.14159265358979323846 * f_dev / Fs;
    double A = 0.8; // normalized amplitude
    for (int k = 0; k < N; k++) {
        double th = k * dphi;
        iq[(size_t)(2 * k) + 0] = (float)(A * cos(th));
        iq[(size_t)(2 * k) + 1] = (float)(A * sin(th));
    }

    s->lowpassed = iq;
    s->lp_len = N * 2;
    s->fll_enabled = 0;
    s->pre_r = 0;
    s->pre_j = 0;

    dsd_fm_demod(s);

    /* Expected steady-state phase delta based on first two samples.
     * With native float output, the demodulator returns raw radians (not Q14 scaled). */
    float r0 = iq[0], j0 = iq[1], r1 = iq[2], j1 = iq[3];
    double re = (double)r1 * (double)r0 + (double)j1 * (double)j0;
    double im = (double)j1 * (double)r0 - (double)r1 * (double)j0;
    float expect_rad = (float)atan2(im, re);
    if (s->result_len != N) {
        fprintf(stderr, "FM demod ref: result_len=%d want %d\n", s->result_len, N);
        free(s);
        return 1;
    }
    // First sample seeds history; steady-state starts at index 1
    if (fabsf(s->result[0]) > 1e-3f) {
        fprintf(stderr, "FM demod ref: result[0]=%f want 0\n", s->result[0]);
        free(s);
        return 1;
    }
    for (int i = 1; i < s->result_len; i++) {
        float v = s->result[i];
        float d = fabsf(v - expect_rad);
        if (d > 0.01f) { // allow small tolerance for native float output
            fprintf(stderr, "FM demod ref: result[%d]=%f expect~%f\n", i, v, expect_rad);
            free(s);
            return 1;
        }
    }

    free(s);
    return 0;
}
