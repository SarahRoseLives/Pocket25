// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/trunk_cc_candidates.h>

#include <dsd-neo/core/state.h>

#include <stdlib.h>

dsd_trunk_cc_candidates*
dsd_trunk_cc_candidates_get(dsd_state* state) {
    if (!state) {
        return NULL;
    }

    dsd_trunk_cc_candidates* cc =
        DSD_STATE_EXT_GET_AS(dsd_trunk_cc_candidates, state, DSD_STATE_EXT_ENGINE_TRUNK_CC_CANDIDATES);
    if (cc) {
        return cc;
    }

    cc = (dsd_trunk_cc_candidates*)calloc(1, sizeof(*cc));
    if (!cc) {
        return NULL;
    }
    if (dsd_state_ext_set(state, DSD_STATE_EXT_ENGINE_TRUNK_CC_CANDIDATES, cc, free) != 0) {
        free(cc);
        return NULL;
    }
    return cc;
}

const dsd_trunk_cc_candidates*
dsd_trunk_cc_candidates_peek(const dsd_state* state) {
    if (!state) {
        return NULL;
    }

    return DSD_STATE_EXT_GET_AS(const dsd_trunk_cc_candidates, (dsd_state*)state,
                                DSD_STATE_EXT_ENGINE_TRUNK_CC_CANDIDATES);
}

int
dsd_trunk_cc_candidates_add(dsd_state* state, long freq_hz, int bump_added) {
    if (!state || freq_hz == 0) {
        return 0;
    }

    dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_get(state);
    if (!cc) {
        return 0;
    }

    if (cc->count < 0 || cc->count > DSD_TRUNK_CC_CANDIDATES_MAX) {
        cc->count = 0;
        cc->idx = 0;
    }

    for (int k = 0; k < cc->count; k++) {
        if (cc->candidates[k] == freq_hz) {
            return 0;
        }
    }

    if (cc->count < DSD_TRUNK_CC_CANDIDATES_MAX) {
        cc->candidates[cc->count++] = freq_hz;
    } else {
        for (int k = 1; k < DSD_TRUNK_CC_CANDIDATES_MAX; k++) {
            cc->candidates[k - 1] = cc->candidates[k];
        }
        cc->candidates[DSD_TRUNK_CC_CANDIDATES_MAX - 1] = freq_hz;
        if (cc->idx > 0) {
            cc->idx--;
        }
    }

    if (bump_added) {
        cc->added++;
    }
    return 1;
}

int
dsd_trunk_cc_candidates_next(dsd_state* state, double now_monotonic_s, long* out_freq_hz) {
    if (!state || !out_freq_hz) {
        return 0;
    }

    dsd_trunk_cc_candidates* cc =
        DSD_STATE_EXT_GET_AS(dsd_trunk_cc_candidates, state, DSD_STATE_EXT_ENGINE_TRUNK_CC_CANDIDATES);
    if (!cc) {
        return 0;
    }

    if (cc->count < 0 || cc->count > DSD_TRUNK_CC_CANDIDATES_MAX) {
        cc->count = 0;
        cc->idx = 0;
        return 0;
    }

    const long skip_cc_freq = (state->trunk_cc_freq != 0) ? state->trunk_cc_freq : state->p25_cc_freq;

    for (int tries = 0; tries < cc->count; tries++) {
        if (cc->idx >= cc->count) {
            cc->idx = 0;
        }
        int idx = cc->idx++;
        long f = cc->candidates[idx];
        if (f != 0 && f != skip_cc_freq) {
            double cool_until = (idx >= 0 && idx < DSD_TRUNK_CC_CANDIDATES_MAX) ? cc->cool_until[idx] : 0.0;
            if (cool_until > 0.0 && now_monotonic_s < cool_until) {
                continue;
            }
            *out_freq_hz = f;
            cc->used++;
            return 1;
        }
    }
    return 0;
}

void
dsd_trunk_cc_candidates_set_cooldown(dsd_state* state, long freq_hz, double until_monotonic_s) {
    if (!state || freq_hz == 0) {
        return;
    }

    dsd_trunk_cc_candidates* cc =
        DSD_STATE_EXT_GET_AS(dsd_trunk_cc_candidates, state, DSD_STATE_EXT_ENGINE_TRUNK_CC_CANDIDATES);
    if (!cc) {
        return;
    }

    for (int i = 0; i < cc->count && i < DSD_TRUNK_CC_CANDIDATES_MAX; i++) {
        if (cc->candidates[i] == freq_hz) {
            cc->cool_until[i] = until_monotonic_s;
            return;
        }
    }
}
