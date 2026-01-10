// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 neighbor update spam test: stress p25_sm_on_neighbor_update with
 * many updates and assert CC candidate list remains bounded and
 * iteration via p25_sm_next_cc_candidate stays consistent.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/timing.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>

#include "test_support.h"

#define setenv dsd_test_setenv

// Stubs for radio control
bool
SetFreq(int sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return true;
}

bool
SetModulation(int sockfd, int bandwidth) {
    (void)sockfd;
    (void)bandwidth;
    return true;
}
struct RtlSdrContext* g_rtl_ctx = 0;

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

static int
expect_true(const char* tag, int cond) {
    if (!cond) {
        fprintf(stderr, "%s: failed\n", tag);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    static dsd_opts opts;
    static dsd_state st;
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);
    opts.p25_trunk = 1;
    // Set system identity so SM can label candidates; disable on-disk cache via env
    st.p2_wacn = 0xABCDE;
    st.p2_sysid = 0x123;
    setenv("DSD_NEO_CC_CACHE", "0", 1);
    dsd_neo_config_init(NULL);

    // Timing start for rough performance guard
    double elapsed_ms = 0.0;
    uint64_t t0_ns = dsd_time_monotonic_ns();

    // Spam with pseudo-random neighbors around 851 MHz
    // Ensure we include some duplicates and the current CC at times.
    const int rounds = 2000;
    for (int i = 0; i < rounds; i++) {
        long f[4];
        int n = (i % 4) + 1;
        for (int k = 0; k < n; k++) {
            long step = (long)((i * 13 + k * 7) % 80) * 12500; // 12.5 kHz steps over ~1 MHz span
            f[k] = 851000000 + step;
            if ((i % 97) == 0 && k == 0) {
                st.p25_cc_freq = f[k]; // sometimes match current CC to test dedup
            }
        }
        p25_sm_on_neighbor_update(&opts, &st, f, n);
        // Count should never exceed 16
        const dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_peek(&st);
        const int count = cc ? cc->count : 0;
        rc |= expect_true("cand<=16", count >= 0 && count <= DSD_TRUNK_CC_CANDIDATES_MAX);
    }

    // Timing end and guard: ensure this remains snappy
    uint64_t t1_ns = dsd_time_monotonic_ns();
    elapsed_ms = (double)(t1_ns - t0_ns) / 1e6;
    // Allow a generous envelope to avoid CI flakiness, but detect regressions
#if DSD_PLATFORM_WIN_NATIVE
    rc |= expect_true("neighbor-spam-fast", elapsed_ms < 1000.0);
#else
    rc |= expect_true("neighbor-spam-fast", elapsed_ms < 200.0);
#endif

    // Next-candidate iteration should cycle through at most count entries and
    // never return 0 or the current CC.
    const dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_peek(&st);
    const int count = cc ? cc->count : 0;
    if (count > 0) {
        int seen_nonzero = 0;
        long last = -1;
        for (int i = 0; i < count * 3; i++) {
            long out = 0;
            int ok = p25_sm_next_cc_candidate(&st, &out);
            rc |= expect_true("next->ok", ok == 1);
            rc |= expect_true("next->nonzero", out != 0);
            rc |= expect_true("next->neq-cc", out != st.p25_cc_freq);
            // do a weak monotonicity sanity: not all calls must differ, but should
            // make progress across multiple calls
            if (last != out) {
                seen_nonzero = 1;
            }
            last = out;
        }
        rc |= expect_true("progress", seen_nonzero);
    }

    return rc;
}
