// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25p2 audio jitter ring helpers:
 * - reset clears head/tail/count and zeroes frames
 * - push/pop maintain FIFO order for up to 3 frames
 * - overflow drops the oldest frame (bounded latency)
 * - pop from empty returns zeros and 0 status.
 */

#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/p25_p2_audio_ring.h>

static int
expect_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

static int
expect_frame(const char* tag, const float* got, const float* want) {
    for (int i = 0; i < 160; i++) {
        if (got[i] != want[i]) {
            fprintf(stderr, "%s: sample %d mismatch (got %.3f want %.3f)\n", tag, i, got[i], want[i]);
            return 1;
        }
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    static dsd_state st;
    memset(&st, 0, sizeof st);

    /* Reset both slots and verify counters. */
    p25_p2_audio_ring_reset(&st, -1);
    rc |= expect_int("reset both count0", st.p25_p2_audio_ring_count[0], 0);
    rc |= expect_int("reset both count1", st.p25_p2_audio_ring_count[1], 0);

    /* Slot 0 basic FIFO semantics. */
    float f0[160];
    float f1[160];
    float f2[160];
    float f3[160];
    for (int i = 0; i < 160; i++) {
        f0[i] = 10.0f + (float)i;
        f1[i] = 20.0f + (float)i;
        f2[i] = 30.0f + (float)i;
        f3[i] = 40.0f + (float)i;
    }

    p25_p2_audio_ring_reset(&st, 0);
    rc |= expect_int("reset slot0 count", st.p25_p2_audio_ring_count[0], 0);

    rc |= expect_int("push f0", p25_p2_audio_ring_push(&st, 0, f0), 1);
    rc |= expect_int("push f1", p25_p2_audio_ring_push(&st, 0, f1), 1);
    rc |= expect_int("push f2", p25_p2_audio_ring_push(&st, 0, f2), 1);
    rc |= expect_int("count after 3 pushes", st.p25_p2_audio_ring_count[0], 3);

    float out[160];
    memset(out, 0, sizeof out);
    rc |= expect_int("pop f0 ok", p25_p2_audio_ring_pop(&st, 0, out), 1);
    rc |= expect_frame("pop f0 frame", out, f0);
    rc |= expect_int("count after pop1", st.p25_p2_audio_ring_count[0], 2);

    memset(out, 0, sizeof out);
    rc |= expect_int("pop f1 ok", p25_p2_audio_ring_pop(&st, 0, out), 1);
    rc |= expect_frame("pop f1 frame", out, f1);
    rc |= expect_int("count after pop2", st.p25_p2_audio_ring_count[0], 1);

    memset(out, 0, sizeof out);
    rc |= expect_int("pop f2 ok", p25_p2_audio_ring_pop(&st, 0, out), 1);
    rc |= expect_frame("pop f2 frame", out, f2);
    rc |= expect_int("count after pop3", st.p25_p2_audio_ring_count[0], 0);

    /* Pop from empty should return 0 and zero-fill out buffer. */
    for (int i = 0; i < 160; i++) {
        out[i] = 123.0f;
    }
    rc |= expect_int("pop empty", p25_p2_audio_ring_pop(&st, 0, out), 0);
    for (int i = 0; i < 160; i++) {
        if (out[i] != 0.0f) {
            fprintf(stderr, "pop empty: out[%d]=%.3f not zero\n", i, out[i]);
            rc |= 1;
            break;
        }
    }

    /* Overflow: push 4 frames; ring keeps last 3 (f1,f2,f3). */
    p25_p2_audio_ring_reset(&st, 0);
    rc |= expect_int("reset slot0 count (2)", st.p25_p2_audio_ring_count[0], 0);

    p25_p2_audio_ring_push(&st, 0, f0);
    p25_p2_audio_ring_push(&st, 0, f1);
    p25_p2_audio_ring_push(&st, 0, f2);
    p25_p2_audio_ring_push(&st, 0, f3); /* should evict f0 */
    rc |= expect_int("count after overflow pushes", st.p25_p2_audio_ring_count[0], 3);

    memset(out, 0, sizeof out);
    rc |= expect_int("pop f1 ok (overflow)", p25_p2_audio_ring_pop(&st, 0, out), 1);
    rc |= expect_frame("pop f1 frame (overflow)", out, f1);

    memset(out, 0, sizeof out);
    rc |= expect_int("pop f2 ok (overflow)", p25_p2_audio_ring_pop(&st, 0, out), 1);
    rc |= expect_frame("pop f2 frame (overflow)", out, f2);

    memset(out, 0, sizeof out);
    rc |= expect_int("pop f3 ok (overflow)", p25_p2_audio_ring_pop(&st, 0, out), 1);
    rc |= expect_frame("pop f3 frame (overflow)", out, f3);
    rc |= expect_int("count after draining overflow", st.p25_p2_audio_ring_count[0], 0);

    return rc;
}
