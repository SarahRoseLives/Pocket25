// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Unit tests for remaining demod helpers: deemph_filter, low_pass_real, and dsd_fm_demod plumbing. */

#include <cmath>
#include <cstdlib>
#include <dsd-neo/dsp/demod_pipeline.h>
#include <dsd-neo/dsp/demod_state.h>
#include <stdio.h>
#include <string.h>

static int
approx_eq(float a, float b, float tol) {
    return std::fabs(a - b) <= tol;
}

static int
monotonic_nondecreasing(const float* x, int n) {
    for (int i = 1; i < n; i++) {
        if (x[i] < x[i - 1]) {
            return 0;
        }
    }
    return 1;
}

int
main(void) {
    demod_state* s = (demod_state*)malloc(sizeof(demod_state));
    if (!s) {
        return 1;
    }
    memset(s, 0, sizeof(*s));

    // deemph_filter: step response
    {
        const int N = 64;
        s->result_len = N;
        for (int i = 0; i < N; i++) {
            s->result[i] = 1.0f;
        }
        s->deemph_a = 0.25f;
        s->deemph_avg = 0.0f;
        deemph_filter(s);
        if (!monotonic_nondecreasing(s->result, N)) {
            fprintf(stderr, "deemph_filter: non-monotonic step response\n");
            free(s);
            return 1;
        }
        if (!approx_eq(s->result[N - 1], 1.0f, 1e-4f)) {
            fprintf(stderr, "deemph_filter: final=%f not near 1.0\n", s->result[N - 1]);
            free(s);
            return 1;
        }
    }

    // low_pass_real: average 2:1 from 48k to 24k on constant signal
    {
        const int N = 32;
        s->result_len = N;
        for (int i = 0; i < N; i++) {
            s->result[i] = 0.5f;
        }
        s->rate_in = 48000;
        s->rate_out2 = 24000;
        s->now_lpr = 0.0f;
        s->prev_lpr_index = 0;
        low_pass_real(s);
        if (s->result_len != N / 2) {
            fprintf(stderr, "low_pass_real: result_len=%d want %d\n", s->result_len, N / 2);
            free(s);
            return 1;
        }
        for (int i = 0; i < s->result_len; i++) {
            if (!approx_eq(s->result[i], 0.5f, 1e-4f)) {
                fprintf(stderr, "low_pass_real: out[%d]=%f not ~0.5\n", i, s->result[i]);
                free(s);
                return 1;
            }
        }
    }

    // dsd_fm_demod: differential phase + FLL offset
    {
        /* Three complex samples rotating +90 deg each step. */
        float iq[6] = {0.5f, 0.0f, 0.0f, 0.5f, -0.5f, 0.0f};
        s->lowpassed = iq;
        s->lp_len = 6; // 3 complex samples
        s->fll_enabled = 1;
        s->fll_freq = 0.003f; // small FLL offset in rad/sample (native float)
        s->pre_r = 0.0f;
        s->pre_j = 0.0f;
        dsd_fm_demod(s);
        if (s->result_len != 3) {
            fprintf(stderr, "dsd_fm_demod: result_len=%d want 3\n", s->result_len);
            free(s);
            return 1;
        }
        /* Output is differential phase in radians + 0.5*fll_freq offset.
         * First sample seeds history (~0), then +90 deg deltas (~π/2 ≈ 1.571).
         * With fll_freq=0.003, offset contribution is 0.0015 rad per sample. */
        float pi_2 = 1.5707963f;
        float fll_offset = 0.5f * 0.003f;
        if (fabsf(s->result[0] - fll_offset) > 0.01f) {
            fprintf(stderr, "dsd_fm_demod: result[0]=%f want ~%f (fll offset)\n", s->result[0], fll_offset);
            free(s);
            return 1;
        }
        for (int i = 1; i < s->result_len; i++) {
            float expect = pi_2 + fll_offset;
            if (fabsf(s->result[i] - expect) > 0.01f) {
                fprintf(stderr, "dsd_fm_demod: result[%d]=%f want ~%f\n", i, s->result[i], expect);
                free(s);
                return 1;
            }
        }
    }

    free(s);
    return 0;
}
