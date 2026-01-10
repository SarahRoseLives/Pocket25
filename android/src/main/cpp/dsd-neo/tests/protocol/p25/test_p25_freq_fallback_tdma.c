// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 freq fallback when CC is TDMA but IDEN TDMA unknown.
 * Ensures process_channel_to_freq uses denom=2 in this case.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>

// Stubs for external hooks possibly referenced
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

    // System carries TDMA voice; IDEN TDMA unknown for id=1
    st.p25_sys_is_tdma = 1;
    int id = 1;
    st.p25_chan_tdma[id] = 0;             // unknown
    st.p25_chan_type[id] = 4;             // type doesn't matter without tdma flag
    st.p25_base_freq[id] = 851000000 / 5; // 851 MHz in 5 Hz units
    st.p25_chan_spac[id] = 100;           // 12.5 kHz (100*125)

    // Raw channel 0x1007 → denom fallback 2 → step=7/2=3
    int chan = (id << 12) | 0x0007;
    long f = process_channel_to_freq(&opts, &st, chan);
    long want = 851000000 + 3 * 100 * 125; // 851.0375 MHz
    rc |= expect_eq_long("fallback denom2", f, want);

    return rc;
}
