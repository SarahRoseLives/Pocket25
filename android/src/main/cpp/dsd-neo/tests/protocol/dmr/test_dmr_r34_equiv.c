// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Test now validates that the Viterbi decoder is no worse than
// the legacy trellis search in terms of cumulative branch metric.
// It no longer requires byte-for-byte output equivalence.

#include <dsd-neo/protocol/dmr/r34_viterbi.h>

// Forward-declare legacy decoder to avoid including broad headers.
uint32_t dmr_34(uint8_t* input, uint8_t treturn[18]);

// Local copies of mapping tables used by both decoders, for metric computation.
static const uint8_t interleave_tbl[98] = {
    0,  1,  8,  9,  16, 17, 24, 25, 32, 33, 40, 41, 48, 49, 56, 57, 64, 65, 72, 73, 80, 81, 88, 89, 96,
    97, 2,  3,  10, 11, 18, 19, 26, 27, 34, 35, 42, 43, 50, 51, 58, 59, 66, 67, 74, 75, 82, 83, 90, 91,
    4,  5,  12, 13, 20, 21, 28, 29, 36, 37, 44, 45, 52, 53, 60, 61, 68, 69, 76, 77, 84, 85, 92, 93, 6,
    7,  14, 15, 22, 23, 30, 31, 38, 39, 46, 47, 54, 55, 62, 63, 70, 71, 78, 79, 86, 87, 94, 95};

static const uint8_t constellation_map_tbl[16] = {11, 12, 0, 7, 14, 9, 5, 2, 10, 13, 1, 6, 15, 8, 4, 3};

static const uint8_t fsm_tbl[64] = {0, 8,  4, 12, 2, 10, 6, 14, 4, 12, 2, 10, 6, 14, 0, 8, 1, 9,  5, 13, 3, 11,
                                    7, 15, 5, 13, 3, 11, 7, 15, 1, 9,  3, 11, 7, 15, 1, 9, 5, 13, 7, 15, 1, 9,
                                    5, 13, 3, 11, 2, 10, 6, 14, 0, 8,  4, 12, 6, 14, 0, 8, 4, 12, 2, 10};

static inline int
hamming4(uint8_t a, uint8_t b) {
    static const uint8_t pop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
    return (int)pop4[((a ^ b) & 0x0F)];
}

static void
deinterleave_and_points(const uint8_t* dibits, uint8_t point[49]) {
    uint8_t de[98];
    for (int i = 0; i < 98; i++) {
        de[interleave_tbl[i]] = (uint8_t)(dibits[i] & 0x3u);
    }
    for (int i = 0; i < 49; i++) {
        uint8_t d0 = de[i * 2 + 0] & 0x3u;
        uint8_t d1 = de[i * 2 + 1] & 0x3u;
        uint8_t nib = (uint8_t)((d0 << 2) | d1);
        point[i] = constellation_map_tbl[nib & 0x0F];
    }
}

static void
unpack_tribits48(const uint8_t bytes18[18], uint8_t tribits[48]) {
    for (int g = 0; g < 6; g++) {
        uint32_t temp = ((uint32_t)bytes18[g * 3 + 0] << 16) | ((uint32_t)bytes18[g * 3 + 1] << 8)
                        | ((uint32_t)bytes18[g * 3 + 2] << 0);
        for (int k = 7; k >= 0; k--) {
            tribits[g * 8 + (7 - k)] = (uint8_t)((temp >> (k * 3)) & 0x7u);
        }
    }
}

static int
path_cost48(const uint8_t points[49], const uint8_t tribits48[48]) {
    int cost = 0;
    uint8_t state = 0;
    for (int t = 0; t < 48; t++) {
        uint8_t tri = tribits48[t] & 0x7u;
        uint8_t expect = fsm_tbl[state * 8 + tri];
        cost += hamming4(expect, points[t]);
        state = tri;
    }
    // best possible last step against point[48]
    int best_last = 1000;
    for (int tri = 0; tri < 8; tri++) {
        int c = hamming4(fsm_tbl[state * 8 + (uint8_t)tri], points[48]);
        if (c < best_last) {
            best_last = c;
        }
    }
    return cost + best_last;
}

static void
gen_pattern(uint8_t* dibits, int len, unsigned seed) {
    // Simple LCG to fill 2-bit dibits deterministically
    uint32_t x = seed ? seed : 1u;
    for (int i = 0; i < len; i++) {
        x = x * 1103515245u + 12345u;
        dibits[i] = (uint8_t)((x >> 24) & 0x03u);
    }
}

static void
run_case(unsigned seed) {
    uint8_t in[98];
    uint8_t a[18], b[18];
    gen_pattern(in, 98, seed);
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    // Legacy
    uint8_t in_copy[98];
    for (int i = 0; i < 98; i++) {
        in_copy[i] = in[i];
    }
    dmr_34(in_copy, a);
    // New
    int rc = dmr_r34_viterbi_decode(in, b);
    assert(rc == 0);
    // Compare path metrics: Viterbi must be no worse than legacy
    uint8_t points[49];
    deinterleave_and_points(in, points);
    uint8_t tri_a[48], tri_b[48];
    unpack_tribits48(a, tri_a);
    unpack_tribits48(b, tri_b);
    int cost_a = path_cost48(points, tri_a);
    int cost_b = path_cost48(points, tri_b);
    assert(cost_b <= cost_a);
}

int
main(void) {
    for (unsigned s = 0; s < 8; s++) {
        run_case(0xC0FFEEu + s);
    }
    printf("DMR R3/4 Viterbi metric <= legacy: OK\n");
    return 0;
}
