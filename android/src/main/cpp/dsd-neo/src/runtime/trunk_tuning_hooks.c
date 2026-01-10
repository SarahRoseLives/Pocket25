// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/trunk_tuning_hooks.h>

#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

#include <time.h>

static dsd_trunk_tuning_hooks g_trunk_tuning_hooks = {0};

static void
dsd_trunk_tuning_fallback_tune_to_freq(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    (void)ted_sps;
    if (!opts || !state || freq <= 0) {
        return;
    }

    state->p25_vc_freq[0] = state->p25_vc_freq[1] = freq;
    state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = freq;
    opts->p25_is_tuned = 1;
    opts->trunk_is_tuned = 1;

    double nowm = dsd_time_now_monotonic_s();
    state->last_vc_sync_time = time(NULL);
    state->p25_last_vc_tune_time = state->last_vc_sync_time;
    state->last_vc_sync_time_m = nowm;
    state->p25_last_vc_tune_time_m = nowm;
}

static void
dsd_trunk_tuning_fallback_return_to_cc(dsd_opts* opts, dsd_state* state) {
    if (opts) {
        opts->p25_is_tuned = 0;
        opts->trunk_is_tuned = 0;
    }
    if (state) {
        state->p25_vc_freq[0] = state->p25_vc_freq[1] = 0;
        state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = 0;
        state->last_vc_sync_time = 0;
        state->last_vc_sync_time_m = 0.0;
    }
}

static void
dsd_trunk_tuning_fallback_tune_to_cc(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    (void)opts;
    (void)ted_sps;
    if (!state || freq <= 0) {
        return;
    }
    state->trunk_cc_freq = freq;
    state->last_cc_sync_time = time(NULL);
    state->last_cc_sync_time_m = dsd_time_now_monotonic_s();
}

void
dsd_trunk_tuning_hooks_set(dsd_trunk_tuning_hooks hooks) {
    g_trunk_tuning_hooks = hooks;
}

void
dsd_trunk_tuning_hook_tune_to_freq(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    if (g_trunk_tuning_hooks.tune_to_freq) {
        g_trunk_tuning_hooks.tune_to_freq(opts, state, freq, ted_sps);
        return;
    }
    dsd_trunk_tuning_fallback_tune_to_freq(opts, state, freq, ted_sps);
}

void
dsd_trunk_tuning_hook_tune_to_cc(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    if (g_trunk_tuning_hooks.tune_to_cc) {
        g_trunk_tuning_hooks.tune_to_cc(opts, state, freq, ted_sps);
        return;
    }
    dsd_trunk_tuning_fallback_tune_to_cc(opts, state, freq, ted_sps);
}

void
dsd_trunk_tuning_hook_return_to_cc(dsd_opts* opts, dsd_state* state) {
    if (g_trunk_tuning_hooks.return_to_cc) {
        g_trunk_tuning_hooks.return_to_cc(opts, state);
        return;
    }
    dsd_trunk_tuning_fallback_return_to_cc(opts, state);
}
