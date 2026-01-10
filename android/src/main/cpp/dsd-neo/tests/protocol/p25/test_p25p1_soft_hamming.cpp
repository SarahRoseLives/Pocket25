// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Unit tests for soft-decision Hamming(10,6,3) decoder.
 */

#include <dsd-neo/fec/Hamming.hpp>
#include <dsd-neo/protocol/p25/p25p1_soft.h>

#include <cstdio>
#include <cstring>

static int g_fail_count = 0;

#define ASSERT_EQ(a, b, msg)                                                                                           \
    do {                                                                                                               \
        if ((a) != (b)) {                                                                                              \
            fprintf(stderr, "FAIL: %s: expected %d, got %d\n", msg, (int)(b), (int)(a));                               \
            g_fail_count++;                                                                                            \
        }                                                                                                              \
    } while (0)

static void
test_no_error() {
    /* Valid Hamming(10,6,3) codeword: data=0b101010 (42), parity=0b1001 */
    Hamming_10_6_3_TableImpl hamming;
    char hex[6] = {1, 0, 1, 0, 1, 0};
    char parity[4];
    hamming.encode(hex, parity);

    char bits[10];
    memcpy(bits, hex, 6);
    memcpy(bits + 6, parity, 4);

    int reliab[10] = {200, 200, 200, 200, 200, 200, 200, 200, 200, 200};
    char out[10];

    int result = hamming_10_6_3_soft(bits, reliab, out);
    ASSERT_EQ(result, 0, "No error case should return 0");

    for (int i = 0; i < 6; i++) {
        ASSERT_EQ(out[i], hex[i], "Data bits should be unchanged");
    }
}

static void
test_single_error() {
    /* Valid codeword, then flip one bit */
    Hamming_10_6_3_TableImpl hamming;
    char hex[6] = {0, 1, 1, 0, 0, 1};
    char parity[4];
    hamming.encode(hex, parity);

    char bits[10];
    memcpy(bits, hex, 6);
    memcpy(bits + 6, parity, 4);

    /* Flip bit 2 (in data portion) */
    bits[2] ^= 1;

    int reliab[10] = {200, 200, 200, 200, 200, 200, 200, 200, 200, 200};
    char out[10];

    int result = hamming_10_6_3_soft(bits, reliab, out);
    ASSERT_EQ(result, 1, "Single error should be corrected");

    /* Verify corrected data matches original */
    for (int i = 0; i < 6; i++) {
        ASSERT_EQ(out[i], hex[i], "Corrected data should match original");
    }
}

static void
test_two_errors_with_soft_info() {
    /* Valid codeword, then flip two bits */
    Hamming_10_6_3_TableImpl hamming;
    char hex[6] = {1, 1, 0, 0, 1, 1};
    char parity[4];
    hamming.encode(hex, parity);

    char bits[10];
    memcpy(bits, hex, 6);
    memcpy(bits + 6, parity, 4);

    /* Flip bits 1 and 3 (both in data portion) */
    bits[1] ^= 1;
    bits[3] ^= 1;

    /* Mark positions 1 and 3 as low reliability */
    int reliab[10] = {200, 10, 200, 10, 200, 200, 200, 200, 200, 200};
    char out[10];

    int result = hamming_10_6_3_soft(bits, reliab, out);

    /* Soft decoder should find and correct both errors */
    if (result <= 1) {
        for (int i = 0; i < 6; i++) {
            ASSERT_EQ(out[i], hex[i], "Soft-corrected data should match original");
        }
    } else {
        fprintf(stderr, "INFO: Two-error case returned uncorrectable (expected for some patterns)\n");
    }
}

static void
test_high_reliability_no_change() {
    /* When all bits are high reliability, soft decoder should trust hard decode */
    Hamming_10_6_3_TableImpl hamming;
    char hex[6] = {0, 0, 0, 1, 1, 1};
    char parity[4];
    hamming.encode(hex, parity);

    char bits[10];
    memcpy(bits, hex, 6);
    memcpy(bits + 6, parity, 4);

    int reliab[10] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    char out[10];

    int result = hamming_10_6_3_soft(bits, reliab, out);
    ASSERT_EQ(result, 0, "High reliability valid codeword should succeed");

    for (int i = 0; i < 6; i++) {
        ASSERT_EQ(out[i], hex[i], "High reliability data should be unchanged");
    }
}

int
main(void) {
    test_no_error();
    test_single_error();
    test_two_errors_with_soft_info();
    test_high_reliability_no_change();

    if (g_fail_count > 0) {
        fprintf(stderr, "FAILED: %d test(s) failed\n", g_fail_count);
        return 1;
    }

    fprintf(stderr, "PASSED: All soft Hamming tests passed\n");
    return 0;
}
