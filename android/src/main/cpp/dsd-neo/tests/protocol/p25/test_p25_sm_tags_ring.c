// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 SM tag ring buffer semantics:
 * - empty and invalid tag inputs leave ring untouched
 * - p25_sm_log_status appends tags with head as a monotonically increasing cursor
 * - ring holds the last 8 tags in FIFO order.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_sm_ui.h>

static int
expect_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

static int
expect_str(const char* tag, const char* got, const char* want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "%s: got \"%s\" want \"%s\"\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    static dsd_opts opts;
    static dsd_state st;
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);

    /* Initial ring is empty. */
    rc |= expect_int("init count", st.p25_sm_tag_count, 0);
    rc |= expect_int("init head", st.p25_sm_tag_head, 0);

    /* Null/empty tags should not modify the ring. */
    p25_sm_log_status(&opts, &st, NULL);
    p25_sm_log_status(&opts, &st, "");
    rc |= expect_int("no-op count", st.p25_sm_tag_count, 0);
    rc |= expect_int("no-op head", st.p25_sm_tag_head, 0);

    /* Push more than capacity and verify that only the last 8 tags remain. */
    const int N = 10;
    char buf[16];
    for (int i = 0; i < N; i++) {
        snprintf(buf, sizeof buf, "T%d", i);
        p25_sm_log_status(&opts, &st, buf);
    }

    rc |= expect_int("count saturated", st.p25_sm_tag_count, 8);
    rc |= expect_int("head advanced", st.p25_sm_tag_head, N);

    /* Reconstruct oldest->newest order using the same modulo scheme as UI. */
    int head = st.p25_sm_tag_head;
    int len = st.p25_sm_tag_count;
    for (int k = 0; k < len; k++) {
        int idx = head - len + k;
        idx %= 8;
        if (idx < 0) {
            idx += 8;
        }
        const char* got = st.p25_sm_tags[idx];
        int expected_index = N - len + k; /* expect T2..T9 when N=10, len=8 */
        char want[16];
        snprintf(want, sizeof want, "T%d", expected_index);
        rc |= expect_str("ring order", got, want);
    }

    /* Last reason should reflect the most recent tag. */
    rc |= expect_str("last reason", st.p25_sm_last_reason, "T9");
    if (st.p25_sm_last_reason_time == 0) {
        fprintf(stderr, "last reason time not set\n");
        rc |= 1;
    }

    /* Basic sanity: head/time are monotonic. */
    time_t now = time(NULL);
    if (st.p25_sm_last_reason_time > now + 5) {
        fprintf(stderr, "last reason time in the future\n");
        rc |= 1;
    }

    return rc;
}
