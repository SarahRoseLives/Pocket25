// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Determinism test for unvoiced noise synthesis.
 *
 * Verifies JMBE-compatible behavior:
 * 1. Same initial state produces identical output (determinism)
 * 2. LCG state advances per frame, producing different noise sequences
 *
 * Note: JMBE uses a fixed seed (3147) per synthesizer instance. The noise
 * generator state persists in mbe_parms and advances naturally per frame.
 * There is no external seeding mechanism for unvoiced noise in JMBE.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mbelib-neo/mbelib.h"

/**
 * @brief Initialize deterministic unvoiced parameter sets for testing.
 * @param cur  Output current parameters (unvoiced bands populated).
 * @param prev Output previous parameters (copy of current).
 */
static void
fill_params_unvoiced(mbe_parms* cur, mbe_parms* prev) {
    mbe_parms prev_enh;                     // unused but required by init
    mbe_initMbeParms(cur, prev, &prev_enh); // resets and copies prev->cur

    // Overwrite cur/prev with deterministic unvoiced setup
    cur->w0 = 0.10f;
    cur->L = 24;
    for (int l = 1; l <= cur->L; ++l) {
        cur->Vl[l] = 0; // unvoiced
        cur->Ml[l] = 0.04f + 0.001f * (float)l;
        cur->PHIl[l] = 0.0f; // not used for unvoiced
        cur->PSIl[l] = 0.0f;
    }
    *prev = *cur; // start from same state
}

/**
 * @brief Test entry: verifies deterministic noise synthesis.
 */
int
main(void) {
    float out1[160], out2[160], out3[160];
    mbe_parms cur1, prev1, cur2, prev2;

    /* Test 1: Determinism - same initial state produces identical output */
    fill_params_unvoiced(&cur1, &prev1);
    mbe_synthesizeSpeechf(out1, &cur1, &prev1, 8);

    fill_params_unvoiced(&cur2, &prev2);
    mbe_synthesizeSpeechf(out2, &cur2, &prev2, 8);

    for (int i = 0; i < 160; ++i) {
        if (out1[i] != out2[i]) {
            fprintf(stderr, "FAIL: determinism - same state produced different output at sample %d\n", i);
            return 1;
        }
    }

    /* Test 2: State progression - consecutive frames produce different noise
     * (LCG state advances, so second frame should differ from first) */
    mbe_moveMbeParms(&cur1, &prev1); // advance: cur becomes prev for next frame
    mbe_synthesizeSpeechf(out3, &cur1, &prev1, 8);

    int diff = 0;
    for (int i = 0; i < 160; ++i) {
        if (out1[i] != out3[i]) {
            diff = 1;
            break;
        }
    }
    if (!diff) {
        fprintf(stderr, "FAIL: state progression - consecutive frames produced identical output\n");
        return 1;
    }

    /* Test 3: Verify noiseSeed actually advances */
    mbe_parms cur_check, prev_check;
    fill_params_unvoiced(&cur_check, &prev_check);
    float seed_before = cur_check.noiseSeed;
    mbe_synthesizeSpeechf(out1, &cur_check, &prev_check, 8);
    float seed_after = cur_check.noiseSeed;

    if (seed_before == seed_after) {
        fprintf(stderr, "FAIL: noiseSeed did not advance after synthesis\n");
        return 1;
    }

    return 0;
}
