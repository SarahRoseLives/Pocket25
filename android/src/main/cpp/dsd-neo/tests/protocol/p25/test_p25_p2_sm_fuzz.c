// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 2 trunk SM fuzzer: interleave MAC-like activity/idle windows,
 * flip both slots' audio gates rapidly, occasionally set ENC pending flags,
 * and assert the state machine returns to CC within hangtime once both slots
 * are idle and activity has ceased. Also verifies forced release behavior.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>

// Strong stubs to observe tunes and releases
static int g_vc_tunes = 0;
static int g_cc_returns = 0;

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
trunk_tune_to_freq(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    (void)freq;
    (void)ted_sps;
    g_vc_tunes++;
    if (opts) {
        opts->p25_is_tuned = 1;
        opts->trunk_is_tuned = 1;
    }
    if (state) {
        state->last_vc_sync_time = time(NULL);
    }
}

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    g_cc_returns++;
    if (opts) {
        opts->p25_is_tuned = 0;
        opts->trunk_is_tuned = 0;
    }
    if (state) {
        state->p25_vc_freq[0] = state->p25_vc_freq[1] = 0;
        state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = 0;
    }
}

static void
install_trunk_tuning_hooks(void) {
    dsd_trunk_tuning_hooks hooks = {0};
    hooks.tune_to_freq = trunk_tune_to_freq;
    hooks.return_to_cc = return_to_cc;
    dsd_trunk_tuning_hooks_set(hooks);
}

// Deterministic xorshift RNG
static uint32_t s_rng = 0xC0FFEEu;

static uint32_t
rnd(void) {
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
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
    install_trunk_tuning_hooks();
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);
    opts.p25_trunk = 1;
    opts.trunk_hangtime = 1.0f;
    st.p25_cc_freq = 851000000;

    // Seed a TDMA IDEN so channel->freq mapping works for TDMA grants
    int id = 2;
    st.p25_chan_tdma[id] = 1;
    st.p25_base_freq[id] = 851000000 / 5;
    st.p25_chan_spac[id] = 100;
    st.p25_iden_trust[id] = 2;

    // Run randomized trials
    const int trials = 128;
    for (int t = 0; t < trials; t++) {
        // TDMA channel: use low-bit as slot hint; vary channel index
        int low = (int)(rnd() % 16);
        int ch = (id << 12) | low;

        // Ensure untuned; request a grant
        opts.p25_is_tuned = 0;
        int before = g_vc_tunes;
        p25_sm_on_group_grant(&opts, &st, ch, /*svc*/ 0x00, /*tg*/ 40000 + (t & 0xFF), /*src*/ 1000 + (t & 0xFF));
        int tuned = (g_vc_tunes > before) && (opts.p25_is_tuned == 1);

        if (!tuned) {
            // If not tuned (e.g., mapping refused), skip this trial
            continue;
        }

        // Simulate MAC_SIGNAL bursts by refreshing activity; randomly flip audio gates
        st.p25_p2_active_slot = (ch & 1) ? 1 : 0;
        for (int k = 0; k < 8; k++) {
            st.p25_p2_audio_allowed[0] = (rnd() & 1);
            st.p25_p2_audio_allowed[1] = (rnd() & 1);
            // Occasionally mark ENC pending to poke teardown paths
            if ((rnd() & 7) == 0) {
                st.p25_p2_enc_pending[0] = (rnd() & 1);
                st.p25_p2_enc_pending[1] = (rnd() & 1);
            }
            st.last_vc_sync_time = time(NULL);
        }

        // Now simulate MAC_IDLE: both slots idle, activity stale beyond hangtime
        st.p25_p2_audio_allowed[0] = 0;
        st.p25_p2_audio_allowed[1] = 0;
        st.last_vc_sync_time = time(NULL) - 3; // > hangtime

        // Give the SM a few ticks to release
        int steps = 0;
        const int max_steps = 5;
        int before_rtc = g_cc_returns;
        while (opts.p25_is_tuned == 1 && steps++ < max_steps) {
            p25_sm_tick(&opts, &st);
        }
        rc |= expect_true("p2-release", g_cc_returns > before_rtc && opts.p25_is_tuned == 0);

        // Forced release should always return to CC even if timers are borderline
        opts.p25_is_tuned = 1; // pretend we’re back on VC
        st.p25_p2_audio_allowed[0] = st.p25_p2_audio_allowed[1] = 0;
        st.p25_sm_force_release = 1;
        before_rtc = g_cc_returns;
        p25_sm_on_release(&opts, &st);
        rc |= expect_true("p2-forced-release", g_cc_returns > before_rtc && opts.p25_is_tuned == 0);
    }

    return rc;
}
