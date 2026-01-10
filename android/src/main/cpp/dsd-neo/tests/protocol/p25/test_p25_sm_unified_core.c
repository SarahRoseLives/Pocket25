// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Basic tests for the unified P25 state machine.
 * 4-state model: IDLE, ON_CC, TUNED, HUNTING
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>

// Minimal stubs for testing
static dsd_opts g_opts;
static dsd_state g_state;

static void
reset_test_state(void) {
    memset(&g_opts, 0, sizeof(g_opts));
    memset(&g_state, 0, sizeof(g_state));
    g_opts.p25_trunk = 1;
    g_opts.trunk_enable = 1;
    g_opts.trunk_hangtime = 2.0f; // op25 TGID_HOLD_TIME
    g_opts.trunk_tune_group_calls = 1;
    g_opts.verbose = 0;
    g_state.p25_cc_freq = 851000000; // Fake CC freq
}

// Test: Init sets correct initial state
static int
test_init_with_cc(void) {
    reset_test_state();
    p25_sm_ctx_t ctx;

    p25_sm_init_ctx(&ctx, &g_opts, &g_state);

    if (ctx.state != P25_SM_ON_CC) {
        fprintf(stderr, "FAIL: Expected ON_CC, got %s\n", p25_sm_state_name(ctx.state));
        return 1;
    }
    if (!ctx.initialized) {
        fprintf(stderr, "FAIL: Expected initialized=1\n");
        return 1;
    }
    return 0;
}

// Test: Init without CC sets IDLE
static int
test_init_without_cc(void) {
    reset_test_state();
    g_state.p25_cc_freq = 0; // No CC known
    p25_sm_ctx_t ctx;

    p25_sm_init_ctx(&ctx, &g_opts, &g_state);

    if (ctx.state != P25_SM_IDLE) {
        fprintf(stderr, "FAIL: Expected IDLE, got %s\n", p25_sm_state_name(ctx.state));
        return 1;
    }
    return 0;
}

// Test: Grant transitions to TUNED
static int
test_grant_to_tuned(void) {
    reset_test_state();
    // Set up a channel->freq mapping so grant can compute frequency
    g_state.trunk_chan_map[0x1234] = 851500000;

    p25_sm_ctx_t ctx;
    p25_sm_init_ctx(&ctx, &g_opts, &g_state);

    p25_sm_event_t ev = p25_sm_ev_group_grant(0x1234, 851500000, 1000, 123, 0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // In 4-state model, grant goes to TUNED (which includes armed/following/hangtime)
    if (ctx.state != P25_SM_TUNED) {
        fprintf(stderr, "FAIL: Expected TUNED after grant, got %s\n", p25_sm_state_name(ctx.state));
        return 1;
    }
    if (ctx.vc_freq_hz != 851500000) {
        fprintf(stderr, "FAIL: Expected vc_freq_hz=851500000, got %ld\n", ctx.vc_freq_hz);
        return 1;
    }
    if (ctx.vc_tg != 1000) {
        fprintf(stderr, "FAIL: Expected vc_tg=1000, got %d\n", ctx.vc_tg);
        return 1;
    }
    return 0;
}

// Test: PTT sets voice_active in TUNED state
static int
test_ptt_voice_active(void) {
    reset_test_state();
    g_state.trunk_chan_map[0x1234] = 851500000;

    p25_sm_ctx_t ctx;
    p25_sm_init_ctx(&ctx, &g_opts, &g_state);

    // Grant
    p25_sm_event_t ev = p25_sm_ev_group_grant(0x1234, 851500000, 1000, 123, 0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // PTT
    ev = p25_sm_ev_ptt(0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // Still in TUNED state (now unified)
    if (ctx.state != P25_SM_TUNED) {
        fprintf(stderr, "FAIL: Expected TUNED after PTT, got %s\n", p25_sm_state_name(ctx.state));
        return 1;
    }
    if (ctx.slots[0].voice_active != 1) {
        fprintf(stderr, "FAIL: Expected slot[0].voice_active=1\n");
        return 1;
    }
    return 0;
}

// Test: END clears voice_active and releases when all slots are inactive
// For P25P1 (non-TDMA), an explicit END triggers immediate release to CC
// rather than waiting for hangtime. This matches P25P1 LCW 0x4F behavior.
static int
test_end_clears_voice(void) {
    reset_test_state();
    g_state.trunk_chan_map[0x1234] = 851500000;

    p25_sm_ctx_t ctx;
    p25_sm_init_ctx(&ctx, &g_opts, &g_state);

    // Grant -> PTT -> END
    p25_sm_event_t ev = p25_sm_ev_group_grant(0x1234, 851500000, 1000, 123, 0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    ev = p25_sm_ev_ptt(0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    ev = p25_sm_ev_end(0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // Explicit END triggers immediate release to ON_CC (no hangtime wait)
    // This is the P25P2 fix: MAC_END_PTT should return to CC immediately
    // rather than waiting for the 2s hangtime timeout.
    if (ctx.state != P25_SM_ON_CC) {
        fprintf(stderr, "FAIL: Expected ON_CC after END (immediate release), got %s\n", p25_sm_state_name(ctx.state));
        return 1;
    }
    if (ctx.slots[0].voice_active != 0) {
        fprintf(stderr, "FAIL: Expected slot[0].voice_active=0 after END\n");
        return 1;
    }
    return 0;
}

// Test: State name function for 4-state model
static int
test_state_names(void) {
    if (strcmp(p25_sm_state_name(P25_SM_IDLE), "IDLE") != 0) {
        fprintf(stderr, "FAIL: Expected 'IDLE'\n");
        return 1;
    }
    if (strcmp(p25_sm_state_name(P25_SM_ON_CC), "ON_CC") != 0) {
        fprintf(stderr, "FAIL: Expected 'ON_CC'\n");
        return 1;
    }
    if (strcmp(p25_sm_state_name(P25_SM_TUNED), "TUNED") != 0) {
        fprintf(stderr, "FAIL: Expected 'TUNED'\n");
        return 1;
    }
    if (strcmp(p25_sm_state_name(P25_SM_HUNTING), "HUNT") != 0) {
        fprintf(stderr, "FAIL: Expected 'HUNT'\n");
        return 1;
    }
    return 0;
}

// Test: Config defaults
static int
test_config_defaults(void) {
    reset_test_state();
    p25_sm_ctx_t ctx;
    p25_sm_init_ctx(&ctx, &g_opts, &g_state);

    // Check defaults (aligned with op25 timing parameters)
    if (ctx.config.hangtime_s != 2.0) {
        fprintf(stderr, "FAIL: Expected hangtime_s=2.0 (op25 TGID_HOLD_TIME), got %.2f\n", ctx.config.hangtime_s);
        return 1;
    }
    if (ctx.config.grant_timeout_s != 3.0) {
        fprintf(stderr, "FAIL: Expected grant_timeout_s=3.0 (op25 TSYS_HOLD_TIME), got %.2f\n",
                ctx.config.grant_timeout_s);
        return 1;
    }
    if (ctx.config.cc_grace_s != 5.0) {
        fprintf(stderr, "FAIL: Expected cc_grace_s=5.0 (op25 CC_HUNT_TIME), got %.2f\n", ctx.config.cc_grace_s);
        return 1;
    }
    return 0;
}

// Test: Singleton access
static int
test_singleton(void) {
    p25_sm_ctx_t* sm1 = p25_sm_get_ctx();
    p25_sm_ctx_t* sm2 = p25_sm_get_ctx();

    if (sm1 != sm2) {
        fprintf(stderr, "FAIL: Singleton should return same pointer\n");
        return 1;
    }
    if (!sm1->initialized) {
        fprintf(stderr, "FAIL: Singleton should be initialized\n");
        return 1;
    }
    return 0;
}

// Test: Audio allowed query
static int
test_audio_allowed(void) {
    reset_test_state();
    g_state.trunk_chan_map[0x1234] = 851500000;

    p25_sm_ctx_t ctx;
    p25_sm_init_ctx(&ctx, &g_opts, &g_state);

    // Before grant, audio not allowed
    if (p25_sm_audio_allowed(&ctx, &g_state, 0) != 0) {
        fprintf(stderr, "FAIL: Audio should not be allowed before grant\n");
        return 1;
    }

    // Grant + PTT
    p25_sm_event_t ev = p25_sm_ev_group_grant(0x1234, 851500000, 1000, 123, 0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    ev = p25_sm_ev_ptt(0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // PTT alone doesn't enable audio - that's handled by MAC_PTT in xcch.c
    // which sets p25_p2_audio_allowed. Simulate what xcch.c does:
    g_state.p25_p2_audio_allowed[0] = 1;

    // Now audio should be allowed (via legacy state)
    if (p25_sm_audio_allowed(&ctx, &g_state, 0) != 1) {
        fprintf(stderr, "FAIL: Audio should be allowed when p25_p2_audio_allowed is set\n");
        return 1;
    }

    // Test that disabling it works
    g_state.p25_p2_audio_allowed[0] = 0;
    if (p25_sm_audio_allowed(&ctx, &g_state, 0) != 0) {
        fprintf(stderr, "FAIL: Audio should not be allowed when p25_p2_audio_allowed is cleared\n");
        return 1;
    }

    return 0;
}

// Test: SACCH slot mapping helper
static int
test_sacch_slot_mapping(void) {
    // SACCH uses inverted slot mapping
    if (p25_sacch_to_voice_slot(0) != 1) {
        fprintf(stderr, "FAIL: p25_sacch_to_voice_slot(0) should be 1\n");
        return 1;
    }
    if (p25_sacch_to_voice_slot(1) != 0) {
        fprintf(stderr, "FAIL: p25_sacch_to_voice_slot(1) should be 0\n");
        return 1;
    }
    return 0;
}

// Test: P25P2 TDMA - END on one slot keeps TUNED if other slot still active
static int
test_tdma_partial_end_stays_tuned(void) {
    reset_test_state();
    g_state.trunk_chan_map[0x1234] = 851500000;
    // Mark this channel as TDMA (P25P2)
    g_state.p25_chan_tdma_explicit[1] = 2; // iden=1, explicit TDMA hint

    p25_sm_ctx_t ctx;
    p25_sm_init_ctx(&ctx, &g_opts, &g_state);

    // Grant on TDMA channel
    p25_sm_event_t ev = p25_sm_ev_group_grant(0x1234, 851500000, 1000, 123, 0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // Should be detected as TDMA
    if (!ctx.vc_is_tdma) {
        fprintf(stderr, "FAIL: Expected vc_is_tdma=1 for TDMA channel\n");
        return 1;
    }

    // PTT on both slots
    ev = p25_sm_ev_ptt(0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);
    ev = p25_sm_ev_ptt(1);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // Simulate audio allowed on slot 1 (slot 0 will end, slot 1 still active)
    g_state.p25_p2_audio_allowed[1] = 1;

    // END on slot 0 only
    ev = p25_sm_ev_end(0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // Should stay TUNED because slot 1 is still active
    if (ctx.state != P25_SM_TUNED) {
        fprintf(stderr, "FAIL: Expected TUNED after END on slot 0 (slot 1 still active), got %s\n",
                p25_sm_state_name(ctx.state));
        return 1;
    }

    // Now end slot 1 as well
    g_state.p25_p2_audio_allowed[1] = 0;
    ev = p25_sm_ev_end(1);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // Now both slots ended - should release to ON_CC
    if (ctx.state != P25_SM_ON_CC) {
        fprintf(stderr, "FAIL: Expected ON_CC after END on both slots, got %s\n", p25_sm_state_name(ctx.state));
        return 1;
    }

    return 0;
}

// Test: P25P2 TDMA - END on single-slot call releases immediately
// This tests the bug fix where calls on only one slot were waiting for
// the full hangtime (10s forced release) instead of releasing on MAC_END_PTT.
static int
test_tdma_single_slot_end_releases(void) {
    reset_test_state();
    g_state.trunk_chan_map[0x1234] = 851500000;
    // Mark this channel as TDMA (P25P2)
    g_state.p25_chan_tdma_explicit[1] = 2; // iden=1, explicit TDMA hint

    p25_sm_ctx_t ctx;
    p25_sm_init_ctx(&ctx, &g_opts, &g_state);

    // Grant on TDMA channel
    p25_sm_event_t ev = p25_sm_ev_group_grant(0x1234, 851500000, 1000, 123, 0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // Should be detected as TDMA
    if (!ctx.vc_is_tdma) {
        fprintf(stderr, "FAIL: Expected vc_is_tdma=1 for TDMA channel\n");
        return 1;
    }

    // PTT on slot 0 ONLY - slot 1 never has any activity
    ev = p25_sm_ev_ptt(0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // Simulate what xcch.c does: enable audio on PTT
    g_state.p25_p2_audio_allowed[0] = 1;

    // Simulate audio in the ring buffer (jitter buffer has samples)
    g_state.p25_p2_audio_ring_count[0] = 5;

    // Verify slot 1 never had activity
    if (ctx.slots[1].last_active_m != 0.0) {
        fprintf(stderr, "FAIL: Expected slot 1 last_active_m=0 (never active)\n");
        return 1;
    }

    // END on slot 0 - should release immediately since slot 1 never had activity
    // This mimics the real scenario: xcch.c calls p25_sm_emit_end() BEFORE clearing
    // p25_p2_audio_allowed, so the SM must handle this correctly.
    // Note: p25_p2_audio_allowed[0] is still 1 AND ring buffer has audio!
    ev = p25_sm_ev_end(0);
    p25_sm_event(&ctx, &g_opts, &g_state, &ev);

    // Should release to ON_CC immediately - not waiting for slot 1
    if (ctx.state != P25_SM_ON_CC) {
        fprintf(stderr, "FAIL: Expected ON_CC after END on single-slot TDMA call, got %s\n",
                p25_sm_state_name(ctx.state));
        fprintf(stderr, "      (slot 1 never had activity, should not block release)\n");
        fprintf(stderr, "      audio_allowed[0]=%d audio_allowed[1]=%d\n", g_state.p25_p2_audio_allowed[0],
                g_state.p25_p2_audio_allowed[1]);
        return 1;
    }

    // Verify the SM cleared audio_allowed for slot 0
    if (g_state.p25_p2_audio_allowed[0] != 0) {
        fprintf(stderr, "FAIL: Expected audio_allowed[0]=0 after END, got %d\n", g_state.p25_p2_audio_allowed[0]);
        return 1;
    }

    return 0;
}

int
main(void) {
    int fail = 0;

    printf("Testing P25 SM (4-state model)...\n");

    fail += test_init_with_cc();
    fail += test_init_without_cc();
    fail += test_grant_to_tuned();
    fail += test_ptt_voice_active();
    fail += test_end_clears_voice();
    fail += test_state_names();
    fail += test_config_defaults();
    fail += test_singleton();
    fail += test_audio_allowed();
    fail += test_sacch_slot_mapping();
    fail += test_tdma_partial_end_stays_tuned();
    fail += test_tdma_single_slot_end_releases();

    if (fail) {
        printf("FAILED: %d test(s)\n", fail);
        return 1;
    }
    printf("PASSED: All P25 SM tests\n");
    return 0;
}
