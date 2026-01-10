// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Verify retune backoff applies per-slot on TDMA voice channels:
 * - After returning from a VC with no voice observed, a short retune backoff
 *   is applied for the same RF frequency and slot.
 * - A subsequent grant to the opposite slot at the same RF is allowed
 *   immediately (no backoff).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>

// --- IO control stubs (rigctl/RTL) ---

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

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    if (opts) {
        opts->p25_is_tuned = 0;
        opts->trunk_is_tuned = 0;
    }
    if (state) {
        state->p25_vc_freq[0] = state->p25_vc_freq[1] = 0;
        state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = 0;
    }
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

    // Enable trunking and seed a CC
    opts.p25_trunk = 1;
    opts.trunk_tune_group_calls = 1;
    opts.trunk_hangtime = 0.2f;
    opts.p25_grant_voice_to_s = 0.5; // apply backoff when dt_since_tune >= 0.5s
    opts.p25_retune_backoff_s = 2.0; // backoff window
    st.p25_cc_freq = 851000000;      // known CC

    // TDMA IDEN: id=2, type=3 => denom=2; trusted
    int id = 2;
    st.p25_chan_iden = id;
    st.p25_chan_type[id] = 3;
    st.p25_chan_tdma[id] = 1;
    st.p25_base_freq[id] = 851000000 / 5;
    st.p25_chan_spac[id] = 100;
    st.p25_iden_trust[id] = 2;

    // Two channels mapping to the same RF: low bit selects slot
    int ch_slot0 = (id << 12) | 0x0002; // slot 0
    int ch_slot1 = (id << 12) | 0x0003; // slot 1 (same RF)

    // 1) Grant on slot 1 -> tune
    opts.p25_is_tuned = 0;
    unsigned prev_tunes = st.p25_sm_tune_count;
    p25_sm_on_group_grant(&opts, &st, ch_slot1, 0 /*svc*/, 1001, 2002);
    rc |= expect_true("initial tune", st.p25_sm_tune_count == prev_tunes + 1 && opts.p25_is_tuned == 1);

    // 2) Force a no-voice return so retune backoff is armed for this slot/freq
    // Ensure no MAC since tune and dt_since_tune >= grant_voice_to
    st.p25_p2_last_mac_active[0] = 0;
    st.p25_p2_last_mac_active[1] = 0;
    st.p25_p2_audio_allowed[0] = 0;
    st.p25_p2_audio_allowed[1] = 0;
    st.p25_p2_audio_ring_count[0] = 0;
    st.p25_p2_audio_ring_count[1] = 0;
    st.p25_last_vc_tune_time = time(NULL) - 1; // > 0.5s
    st.p25_p2_active_slot = 1;                 // last active slot = 1
    st.p25_sm_force_release = 1;               // force immediate return to CC
    p25_sm_on_release(&opts, &st);
    rc |= expect_true("returned", opts.p25_is_tuned == 0);

    // 3) Opposite-slot grant on same RF should be allowed immediately
    prev_tunes = st.p25_sm_tune_count;
    opts.p25_is_tuned = 0;
    p25_sm_on_group_grant(&opts, &st, ch_slot0, 0 /*svc*/, 1001, 2002);
    rc |= expect_true("other-slot allowed", st.p25_sm_tune_count == prev_tunes + 1 && opts.p25_is_tuned == 1);

    return rc;
}
