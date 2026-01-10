// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25p2 FACCH RS(63,35) decode vectors
 */

#include <stdio.h>
#include <string.h>

// Provided by src/fec/ez.cpp via dsd-neo_fec
int ez_rs28_facch(int payload[156], int parity[114]);

static int
expect_eq_bit(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int payload[156];
    int parity[114];
    memset(payload, 0, sizeof(payload));
    memset(parity, 0, sizeof(parity));

    // Vector 1: all-zero codeword (systematic all-zeros is valid)
    int rc = ez_rs28_facch(payload, parity);
    if (rc < 0) {
        fprintf(stderr, "FACCH RS decode failed on all-zero codeword (rc=%d)\n", rc);
        return 1;
    }
    // Ensure payload remains all zeros
    for (int i = 0; i < 156; i++) {
        if (expect_eq_bit("zero payload", payload[i], 0)) {
            return 2;
        }
    }

    // Vector 2: single-bit error in one 6-bit symbol (should be correctable)
    memset(payload, 0, sizeof(payload));
    memset(parity, 0, sizeof(parity));
    payload[5] ^= 1; // flip one bit in first symbol
    rc = ez_rs28_facch(payload, parity);
    if (rc < 0) {
        fprintf(stderr, "FACCH RS failed to correct single-bit error (rc=%d)\n", rc);
        return 3;
    }
    for (int i = 0; i < 156; i++) {
        if (expect_eq_bit("single-bit corrected", payload[i], 0)) {
            return 4;
        }
    }

    // Vector 3: exceed t=14 symbol errors (flip 16 distinct symbols) → expect failure
    memset(payload, 0, sizeof(payload));
    memset(parity, 0, sizeof(parity));
    for (int s = 0; s < 16; s++) {
        int bit = s * 6; // first bit of each symbol
        if (bit < 156) {
            payload[bit] ^= 1;
        }
    }
    rc = ez_rs28_facch(payload, parity);
    if (rc >= 0) {
        fprintf(stderr, "FACCH RS unexpectedly succeeded with >t symbol errors (rc=%d)\n", rc);
        return 5;
    }

    fprintf(stderr, "P25p2 FACCH RS decode vectors passed\n");
    return 0;
}
