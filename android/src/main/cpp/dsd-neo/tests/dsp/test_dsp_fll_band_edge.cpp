// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Regression test for OP25/GNU Radio-compatible band-edge FLL filter design. */

#include <dsd-neo/dsp/costas.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int
closef(float a, float b, float tol) {
    float d = a - b;
    if (d < 0.0f) {
        d = -d;
    }
    return (d <= tol) ? 1 : 0;
}

static int
check_upper_taps(const dsd_fll_band_edge_state_t* f, const float* exp_r, const float* exp_i, int n, float tol) {
    for (int i = 0; i < n; i++) {
        float ur = f->taps_upper_r[i];
        float ui = f->taps_upper_i[i];
        if (!closef(ur, exp_r[i], tol) || !closef(ui, exp_i[i], tol)) {
            fprintf(stderr, "BE-FLL taps: mismatch at i=%d got=(%.8f,%.8f) exp=(%.8f,%.8f)\n", i, ur, ui, exp_r[i],
                    exp_i[i]);
            return 0;
        }
        /* Lower band-edge is complex conjugate of upper band-edge. */
        float lr = f->taps_lower_r[i];
        float li = f->taps_lower_i[i];
        if (!closef(lr, ur, tol) || !closef(li, -ui, tol)) {
            fprintf(stderr, "BE-FLL taps: lower != conj(upper) at i=%d lower=(%.8f,%.8f) upper=(%.8f,%.8f)\n", i, lr,
                    li, ur, ui);
            return 0;
        }
    }
    return 1;
}

int
main(void) {
    const float tol = 1e-5f;

    /* Expected values taken from GNU Radio digital.fll_band_edge_cc::print_taps()
     * for rolloff=0.2 and filter_size=2*sps+1. */
    {
        dsd_fll_band_edge_state_t f;
        memset(&f, 0, sizeof(f));
        dsd_fll_band_edge_init(&f, 5);
        if (!(f.initialized && f.sps == 5 && f.n_taps == 11)) {
            fprintf(stderr, "BE-FLL init: unexpected state sps=%d n_taps=%d initialized=%d\n", f.sps, f.n_taps,
                    f.initialized);
            return 1;
        }

        const float exp_r[11] = {-5.5667e-02f, -7.2177e-02f, -4.8399e-02f, +4.9139e-03f, +5.8086e-02f, +8.0161e-02f,
                                 +5.8086e-02f, +4.9139e-03f, -4.8399e-02f, -7.2177e-02f, -5.5667e-02f};
        const float exp_i[11] = {-4.0445e-02f, +9.1181e-03f, +5.8504e-02f, +7.8105e-02f, +5.4546e-02f, +0.0000e+00f,
                                 -5.4546e-02f, -7.8105e-02f, -5.8504e-02f, -9.1181e-03f, +4.0445e-02f};
        if (!check_upper_taps(&f, exp_r, exp_i, 11, tol)) {
            return 1;
        }
    }

    {
        dsd_fll_band_edge_state_t f;
        memset(&f, 0, sizeof(f));
        dsd_fll_band_edge_init(&f, 4);
        if (!(f.initialized && f.sps == 4 && f.n_taps == 9)) {
            fprintf(stderr, "BE-FLL init: unexpected state sps=%d n_taps=%d initialized=%d\n", f.sps, f.n_taps,
                    f.initialized);
            return 1;
        }

        const float exp_r[9] = {-6.8359e-02f, -8.5981e-02f, -2.9297e-02f, +5.7321e-02f, +9.8437e-02f,
                                +5.7321e-02f, -2.9297e-02f, -8.5981e-02f, -6.8359e-02f};
        const float exp_i[9] = {-4.9666e-02f, +2.7937e-02f, +9.0166e-02f, +7.8895e-02f, +0.0000e+00f,
                                -7.8895e-02f, -9.0166e-02f, -2.7937e-02f, +4.9666e-02f};
        if (!check_upper_taps(&f, exp_r, exp_i, 9, tol)) {
            return 1;
        }
    }

    return 0;
}
