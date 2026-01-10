// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Unit test: FM envelope AGC block moves RMS toward target (no limiter engaged). */

#include <cmath>
#include <cstdlib>
#include <dsd-neo/dsp/demod_pipeline.h>
#include <dsd-neo/dsp/demod_state.h>
#include <stdio.h>
#include <string.h>

static double
rms_mag(const float* iq, int pairs) {
    double acc = 0.0;
    for (int n = 0; n < pairs; n++) {
        double I = iq[(size_t)(2 * n) + 0];
        double Q = iq[(size_t)(2 * n) + 1];
        acc += I * I + Q * Q;
    }
    return (pairs > 0) ? sqrt(acc / pairs) : 0.0;
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
    // Build a block with low RMS (~0.14) compared to target 0.30
    for (int n = 0; n < pairs; n++) {
        double ang = (2.0 * 3.14159265358979323846 * n) / 37.0; // avoid exact periodicity
        float I = (float)(0.10 * cos(ang));
        float Q = (float)(0.10 * sin(ang));
        in[(size_t)(2 * n) + 0] = I;
        in[(size_t)(2 * n) + 1] = Q;
    }
    s->lowpassed = in;
    s->lp_len = pairs * 2;
    s->mode_demod = &raw_demod; // copy lowpassed -> result
    s->iq_dc_block_enable = 0;
    s->fm_agc_enable = 1;
    s->fm_agc_target_rms = 0.30f;
    s->fm_agc_min_rms = 0.05f;
    s->fm_agc_gain = 1.0f;    // start at 1.0
    s->fm_limiter_enable = 0; // keep limiter off for this test
    s->iqbal_enable = 0;
    s->fll_enabled = 0;
    s->ted_enabled = 0;
    s->squelch_gate_open = 1;
    s->squelch_env = 1.0f;
    s->squelch_env_attack = 0.125f;
    s->squelch_env_release = 0.03125f;

    double pre = rms_mag(in, pairs);
    // Run multiple blocks with same input to allow smoothed gain to converge
    for (int it = 0; it < 8; it++) {
        // refresh input buffer (full_demod modifies in-place)
        for (int n = 0; n < pairs; n++) {
            double ang = (2.0 * 3.14159265358979323846 * n) / 37.0;
            in[(size_t)(2 * n) + 0] = (float)(0.10 * cos(ang));
            in[(size_t)(2 * n) + 1] = (float)(0.10 * sin(ang));
        }
        s->lowpassed = in;
        s->lp_len = pairs * 2;
        full_demod(s);
    }
    double post = rms_mag(s->result, s->result_len / 2);

    if (!(pre > 0.10 && pre < 0.20)) {
        fprintf(stderr, "AGC: unexpected pre-RMS %.4f\n", pre);
        free(s);
        return 1;
    }
    // Expect post-RMS to be close to target after several iterations
    if (!(post > 0.22 && post < 0.38)) {
        fprintf(stderr, "AGC: post-RMS %.4f not near target 0.30 after iterations\n", post);
        free(s);
        return 1;
    }

    free(s);
    return 0;
}
