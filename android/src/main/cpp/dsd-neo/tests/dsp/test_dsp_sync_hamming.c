// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/dsp/sync_hamming.h>

#include <stdio.h>

static int
expect_int_eq(const char* label, int actual, int expected) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s: got %d expected %d\n", label, actual, expected);
        return 0;
    }
    return 1;
}

int
main(void) {
    int ok = 1;

    ok &= expect_int_eq("hamming: identical", dsd_sync_hamming_distance("0123", "0123", 4), 0);
    ok &= expect_int_eq("hamming: one mismatch", dsd_sync_hamming_distance("0122", "0123", 4), 1);

    const char buf_num_ok[] = {0, 1, 2, 3};
    const char buf_num_bad[] = {0, 1, 2, 2};
    ok &= expect_int_eq("hamming: numeric identical", dsd_sync_hamming_distance(buf_num_ok, "0123", 4), 0);
    ok &= expect_int_eq("hamming: numeric one mismatch", dsd_sync_hamming_distance(buf_num_bad, "0123", 4), 1);

    const char* pat = "0123";
    ok &= expect_int_eq("remap: invert", dsd_qpsk_sync_hamming_with_remaps("2301", pat, pat, 4), 0);
    ok &= expect_int_eq("remap: swap", dsd_qpsk_sync_hamming_with_remaps("0213", pat, pat, 4), 0);
    ok &= expect_int_eq("remap: xor3", dsd_qpsk_sync_hamming_with_remaps("3210", pat, pat, 4), 0);
    ok &= expect_int_eq("remap: rot", dsd_qpsk_sync_hamming_with_remaps("2031", pat, pat, 4), 0);

    return ok ? 0 : 1;
}
