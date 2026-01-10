// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 learned channel map persistence test.
 * Verifies that once a channel→freq is computed, it is stored in trunk_chan_map
 * and used even if IDEN base/spacing are cleared thereafter.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>

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
expect_eq_long(const char* tag, long got, long want) {
    if (got != want) {
        fprintf(stderr, "%s: got %ld want %ld\n", tag, got, want);
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

    int iden = 1;
    int chan = (iden << 12) | 0x000A; // 0x100A
    st.p25_chan_type[iden] = 1;
    st.p25_chan_tdma[iden] = 0;
    st.p25_base_freq[iden] = 851000000 / 5;
    st.p25_chan_spac[iden] = 100;

    long f1 = process_channel_to_freq(&opts, &st, chan);
    long want = 851000000 + 10 * 100 * 125; // 851.125 MHz
    rc |= expect_eq_long("first calc", f1, want);

    // Clear IDEN params; subsequent lookup should still return via trunk_chan_map
    st.p25_base_freq[iden] = 0;
    st.p25_chan_spac[iden] = 0;
    long f2 = process_channel_to_freq(&opts, &st, chan);
    rc |= expect_eq_long("map fallback", f2, want);

    return rc;
}
