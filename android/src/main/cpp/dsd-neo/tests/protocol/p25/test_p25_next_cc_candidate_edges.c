// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 next CC candidate edge cases.
 * - Returns 0 when empty list
 * - Returns 0 when only current CC or zeros present
 */

#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>

// Stubs
bool
SetFreq(int sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return false;
}

bool
SetModulation(int sockfd, int bandwidth) {
    (void)sockfd;
    (void)bandwidth;
    return false;
}

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}
struct RtlSdrContext* g_rtl_ctx = 0;

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

static int
expect_eq_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
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

    long out = -1;
    // Empty list
    rc |= expect_eq_int("empty", p25_sm_next_cc_candidate(&st, &out), 0);

    // Only current CC and zeros
    st.p25_cc_freq = 851000000;
    long neigh[4] = {st.p25_cc_freq, 0, 0, 0};
    p25_sm_on_neighbor_update(&opts, &st, neigh, 4);
    rc |= expect_eq_int("cc-only", p25_sm_next_cc_candidate(&st, &out), 0);
    return rc;
}
