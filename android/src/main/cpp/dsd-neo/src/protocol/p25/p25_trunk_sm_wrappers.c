// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Legacy/compatibility wrapper APIs for the unified P25 trunking state machine.
 *
 * These entry points dispatch through an optional override table
 * (`p25_sm_get_api()`), which tests can install via `p25_sm_set_api(...)`.
 */

#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_cc_candidates.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm_api.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>

#include <string.h>

static inline double
now_monotonic(void) {
    return dsd_time_now_monotonic_s();
}

/* ============================================================================
 * Neighbor Update and CC Candidate Functions
 * ============================================================================ */

static void
p25_sm_on_neighbor_update_default(dsd_opts* opts, dsd_state* state, const long* freqs, int count) {
    if (count <= 0 || !state || !freqs) {
        return;
    }
    // Lazy-load any persisted candidates once system identity is known
    p25_cc_try_load_cache(opts, state);

    for (int i = 0; i < count; i++) {
        long f = freqs[i];
        if (f == 0) {
            continue;
        }
        // Track neighbor list for UI
        p25_nb_add(state, f);
        // Add to candidate list (dedup + FIFO rollover)
        (void)p25_cc_add_candidate(state, f, 1);
    }
}

void
p25_sm_on_neighbor_update(dsd_opts* opts, dsd_state* state, const long* freqs, int count) {
    p25_sm_api api = p25_sm_get_api();
    if (api.on_neighbor_update) {
        api.on_neighbor_update(opts, state, freqs, count);
        return;
    }
    p25_sm_on_neighbor_update_default(opts, state, freqs, count);
}

static int
p25_sm_next_cc_candidate_default(dsd_state* state, long* out_freq) {
    if (!state || !out_freq) {
        return 0;
    }
    double nowm = now_monotonic();
    return dsd_trunk_cc_candidates_next(state, nowm, out_freq);
}

int
p25_sm_next_cc_candidate(dsd_state* state, long* out_freq) {
    p25_sm_api api = p25_sm_get_api();
    if (api.next_cc_candidate) {
        return api.next_cc_candidate(state, out_freq);
    }
    return p25_sm_next_cc_candidate_default(state, out_freq);
}

/* ============================================================================
 * Legacy Compatibility Wrappers
 * These use weak symbols (where supported) to allow tests to override them.
 * ============================================================================ */

static void
p25_sm_init_default(dsd_opts* opts, dsd_state* state) {
    p25_sm_init_ctx(p25_sm_get_ctx(), opts, state);
}

void
p25_sm_init(dsd_opts* opts, dsd_state* state) {
    p25_sm_api api = p25_sm_get_api();
    if (api.init) {
        api.init(opts, state);
        return;
    }
    p25_sm_init_default(opts, state);
}

static void
p25_sm_on_group_grant_default(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int tg, int src) {
    p25_sm_event_t ev = p25_sm_ev_group_grant(channel, 0, tg, src, svc_bits);
    p25_sm_event(p25_sm_get_ctx(), opts, state, &ev);
}

void
p25_sm_on_group_grant(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int tg, int src) {
    p25_sm_api api = p25_sm_get_api();
    if (api.on_group_grant) {
        api.on_group_grant(opts, state, channel, svc_bits, tg, src);
        return;
    }
    p25_sm_on_group_grant_default(opts, state, channel, svc_bits, tg, src);
}

static void
p25_sm_on_indiv_grant_default(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int dst, int src) {
    p25_sm_event_t ev = p25_sm_ev_indiv_grant(channel, 0, dst, src, svc_bits);
    p25_sm_event(p25_sm_get_ctx(), opts, state, &ev);
}

void
p25_sm_on_indiv_grant(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int dst, int src) {
    p25_sm_api api = p25_sm_get_api();
    if (api.on_indiv_grant) {
        api.on_indiv_grant(opts, state, channel, svc_bits, dst, src);
        return;
    }
    p25_sm_on_indiv_grant_default(opts, state, channel, svc_bits, dst, src);
}

static void
p25_sm_on_release_default(dsd_opts* opts, dsd_state* state) {
    p25_sm_release(p25_sm_get_ctx(), opts, state, "explicit-release");
}

void
p25_sm_on_release(dsd_opts* opts, dsd_state* state) {
    p25_sm_api api = p25_sm_get_api();
    if (api.on_release) {
        api.on_release(opts, state);
        return;
    }
    p25_sm_on_release_default(opts, state);
}

static void
p25_sm_tick_default(dsd_opts* opts, dsd_state* state) {
    p25_sm_tick_ctx(p25_sm_get_ctx(), opts, state);
}

void
p25_sm_tick(dsd_opts* opts, dsd_state* state) {
    p25_sm_api api = p25_sm_get_api();
    if (api.tick) {
        api.tick(opts, state);
        return;
    }
    p25_sm_tick_default(opts, state);
}
