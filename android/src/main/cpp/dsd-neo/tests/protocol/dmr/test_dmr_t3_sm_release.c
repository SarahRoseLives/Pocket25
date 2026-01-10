// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

// DMR SM release gating: defer when slot is active; release via tick.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/dmr/dmr_trunk_sm.h>

// Stubs
void
trunk_tune_to_freq(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    (void)freq;
    (void)ted_sps;
    if (opts) {
        opts->trunk_is_tuned = 1;
    }
    if (state) {
        state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = freq;
    }
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

void
dmr_reset_blocks(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

int
main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    static dsd_opts opts;
    static dsd_state state;
    memset(&opts, 0, sizeof opts);
    memset(&state, 0, sizeof state);
    opts.trunk_enable = 1;
    opts.trunk_hangtime = 0.5f;
    state.trunk_cc_freq = 851000000;

    // Initialize and get context
    dmr_sm_init(&opts, &state);
    dmr_sm_ctx_t* ctx = dmr_sm_get_ctx();
    assert(ctx != NULL);
    assert(ctx->state == DMR_SM_ON_CC);

    // Grant to tune to VC
    long vc = 852000000;
    dmr_sm_emit_group_grant(&opts, &state, vc, 0, 100, 1234);
    assert(opts.trunk_is_tuned == 1);
    assert(ctx->state == DMR_SM_TUNED);

    // Simulate voice active on slot 0
    dmr_sm_emit_voice_sync(&opts, &state, 0);
    assert(ctx->slots[0].voice_active == 1);

    // Tick while voice active - should NOT release
    dmr_sm_tick(&opts, &state);
    assert(opts.trunk_is_tuned == 1);
    assert(ctx->state == DMR_SM_TUNED);

    // Mark voice inactive but with recent activity (hangtime applies)
    ctx->slots[0].voice_active = 0;
    // t_voice_m is still recent, so tick should defer

    // Tick with recent voice - should stay tuned (hangtime)
    dmr_sm_tick(&opts, &state);
    assert(opts.trunk_is_tuned == 1);

    // Set voice timestamp far in the past to exceed hangtime
    // Use a small positive value that's clearly in the past (more than hangtime_s ago)
    double now_m = ctx->t_voice_m; // Get current monotonic time reference
    ctx->t_voice_m = now_m - 10.0; // 10 seconds ago, well past hangtime

    // Tick should now release
    dmr_sm_tick(&opts, &state);
    assert(opts.trunk_is_tuned == 0);
    assert(ctx->state == DMR_SM_ON_CC);

    printf("DMR_T3_SM_RELEASE: OK\n");
    return 0;
}
