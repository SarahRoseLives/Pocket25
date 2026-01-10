// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Verify CC candidate cooldown: after tuning a failing candidate, it is
 * cooled down and skipped on the next hunt in favor of another candidate. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>

static long g_last_tuned_cc = 0;

void
trunk_tune_to_cc(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    (void)opts;
    (void)state;
    (void)ted_sps;
    g_last_tuned_cc = freq;
}

static void
install_trunk_tuning_hooks(void) {
    dsd_trunk_tuning_hooks hooks = {0};
    hooks.tune_to_cc = trunk_tune_to_cc;
    dsd_trunk_tuning_hooks_set(hooks);
}

static void
init_basic(dsd_opts* o, dsd_state* s) {
    memset(o, 0, sizeof(*o));
    memset(s, 0, sizeof(*s));
    o->p25_trunk = 1;
    o->trunk_hangtime = 0.2; // short for test
    o->p25_prefer_candidates = 1;
    s->p25_cc_freq = 851000000;
    p25_sm_init(o, s);
}

int
main(void) {
    static dsd_opts o;
    static dsd_state st;
    install_trunk_tuning_hooks();
    init_basic(&o, &st);
    // Two candidates A, B
    long A = 852000000;
    long B = 853000000;
    (void)dsd_trunk_cc_candidates_add(&st, A, 0);
    (void)dsd_trunk_cc_candidates_add(&st, B, 0);
    // Force CC hunt
    st.last_cc_sync_time_m = dsd_time_now_monotonic_s() - 10.0;

    // First tick: should tune to A
    g_last_tuned_cc = 0;
    p25_sm_tick(&o, &st);
    assert(g_last_tuned_cc == A);

    // Simulate evaluation window expiry with no CC activity to trigger cooldown for A
    st.p25_cc_eval_freq = A;
    st.p25_cc_eval_start_m = dsd_time_now_monotonic_s() - 5.0;
    st.last_cc_sync_time_m = 0.0; // no CC activity

    // Next tick: cooldown applied; next hunt should pick B
    g_last_tuned_cc = 0;
    p25_sm_tick(&o, &st);
    assert(g_last_tuned_cc == B);
    return 0;
}
