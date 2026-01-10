// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Unit test: complex IQ DC block (reduces DC bias on I and Q). */

#include <cstdlib>
#include <dsd-neo/dsp/demod_pipeline.h>
#include <dsd-neo/dsp/demod_state.h>
#include <stdio.h>
#include <string.h>

static double
mean_of(const float* x, int n, int step) {
    double acc = 0.0;
    double cnt = 0.0;
    for (int i = 0; i + (step - 1) < n; i += step) {
        acc += x[i];
        cnt += 1.0;
    }
    return (cnt > 0.0) ? acc / cnt : 0.0;
}

int
main(void) {
    demod_state* s = (demod_state*)malloc(sizeof(demod_state));
    if (!s) {
        return 1;
    }
    memset(s, 0, sizeof(*s));

    const int pairs = 256;
    static float in[(size_t)pairs * 2];
    for (int k = 0; k < pairs; k++) {
        // Add DC offsets with small noise
        in[(size_t)(2 * k) + 0] = 0.10f + 0.001f * (float)(k % 7);
        in[(size_t)(2 * k) + 1] = -0.05f - 0.001f * (float)(k % 5);
    }
    s->lowpassed = in;
    s->lp_len = pairs * 2;
    s->mode_demod = &raw_demod; // copy lowpassed -> result
    s->iq_dc_block_enable = 1;
    s->iq_dc_shift = 11; // smoothing retained; pre-seed averages to converge in one block
    // Pre-seed running DC averages to the block mean to emulate warmed state
    double pre_I = mean_of(in, s->lp_len, 2);
    double pre_Q = mean_of(in + 1, s->lp_len - 1, 2);
    s->iq_dc_avg_r = (float)pre_I;
    s->iq_dc_avg_i = (float)pre_Q;
    s->fm_agc_enable = 0;
    s->iqbal_enable = 0;
    s->fll_enabled = 0;
    s->ted_enabled = 0;

    full_demod(s);

    double post_I = mean_of(s->result, s->result_len, 2);
    double post_Q = mean_of(s->result + 1, s->result_len - 1, 2);

    if (!(pre_I > 0.09 && pre_Q < -0.04)) {
        fprintf(stderr, "IQ DC pre means unexpected: I=%.2f Q=%.2f\n", pre_I, pre_Q);
        free(s);
        return 1;
    }
    if (!(post_I > -0.005 && post_I < 0.005 && post_Q > -0.005 && post_Q < 0.005)) {
        fprintf(stderr, "IQ DC block insufficient: post I=%.2f Q=%.2f\n", post_I, post_Q);
        free(s);
        return 1;
    }

    free(s);
    return 0;
}
