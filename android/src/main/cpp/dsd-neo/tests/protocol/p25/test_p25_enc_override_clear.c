// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Verify ENC override via regroup KEY=0: encrypted SVC bits should tune when
 * WGID is within an active SGID that has KEY=0 (clear) policy. */

#include <stdint.h>
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
    // FDMA IDEN
    int id = 1;
    st.p25_chan_iden = id;
    st.p25_chan_type[id] = 1;
    st.p25_chan_tdma[id] = 0;
    st.p25_base_freq[id] = 851000000 / 5;
    st.p25_chan_spac[id] = 100;
    st.p25_iden_trust[id] = 2;
    int ch = (id << 12) | 0x000A;

    // ENC calls disabled by policy
    opts.trunk_tune_enc_calls = 0;

    // Create a regroup SG with KEY=0, WGID includes TG=0x2345
    p25_patch_update(&st, 69, /*is_patch*/ 1, /*active*/ 1);
    p25_patch_add_wgid(&st, 69, 0x2345);
    p25_patch_set_kas(&st, 69, /*key*/ 0, /*alg*/ 0x84, /*ssn*/ 17);

    unsigned before = st.p25_sm_tune_count;
    // svc has ENC bit set (0x40); override should allow tune
    p25_sm_on_group_grant(&opts, &st, ch, /*svc*/ 0x40, /*tg*/ 0x2345, /*src*/ 1001);
    rc |= expect_true("enc override clear", st.p25_sm_tune_count == before + 1);

    return rc;
}
