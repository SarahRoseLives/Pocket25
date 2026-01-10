// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * End-to-end DMR Tier III trunking shim tests:
 * - Neighbor/alternate CC candidates
 * - Explicit frequency grants
 * - LPCN-derived grants with trust gating (on-CC vs off-CC)
 * - Release handling via tick with slot activity + hangtime
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/io/rigctl_client.h>
#include <dsd-neo/protocol/dmr/dmr_trunk_sm.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>

// Rigctl/RTL and CC-return stubs to avoid external I/O and core linkage
bool
SetFreq(dsd_socket_t sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return false;
}

bool
SetModulation(dsd_socket_t sockfd, int bandwidth) {
    (void)sockfd;
    (void)bandwidth;
    return false;
}

long int
GetCurrentFreq(dsd_socket_t sockfd) {
    (void)sockfd;
    return 0;
}
struct RtlSdrContext;
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
        opts->trunk_is_tuned = 0;
    }
    if (state) {
        state->trunk_vc_freq[0] = 0;
        state->trunk_vc_freq[1] = 0;
    }
}

// Provide local stubs to satisfy DMR SM linker deps
void
dmr_reset_blocks(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
trunk_tune_to_freq(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    (void)ted_sps;
    if (!opts || !state || freq <= 0) {
        return;
    }
    state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = freq;
    opts->trunk_is_tuned = 1;
    state->last_vc_sync_time = time(NULL);
}

static void
init_env(dsd_opts* opts, dsd_state* state) {
    memset(opts, 0, sizeof(*opts));
    memset(state, 0, sizeof(*state));
    opts->trunk_enable = 1;
    opts->use_rigctl = 0;
    opts->audio_in_type = AUDIO_IN_PULSE;
    state->trunk_cc_freq = 851000000; // mock CC
}

static void
test_neighbor_candidates(void) {
    static dsd_opts opts;
    static dsd_state state;
    init_env(&opts, &state);

    long cand[3] = {851012500, 852500000, 0};
    dmr_sm_on_neighbor_update(&opts, &state, cand, 3);
    const dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_peek(&state);
    assert(cc != NULL);
    assert(cc->count >= 2);

    long next = 0;
    int ok = dmr_sm_next_cc_candidate(&state, &next);
    assert(ok == 1);
    assert(next == cand[0] || next == cand[1]);
}

static void
test_explicit_grant_and_release(void) {
    static dsd_opts opts;
    static dsd_state state;
    init_env(&opts, &state);
    opts.trunk_hangtime = 0.5f;

    // Initialize SM
    dmr_sm_init(&opts, &state);
    dmr_sm_ctx_t* ctx = dmr_sm_get_ctx();
    assert(ctx != NULL);

    long vc = 852000000;
    dmr_sm_emit_group_grant(&opts, &state, vc, /*lpcn*/ 0, /*tg*/ 1001, /*src*/ 42);
    assert(opts.trunk_is_tuned == 1);
    assert(state.trunk_vc_freq[0] == vc);
    assert(ctx->state == DMR_SM_TUNED);

    // Voice active on slot 0
    dmr_sm_emit_voice_sync(&opts, &state, 0);
    assert(ctx->slots[0].voice_active == 1);

    // Tick while voice active - should stay tuned
    dmr_sm_tick(&opts, &state);
    assert(opts.trunk_is_tuned == 1);

    // Mark voice inactive (but hangtime not expired yet)
    ctx->slots[0].voice_active = 0;
    // t_voice_m was just set, so within hangtime

    // Tick - should stay tuned (hangtime)
    dmr_sm_tick(&opts, &state);
    assert(opts.trunk_is_tuned == 1);

    // Set voice timestamp far in past to exceed hangtime
    double now_m = ctx->t_voice_m; // Get current monotonic time reference
    ctx->t_voice_m = now_m - 10.0; // 10 seconds ago, well past hangtime

    // Tick - should release
    dmr_sm_tick(&opts, &state);
    assert(opts.trunk_is_tuned == 0);
    assert(ctx->state == DMR_SM_ON_CC);
}

static void
test_lpcn_trust_gating(void) {
    static dsd_opts opts;
    static dsd_state state;
    init_env(&opts, &state);

    // Initialize SM
    dmr_sm_init(&opts, &state);
    dmr_sm_ctx_t* ctx = dmr_sm_get_ctx();

    // On CC (trunk_is_tuned==0): allow tuning with untrusted LPCN mapping
    int lpcn = 0x0123;
    long f1 = 853000000;
    state.trunk_chan_map[lpcn] = f1;
    state.dmr_lcn_trust[lpcn] = 1; // unconfirmed
    opts.trunk_is_tuned = 0;       // on CC
    ctx->state = DMR_SM_ON_CC;
    dmr_sm_emit_group_grant(&opts, &state, /*freq_hz*/ 0, lpcn, /*tg*/ 101, /*src*/ 99);
    assert(opts.trunk_is_tuned == 1);
    assert(state.trunk_vc_freq[0] == f1);

    // Off CC (currently tuned to VC): block tune with untrusted mapping
    int lpcn2 = 0x0124;
    long f2 = 854000000;
    state.trunk_chan_map[lpcn2] = f2;
    state.dmr_lcn_trust[lpcn2] = 1; // unconfirmed
    long prev = state.trunk_vc_freq[0];
    opts.trunk_is_tuned = 1; // off CC
    dmr_sm_emit_group_grant(&opts, &state, /*freq_hz*/ 0, lpcn2, /*tg*/ 101, /*src*/ 99);
    assert(state.trunk_vc_freq[0] == prev); // unchanged (blocked)
}

int
main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    test_neighbor_candidates();
    test_explicit_grant_and_release();
    test_lpcn_trust_gating();
    printf("DMR_T3_SHIM: OK\n");
    return 0;
}
