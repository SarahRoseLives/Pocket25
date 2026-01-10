// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/dsp/demod_state.h>
#include <dsd-neo/dsp/snr_bias.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

static int
nearly_equal(double got, double expected, double tol) {
    double d = got - expected;
    if (d < 0) {
        d = -d;
    }
    return d <= tol;
}

int
main(void) {
    const int rate_out = 48000;
    const int ted_sps = 10;
    const double tol = 1e-6;

    struct test_case {
        int profile;
        double expected_c4fm;
        double expected_evm;
        const char* name;
    };

    const struct test_case cases[] = {
        {DSD_CH_LPF_PROFILE_WIDE, 8.055726150081295, 4.245726150081294, "WIDE"},
        {DSD_CH_LPF_PROFILE_6K25, 4.71542359241223, 0.9054235924122291, "6K25"},
        {DSD_CH_LPF_PROFILE_12K5, 6.321214521186566, 2.5112145211865657, "12K5"},
        {DSD_CH_LPF_PROFILE_PROVOICE, 7.046721192672685, 3.2367211926726838, "PROVOICE"},
        {DSD_CH_LPF_PROFILE_P25_C4FM, 6.399467896306133, 2.5894678963061324, "P25_C4FM"},
        {DSD_CH_LPF_PROFILE_P25_CQPSK, 7.668200260161129, 3.8582002601611283, "P25_CQPSK"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const struct test_case* tc = &cases[i];
        double got_c4fm = dsd_snr_bias_c4fm_db(rate_out, ted_sps, tc->profile);
        if (!nearly_equal(got_c4fm, tc->expected_c4fm, tol)) {
            fprintf(stderr, "SNR bias C4FM mismatch for %s: got %.12f expected %.12f\n", tc->name, got_c4fm,
                    tc->expected_c4fm);
            return 1;
        }
        double got_evm = dsd_snr_bias_evm_db(rate_out, ted_sps, tc->profile);
        if (!nearly_equal(got_evm, tc->expected_evm, tol)) {
            fprintf(stderr, "SNR bias EVM mismatch for %s: got %.12f expected %.12f\n", tc->name, got_evm,
                    tc->expected_evm);
            return 1;
        }
    }

    /* Fallback behavior for invalid inputs should be stable. */
    {
        const double fb_c4fm = dsd_snr_bias_c4fm_db(0, ted_sps, DSD_CH_LPF_PROFILE_WIDE);
        const double fb_evm = dsd_snr_bias_evm_db(rate_out, 0, DSD_CH_LPF_PROFILE_WIDE);
        if (!nearly_equal(fb_c4fm, 7.93, tol)) {
            fprintf(stderr, "SNR bias C4FM fallback mismatch: got %.12f expected %.12f\n", fb_c4fm, 7.93);
            return 1;
        }
        if (!nearly_equal(fb_evm, 2.42, tol)) {
            fprintf(stderr, "SNR bias EVM fallback mismatch: got %.12f expected %.12f\n", fb_evm, 2.42);
            return 1;
        }
    }

    return 0;
}
