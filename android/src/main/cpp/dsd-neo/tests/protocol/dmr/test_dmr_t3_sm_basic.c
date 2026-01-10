// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

// Minimal DMR SM smoke test: grant tunes to VC; tick handles release.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/io/rigctl_client.h>
#include <dsd-neo/protocol/dmr/dmr_trunk_sm.h>

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
    // Minimal behavior needed by the test
    if (opts) {
        opts->trunk_is_tuned = 0;
    }
    if (state) {
        state->trunk_vc_freq[0] = 0;
        state->trunk_vc_freq[1] = 0;
    }
}

static void
init_opts_state(dsd_opts* opts, dsd_state* state) {
    memset(opts, 0, sizeof(*opts));
    memset(state, 0, sizeof(*state));
    opts->trunk_enable = 1;               // enable trunking logic
    opts->use_rigctl = 0;                 // avoid IO during test
    opts->audio_in_type = AUDIO_IN_PULSE; // avoid RTL path
    opts->setmod_bw = 0;
    opts->trunk_hangtime = 0.0f;      // no hangtime delay for this test
    state->trunk_cc_freq = 851000000; // pretend CC
}

int
main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    dsd_opts opts;
    dsd_state state;
    init_opts_state(&opts, &state);

    // Initialize the SM
    dmr_sm_init(&opts, &state);

    // Get context to check state
    dmr_sm_ctx_t* ctx = dmr_sm_get_ctx();
    assert(ctx != NULL);
    assert(ctx->state == DMR_SM_ON_CC);

    // Before grant, not tuned
    assert(opts.trunk_is_tuned == 0);
    assert(state.trunk_vc_freq[0] == 0);

    // Group grant with explicit frequency
    long vc = 852000000;
    dmr_sm_emit_group_grant(&opts, &state, vc, /*lpcn*/ 0, /*tg*/ 101, /*src*/ 1234);

    // Expect tuned
    assert(opts.trunk_is_tuned == 1);
    assert(state.trunk_vc_freq[0] == vc);
    assert(state.trunk_vc_freq[1] == vc);
    assert(ctx->state == DMR_SM_TUNED);

    // Simulate voice activity
    dmr_sm_emit_voice_sync(&opts, &state, 0);
    assert(ctx->slots[0].voice_active == 1);

    // Mark voice inactive
    ctx->slots[0].voice_active = 0;

    // Set voice timestamp in the past to exceed hangtime
    double now_m = ctx->t_voice_m; // Get current monotonic time reference
    ctx->t_voice_m = now_m - 10.0; // 10 seconds ago, well past hangtime

    // Tick should trigger release due to hangtime expiry
    dmr_sm_tick(&opts, &state);

    // Expect back on CC
    assert(opts.trunk_is_tuned == 0);
    assert(state.trunk_vc_freq[0] == 0);
    assert(state.trunk_vc_freq[1] == 0);
    assert(ctx->state == DMR_SM_ON_CC);

    printf("DMR_T3_SM_BASIC: OK\n");
    return 0;
}
