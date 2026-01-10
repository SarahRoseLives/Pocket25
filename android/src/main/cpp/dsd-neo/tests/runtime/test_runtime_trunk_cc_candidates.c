// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>

static void
test_add_dedup_rollover(void) {
    dsd_state* st = calloc(1, sizeof(*st));
    assert(st != NULL);

    assert(dsd_trunk_cc_candidates_peek(st) == NULL);

    assert(dsd_trunk_cc_candidates_add(st, 100, 0) == 1);
    const dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_peek(st);
    assert(cc != NULL);
    assert(cc->count == 1);
    assert(cc->candidates[0] == 100);
    assert(cc->added == 0);

    assert(dsd_trunk_cc_candidates_add(st, 100, 1) == 0);
    assert(cc->added == 0);

    assert(dsd_trunk_cc_candidates_add(st, 200, 1) == 1);
    assert(cc->count == 2);
    assert(cc->candidates[0] == 100);
    assert(cc->candidates[1] == 200);
    assert(cc->added == 1);

    dsd_state* st2 = calloc(1, sizeof(*st2));
    assert(st2 != NULL);
    for (long f = 1; f <= DSD_TRUNK_CC_CANDIDATES_MAX; f++) {
        assert(dsd_trunk_cc_candidates_add(st2, f, 0) == 1);
    }
    dsd_trunk_cc_candidates* cc2 = dsd_trunk_cc_candidates_get(st2);
    assert(cc2 != NULL);
    assert(cc2->count == DSD_TRUNK_CC_CANDIDATES_MAX);
    assert(cc2->candidates[0] == 1);
    assert(cc2->candidates[DSD_TRUNK_CC_CANDIDATES_MAX - 1] == DSD_TRUNK_CC_CANDIDATES_MAX);

    cc2->idx = 5;
    assert(dsd_trunk_cc_candidates_add(st2, 17, 0) == 1);
    assert(cc2->count == DSD_TRUNK_CC_CANDIDATES_MAX);
    assert(cc2->candidates[0] == 2);
    assert(cc2->candidates[DSD_TRUNK_CC_CANDIDATES_MAX - 2] == 16);
    assert(cc2->candidates[DSD_TRUNK_CC_CANDIDATES_MAX - 1] == 17);
    assert(cc2->idx == 4);

    dsd_state_ext_free_all(st2);
    free(st2);
    dsd_state_ext_free_all(st);
    free(st);
}

static void
test_next_and_cooldown(void) {
    dsd_state* st = calloc(1, sizeof(*st));
    assert(st != NULL);
    assert(dsd_trunk_cc_candidates_add(st, 100, 0) == 1);
    assert(dsd_trunk_cc_candidates_add(st, 200, 0) == 1);

    dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_get(st);
    assert(cc != NULL);

    // Skip current CC frequency when set
    st->trunk_cc_freq = 100;
    cc->idx = 0;
    cc->used = 0;
    long out = 0;
    assert(dsd_trunk_cc_candidates_next(st, 0.0, &out) == 1);
    assert(out == 200);
    assert(cc->used == 1);

    // Cooldown skips a candidate until it expires
    st->trunk_cc_freq = 0;
    st->p25_cc_freq = 0;
    cc->idx = 0;
    cc->used = 0;
    dsd_trunk_cc_candidates_set_cooldown(st, 100, 10.0);

    out = 0;
    assert(dsd_trunk_cc_candidates_next(st, 0.0, &out) == 1);
    assert(out == 200);
    assert(cc->used == 1);

    out = 0;
    assert(dsd_trunk_cc_candidates_next(st, 0.0, &out) == 1);
    assert(out == 200);
    assert(cc->used == 2);

    out = 0;
    assert(dsd_trunk_cc_candidates_next(st, 11.0, &out) == 1);
    assert(out == 100);
    assert(cc->used == 3);

    dsd_state_ext_free_all(st);
    free(st);
}

int
main(void) {
    test_add_dedup_rollover();
    test_next_and_cooldown();
    return 0;
}
