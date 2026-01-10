// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * UI/status tagging helpers for the P25 trunking state machine.
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_sm_ui.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

void
p25_sm_log_status(dsd_opts* opts, dsd_state* state, const char* tag) {
    if (!opts || !state) {
        return;
    }
    if (tag && tag[0] != '\0') {
        snprintf(state->p25_sm_last_reason, sizeof state->p25_sm_last_reason, "%s", tag);
        state->p25_sm_last_reason_time = time(NULL);
        int idx = state->p25_sm_tag_head % 8;
        snprintf(state->p25_sm_tags[idx], sizeof state->p25_sm_tags[idx], "%s", tag);
        state->p25_sm_tag_time[idx] = state->p25_sm_last_reason_time;
        state->p25_sm_tag_head++;
        if (state->p25_sm_tag_count < 8) {
            state->p25_sm_tag_count++;
        }
    }
    if (opts->verbose > 1) {
        const dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_peek(state);
        const unsigned int cc_added = cc ? cc->added : 0;
        const unsigned int cc_used = cc ? cc->used : 0;
        const int cc_count = (cc && cc->count > 0 && cc->count <= DSD_TRUNK_CC_CANDIDATES_MAX) ? cc->count : 0;
        const int cc_idx = cc ? cc->idx : 0;

        fprintf(stderr, "\n  P25 SM: %s tunes=%u rel=%u/%u cc_cand add=%u used=%u count=%d idx=%d\n",
                tag ? tag : "status", state->p25_sm_tune_count, state->p25_sm_release_count,
                state->p25_sm_cc_return_count, cc_added, cc_used, cc_count, cc_idx);
    }
}
