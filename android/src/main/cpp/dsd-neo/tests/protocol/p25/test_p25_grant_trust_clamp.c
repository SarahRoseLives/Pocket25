// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 grant trust clamp tests.
 * Ensures untrusted IDENs block tuning unless provisional (provenance unset)
 * on current CC, in which case tuning is allowed.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>

// Stubs for external hooks
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
    const int iden = 1;
    const int channel = (iden << 12) | 0x000A; // ch=10

    // Common IDEN params and CC freq
    static dsd_opts opts;
    static dsd_state st;
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);
    opts.p25_trunk = 1;
    opts.trunk_tune_group_calls = 1;
    st.p25_cc_freq = 851000000;
    st.p25_chan_type[iden] = 1;
    st.p25_chan_tdma[iden] = 0;
    st.p25_base_freq[iden] = 851000000 / 5;
    st.p25_chan_spac[iden] = 100;

    // Case: trust<2 but on CC and provenance unset → allowed
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);
    opts.p25_trunk = 1;
    opts.trunk_tune_group_calls = 1;
    st.p25_cc_freq = 851000000;
    st.p25_chan_type[iden] = 1;
    st.p25_chan_tdma[iden] = 0;
    st.p25_base_freq[iden] = 851000000 / 5;
    st.p25_chan_spac[iden] = 100;
    st.p25_iden_trust[iden] = 1; // untrusted
    st.p25_iden_wacn[iden] = 0;  // provenance unset
    st.p25_iden_sysid[iden] = 0; // provenance unset
    int before = st.p25_sm_tune_count;
    p25_sm_on_group_grant(&opts, &st, channel, 0, /*tg*/ 1234, /*src*/ 5678);
    rc |= expect_true("tune allowed provisional", st.p25_sm_tune_count == before + 1);
    rc |= expect_true("tuned flag set", opts.p25_is_tuned == 1);
    rc |= expect_true("vc freq set", st.p25_vc_freq[0] != 0);

    return rc;
}
