// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 2 RS(63,35) wrapper tests for FACCH and SACCH.
 *
 * Build valid shortened codewords using ezpwd and feed through the wrapper
 * bit mappers (ez_rs28_facch / ez_rs28_sacch). Expect decode success.
 */

#include <exception>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "ezpwd/rs"

extern "C" {
int ez_rs28_facch(int payload[156], int parity[114]);
int ez_rs28_sacch(int payload[180], int parity[132]);
}

static void
sym_to_bits6(uint8_t s, int* out6) {
    for (int i = 0; i < 6; i++) {
        out6[i] = (int)((s >> (5 - i)) & 1);
    }
}

static int
expect_ge(const char* tag, int got, int minv) {
    if (got < minv) {
        fprintf(stderr, "%s: got %d < %d\n", tag, got, minv);
        return 1;
    }
    return 0;
}

int
main(void) try {
    int rc = 0;
    ezpwd::RS<63, 35> rs;

    // Build a systematic codeword (data || parity)
    std::vector<uint8_t> data(35), parity(28);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = (uint8_t)((i * 7 + 3) & 0x3F);
    }
    rs.encode(data, parity);
    std::vector<uint8_t> cw;
    cw.reserve(63);
    cw.insert(cw.end(), data.begin(), data.end());
    cw.insert(cw.end(), parity.begin(), parity.end());

    // FACCH: map cw[0..25] → payload bits (26 syms), cw[26..44] → parity bits (19 syms)
    {
        int payload[156] = {0};
        int pbits = 0;
        for (int i = 0; i < 26; i++) { // 26 symbols
            int bits6[6];
            sym_to_bits6(cw[i], bits6);
            for (int b = 0; b < 6; b++) {
                payload[pbits++] = bits6[b];
            }
        }
        int parity_bits[114] = {0};
        int qbits = 0;
        for (int i = 26; i < 45; i++) { // 19 symbols
            int bits6[6];
            sym_to_bits6(cw[i], bits6);
            for (int b = 0; b < 6; b++) {
                parity_bits[qbits++] = bits6[b];
            }
        }
        int ec = ez_rs28_facch(payload, parity_bits);
        rc |= expect_ge("FACCH decode ec", ec, 0);
    }

    // SACCH: map cw[0..29] → payload bits (30 syms), cw[30..51] → parity bits (22 syms)
    {
        int payload[180] = {0};
        int pbits = 0;
        for (int i = 0; i < 30; i++) {
            int bits6[6];
            sym_to_bits6(cw[i], bits6);
            for (int b = 0; b < 6; b++) {
                payload[pbits++] = bits6[b];
            }
        }
        int parity_bits[132] = {0};
        int qbits = 0;
        for (int i = 30; i < 52; i++) {
            int bits6[6];
            sym_to_bits6(cw[i], bits6);
            for (int b = 0; b < 6; b++) {
                parity_bits[qbits++] = bits6[b];
            }
        }
        int ec = ez_rs28_sacch(payload, parity_bits);
        rc |= expect_ge("SACCH decode ec", ec, 0);
    }

    return rc;
} catch (const std::exception& e) {

    fprintf(stderr, "Unhandled exception: %s\n", e.what());
    return 1;
} catch (...) {
    fprintf(stderr, "Unhandled exception\n");
    return 1;
}
