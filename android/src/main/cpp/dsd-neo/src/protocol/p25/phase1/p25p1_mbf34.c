// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 1 Confirmed Data (3/4) decoder (MBF)
 *
 * This implementation mirrors the lightweight 3/4 trellis decoder used in
 * our DMR path, adapted for P25 MBF Confirmed Data blocks. It expects 98
 * dibits and produces 18 bytes per block laid out as:
 *
 *  byte[0]: [DBSN(7 bits, MSB..bit1)] | [CRC9 MSB at bit0]
 *  byte[1]: [CRC9 low 8 bits]
 *  byte[2..17]: 16 bytes (128 bits) of payload
 *
 * Interleave schedule and trellis are currently aligned with the known-good
 * DMR approach. If TIA-102 MBF interleaver differs, update the interleave[]
 * table below to match. The finite-state machine (fsm) and constellation
 * permutation used here are consistent with the 3/4 coding used across
 * several 4FSK systems.
 */

#include <dsd-neo/protocol/p25/p25p1_mbf34.h>

#include <string.h>

// P25 MBF 3/4 dibit deinterleave schedule (placeholder: matches DMR 3/4)
static const uint8_t p25_mbf34_interleave[98] = {
    0,  1,  8,  9,  16, 17, 24, 25, 32, 33, 40, 41, 48, 49, 56, 57, 64, 65, 72, 73, 80, 81, 88, 89, 96,
    97, 2,  3,  10, 11, 18, 19, 26, 27, 34, 35, 42, 43, 50, 51, 58, 59, 66, 67, 74, 75, 82, 83, 90, 91,
    4,  5,  12, 13, 20, 21, 28, 29, 36, 37, 44, 45, 52, 53, 60, 61, 68, 69, 76, 77, 84, 85, 92, 93, 6,
    7,  14, 15, 22, 23, 30, 31, 38, 39, 46, 47, 54, 55, 62, 63, 70, 71, 78, 79, 86, 87, 94, 95};

// Dibit-pair nibble to constellation point permutation (bijective)
static const uint8_t p25_constellation_map[16] = {11, 12, 0, 7, 14, 9, 5, 2, 10, 13, 1, 6, 15, 8, 4, 3};

// Finite-state machine mapping: (state*8 + tribit) -> constellation point
static const uint8_t p25_fsm[64] = {0, 8,  4, 12, 2, 10, 6, 14, 4, 12, 2, 10, 6, 14, 0, 8, 1, 9,  5, 13, 3, 11,
                                    7, 15, 5, 13, 3, 11, 7, 15, 1, 9,  3, 11, 7, 15, 1, 9, 5, 13, 7, 15, 1, 9,
                                    5, 13, 3, 11, 2, 10, 6, 14, 0, 8,  4, 12, 6, 14, 0, 8, 4, 12, 2, 10};

// Attempt to find a surviving/best path given a local error at position
static uint8_t
p25_fix34(uint8_t* p, uint8_t state, int position) {
    int i, j, k, counter, best_p, best_v, survivors;
    uint8_t temp_s, tri, t;

    int s[8];
    memset(s, 0, sizeof(s));

    uint8_t temp_p[8];
    temp_p[0] = (p[position] ^ 1) & 0xF;
    temp_p[1] = (p[position] ^ 3) & 0xF;
    temp_p[2] = (p[position] ^ 5) & 0xF;
    temp_p[3] = (p[position] ^ 7) & 0xF;
    temp_p[4] = (p[position] ^ 9) & 0xF;
    temp_p[5] = (p[position] ^ 11) & 0xF;
    temp_p[6] = (p[position] ^ 13) & 0xF;
    temp_p[7] = (p[position] ^ 15) & 0xF;

    best_p = 0;
    best_v = 0;

    for (k = 0; k < 8; k++) {
        temp_s = state;
        counter = 0;
        tri = 0;
        for (i = position; i < 49; i++) {
            t = (i == position) ? temp_p[k] : p[i];
            if (tri != 0xFF) {
                tri = 0xFF;
                for (j = 0; j < 8; j++) {
                    if (p25_fsm[(temp_s * 8) + j] == t) {
                        tri = temp_s = (uint8_t)j;
                        counter++;
                        break;
                    }
                }
                if (counter > best_p) {
                    best_p = counter;
                    best_v = k;
                }
                if (i == 48) {
                    s[k] = 1;
                }
            }
        }
    }

    survivors = 0;
    for (k = 0; k < 8; k++) {
        if (s[k] == 1) {
            survivors++;
        }
    }
    (void)survivors; // debug aid if needed
    return temp_p[best_v];
}

int
p25_mbf34_decode(const uint8_t dibits[98], uint8_t out[18]) {
    if (!dibits || !out) {
        return -1;
    }

    uint32_t irr_err = 0;

    uint8_t deint[98];
    memset(deint, 0, sizeof(deint));
    for (int i = 0; i < 98; i++) {
        deint[p25_mbf34_interleave[i]] = dibits[i];
    }

    uint8_t nibs[49];
    memset(nibs, 0, sizeof(nibs));
    for (int i = 0; i < 49; i++) {
        nibs[i] = (uint8_t)((deint[i * 2 + 0] << 2) | (deint[i * 2 + 1]));
    }

    uint8_t point[49];
    memset(point, 0xFF, sizeof(point));
    for (int i = 0; i < 49; i++) {
        point[i] = p25_constellation_map[nibs[i] & 0xF];
    }

    uint8_t state = 0;
    uint32_t tribits[49];
    memset(tribits, 0xF, sizeof(tribits));

    for (int i = 0; i < 49; i++) {
        for (int j = 0; j < 8; j++) {
            if (p25_fsm[(state * 8) + j] == point[i]) {
                tribits[i] = state = (uint8_t)j;
                break;
            }
        }
        if (tribits[i] > 7) {
            irr_err++;
            point[i] = p25_fix34(point, state, i);
            i--;
        }
    }

    // Pack first 48 tribits into 18 bytes (24 bits per 8-tribit group)
    uint32_t tmp;
    for (int i = 0; i < 6; i++) {
        tmp = (tribits[(i * 8) + 0] << 21) | (tribits[(i * 8) + 1] << 18) | (tribits[(i * 8) + 2] << 15)
              | (tribits[(i * 8) + 3] << 12) | (tribits[(i * 8) + 4] << 9) | (tribits[(i * 8) + 5] << 6)
              | (tribits[(i * 8) + 6] << 3) | (tribits[(i * 8) + 7] << 0);
        out[(i * 3) + 0] = (uint8_t)((tmp >> 16) & 0xFF);
        out[(i * 3) + 1] = (uint8_t)((tmp >> 8) & 0xFF);
        out[(i * 3) + 2] = (uint8_t)((tmp >> 0) & 0xFF);
    }

    (void)irr_err; // could be used for telemetry
    return 0;
}
