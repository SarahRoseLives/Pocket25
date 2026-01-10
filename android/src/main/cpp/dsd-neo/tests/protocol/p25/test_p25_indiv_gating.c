// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Verify on_indiv_grant gating for data/private/enc policies. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>

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
    st.p25_cc_freq = 851000000;
    // FDMA IDEN
    int id = 1;
    st.p25_chan_iden = id;
    st.p25_chan_type[id] = 1;
    st.p25_chan_tdma[id] = 0;
    st.p25_base_freq[id] = 851000000 / 5;
    st.p25_chan_spac[id] = 100;
    st.p25_iden_trust[id] = 2;
    int ch = (id << 12) | 0x000A;

    // Case A: private tuning disabled → block
    opts.trunk_tune_private_calls = 0;
    unsigned before = st.p25_sm_tune_count;
    p25_sm_on_indiv_grant(&opts, &st, ch, /*svc*/ 0x00, /*dst*/ 1001, /*src*/ 1002);
    rc |= expect_true("private off", st.p25_sm_tune_count == before);

    // Case B: private on but data disabled → block when 0x10 set
    opts.trunk_tune_private_calls = 1;
    opts.trunk_tune_data_calls = 0;
    before = st.p25_sm_tune_count;
    p25_sm_on_indiv_grant(&opts, &st, ch, /*svc*/ 0x10, /*dst*/ 1001, /*src*/ 1002);
    rc |= expect_true("data off", st.p25_sm_tune_count == before);

    // Case C: private on, data on but ENC disabled → block when 0x40 set
    opts.trunk_tune_data_calls = 1;
    opts.trunk_tune_enc_calls = 0;
    before = st.p25_sm_tune_count;
    p25_sm_on_indiv_grant(&opts, &st, ch, /*svc*/ 0x40, /*dst*/ 1001, /*src*/ 1002);
    rc |= expect_true("enc off", st.p25_sm_tune_count == before);

    // Case D: all enabled → tune
    opts.trunk_tune_private_calls = 1;
    opts.trunk_tune_data_calls = 1;
    opts.trunk_tune_enc_calls = 1;
    before = st.p25_sm_tune_count;
    p25_sm_on_indiv_grant(&opts, &st, ch, /*svc*/ 0x40, /*dst*/ 1001, /*src*/ 1002);
    rc |= expect_true("all on tunes", st.p25_sm_tune_count == before + 1);

    return rc;
}
