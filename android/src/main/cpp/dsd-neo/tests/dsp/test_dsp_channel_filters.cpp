// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Unit tests for input channel filters in dsd_filters.c: DC preservation. */

#include <stdint.h>
#include <stdio.h>

extern "C" void init_rrc_filter_memory(void);
extern "C" float dmr_filter(float sample, int sps);
extern "C" float nxdn_filter(float sample, int sps);
extern "C" float dpmr_filter(float sample, int sps);
extern "C" float m17_filter(float sample, int sps);

static int
approx_eq(float a, float b, float tol) {
    float d = a - b;
    if (d < 0) {
        d = -d;
    }
    return d <= tol;
}

static int
dc_pass_check(float (*f)(float, int), float dc, int sps, int warm, float tol) {
    float y = 0.0f;
    for (int i = 0; i < warm; i++) {
        y = f(dc, sps);
    }
    return approx_eq(y, dc, tol);
}

int
main(void) {
    init_rrc_filter_memory();
    const float dc = 0.1f;
    const int warm = 512; // exceed any filter length for steady-state
    const int sps_dmr = 10;
    const int sps_nxdn = 20;
    const int sps_dpmr = 20;
    const int sps_m17 = 10;
    // Allow small rounding tolerance
    if (!dc_pass_check(&dmr_filter, dc, sps_dmr, warm, 1e-4f)) {
        fprintf(stderr, "DMR DC fail\n");
        return 1;
    }
    if (!dc_pass_check(&nxdn_filter, dc, sps_nxdn, warm, 1e-4f)) {
        fprintf(stderr, "NXDN DC fail\n");
        return 1;
    }
    if (!dc_pass_check(&dpmr_filter, dc, sps_dpmr, warm, 1e-4f)) {
        fprintf(stderr, "DPMR DC fail\n");
        return 1;
    }
    if (!dc_pass_check(&m17_filter, dc, sps_m17, warm, 1e-4f)) {
        fprintf(stderr, "M17 DC fail\n");
        return 1;
    }
    return 0;
}
