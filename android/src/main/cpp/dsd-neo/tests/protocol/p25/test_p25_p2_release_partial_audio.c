// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 2: ensure partial SS18 audio is flushed on release.
 *
 * Short P25p2 calls can end before a full 18-frame superframe is available for
 * playSynthesizedVoiceSS18(), causing the buffered audio to be dropped when
 * returning to the control channel. Verify that p25_sm_on_release() triggers a
 * best-effort flush that clears the buffered short frames so short calls are
 * still audible.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/p25_optional_hooks.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>

// Minimal IO stubs (avoid actual tuning/audio devices in unit tests)
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

static int g_return_to_cc_called = 0;

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
    g_return_to_cc_called++;
}

static void
install_trunk_tuning_hooks(void) {
    dsd_trunk_tuning_hooks hooks = {0};
    hooks.return_to_cc = return_to_cc;
    dsd_trunk_tuning_hooks_set(hooks);
}

static int g_p25p2_flush_called = 0;

void
dsd_p25p2_flush_partial_audio(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    g_p25p2_flush_called++;
    if (!state) {
        return;
    }
    state->voice_counter[0] = 0;
    state->voice_counter[1] = 0;
    memset(state->s_l4, 0, sizeof(state->s_l4));
    memset(state->s_r4, 0, sizeof(state->s_r4));
}

static void
install_p25_optional_hooks(void) {
    dsd_p25_optional_hooks hooks = {0};
    hooks.p25p2_flush_partial_audio = dsd_p25p2_flush_partial_audio;
    dsd_p25_optional_hooks_set(hooks);
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
    install_trunk_tuning_hooks();
    install_p25_optional_hooks();
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);

    opts.p25_trunk = 1;
    opts.trunk_tune_group_calls = 1;
    opts.floating_point = 0;
    opts.pulse_digi_rate_out = 8000;
    opts.slot1_on = 1;
    opts.slot2_on = 1;

    // Establish a TDMA VC context so the SM release path executes P25p2 logic.
    st.p25_cc_freq = 851000000;
    int id = 2;
    st.p25_chan_iden = id;
    st.p25_chan_type[id] = 3;
    st.p25_chan_tdma[id] = 1;
    st.p25_base_freq[id] = 851000000 / 5;
    st.p25_chan_spac[id] = 100;
    st.p25_iden_trust[id] = 2;

    p25_sm_init(&opts, &st);
    int ch_tdma = (id << 12) | 0x0001;
    p25_sm_on_group_grant(&opts, &st, ch_tdma, 0, 1234, 5678);

    // Simulate a short call that buffered some audio but ended before the
    // normal SS18 playback cadence. Also simulate gates already cleared.
    st.s_l4[0][0] = 123;
    st.s_r4[0][0] = -456;
    st.voice_counter[0] = 1;
    st.voice_counter[1] = 1;
    st.p25_p2_audio_allowed[0] = 0;
    st.p25_p2_audio_allowed[1] = 0;

    g_return_to_cc_called = 0;
    g_p25p2_flush_called = 0;
    p25_sm_on_release(&opts, &st);
    rc |= expect_eq_int("return_to_cc called", g_return_to_cc_called, 1);
    rc |= expect_eq_int("flush called", g_p25p2_flush_called, 1);

    // Flush should clear buffered samples and reset counters.
    rc |= expect_eq_int("s_l4 cleared", st.s_l4[0][0], 0);
    rc |= expect_eq_int("s_r4 cleared", st.s_r4[0][0], 0);
    rc |= expect_eq_int("voice_counter[0] reset", st.voice_counter[0], 0);
    rc |= expect_eq_int("voice_counter[1] reset", st.voice_counter[1], 0);

    return rc;
}
