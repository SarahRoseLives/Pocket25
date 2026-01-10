// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Verify TG Hold gating in trunk SM: only held TG should tune while hold is set. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>

// Stubs for external IO
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
struct RtlSdrContext* g_rtl_ctx = 0;

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

static int
expect_true(const char* tag, int cond) {
    if (!cond) {
        fprintf(stderr, "%s: expected true\n", tag);
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
    opts.trunk_tune_group_calls = 1;
    st.p25_cc_freq = 851000000;

    // Simple FDMA IDEN
    int id = 1;
    st.p25_chan_iden = id;
    st.p25_chan_type[id] = 1;
    st.p25_chan_tdma[id] = 0;
    st.p25_base_freq[id] = 851000000 / 5;
    st.p25_chan_spac[id] = 100;
    st.p25_iden_trust[id] = 2;
    int ch = (id << 12) | 0x000A;

    // Hold TG 1234
    st.tg_hold = 1234;

    // Grant non-held TG -> should be blocked
    unsigned before = st.p25_sm_tune_count;
    opts.p25_is_tuned = 0;
    p25_sm_on_group_grant(&opts, &st, ch, 0 /*svc*/, /*tg*/ 4321, /*src*/ 999);
    rc |= expect_true("non-held blocked", st.p25_sm_tune_count == before && opts.p25_is_tuned == 0);

    // Grant held TG -> should tune
    before = st.p25_sm_tune_count;
    opts.p25_is_tuned = 0;
    p25_sm_on_group_grant(&opts, &st, ch, 0 /*svc*/, /*tg*/ 1234, /*src*/ 888);
    rc |= expect_true("held allowed", st.p25_sm_tune_count == before + 1);

    return rc;
}
