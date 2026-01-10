// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
 * ez.cpp
 * EZPWD R-S bridge and ISCH map lookup
 *
 * original copyrights for portions used below (OP25, EZPWD)
 *
 * LWVMOBILE
 * 2022-09 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/platform/posix_compat.h>
#include <unordered_map>
#include "ezpwd/rs"

extern "C" {
int ez_rs28_ess(int payload[96], int parity[168]);
int ez_rs28_facch(int payload[156], int parity[114]);
int ez_rs28_sacch(int payload[180], int parity[132]);
int ez_rs28_ess_soft(int payload[96], int parity[168], const int* erasures, int n_erasures);
int ez_rs28_facch_soft(int payload[156], int parity[114], const int* erasures, int n_erasures);
int ez_rs28_sacch_soft(int payload[180], int parity[132], const int* erasures, int n_erasures);
int isch_lookup(uint64_t isch);
}

std::vector<uint8_t> ESS_A(28, 0); // ESS_A and ESS_B are hexbits vectors
std::vector<uint8_t> ESS_B(16, 0);
ezpwd::RS<63, 35> rs28;
std::vector<uint8_t> HB(63, 0);
std::vector<uint8_t> HBS(63, 0);
std::vector<uint8_t> HBL(63, 0);
std::vector<int> Erasures;

//Reed-Solomon Correction of ESS section
int
ez_rs28_ess(int payload[96], int parity[168]) {
    //do something

    uint8_t a, b, i, j, k;
    k = 0;
    for (i = 0; i < 16; i++) {
        b = 0;
        for (j = 0; j < 6; j++) {
            b = b << 1;
            b = b + payload[k]; //convert bits to hexbits.
            k++;
        }
        ESS_B[i] = b;
        // fprintf (stderr, " %X", ESS_B[i]);
    }

    k = 0;
    for (i = 0; i < 28; i++) {
        a = 0;
        for (j = 0; j < 6; j++) {
            a = a << 1;
            a = a + parity[k]; //convert bits to hexbits.
            k++;
        }
        ESS_A[i] = a;
        // fprintf (stderr, " %X ", ESS_A[i]);
    }

    int ec;

    ec = rs28.decode(ESS_B, ESS_A);
    // fprintf (stderr, "\n EC = %d \n", ec);

    //convert ESS_B back to bits
    k = 0;
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 6; j++) {
            b = (ESS_B[i] >> (5 - j) & 0x1);
            payload[k] = b;
            k++;
        }
        // fprintf (stderr, " %X", ESS_B[i]);
    }

    return (ec);
}

/**
 * Reed-Solomon correction of ESS section with dynamic erasures.
 *
 * ESS uses RS(44,16,29) over GF(64):
 *   - 16 payload hexbits (96 bits from ESS_B)
 *   - 28 parity hexbits (168 bits from ESS_A)
 *
 * The ezpwd RS<63,35> decoder maps ESS_B to positions 0-15 and ESS_A to 16-43.
 *
 * @param payload   96-bit payload array (ESS_B, converted to 16 hexbits).
 * @param parity    168-bit parity array (ESS_A, converted to 28 hexbits).
 * @param erasures  Erasure position array (RS symbol positions 0-43).
 * @param n_erasures Number of erasures (must be <= 28).
 * @return Correction count, or -1 on failure.
 */
int
ez_rs28_ess_soft(int payload[96], int parity[168], const int* erasures, int n_erasures) {
    int ec = -2;
    int i, j, k;
    uint8_t a, b;

    /* Build erasure vector from provided positions */
    std::vector<int> ErasuresDyn;
    for (i = 0; i < n_erasures && i < 28; i++) {
        ErasuresDyn.push_back(erasures[i]);
    }

    /* Convert payload bits to hexbits (16 hexbits = 96 bits) */
    k = 0;
    for (i = 0; i < 16; i++) {
        b = 0;
        for (j = 0; j < 6; j++) {
            b = b << 1;
            b = b + payload[k];
            k++;
        }
        ESS_B[i] = b;
    }

    /* Convert parity bits to hexbits (28 hexbits = 168 bits) */
    k = 0;
    for (i = 0; i < 28; i++) {
        a = 0;
        for (j = 0; j < 6; j++) {
            a = a << 1;
            a = a + parity[k];
            k++;
        }
        ESS_A[i] = a;
    }

    /* Decode with erasures */
    ec = rs28.decode(ESS_B, ESS_A, ErasuresDyn);

    /* Convert ESS_B back to bits */
    k = 0;
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 6; j++) {
            b = (ESS_B[i] >> (5 - j)) & 0x1;
            payload[k] = b;
            k++;
        }
    }

    return ec;
}

//Reed-Solomon Correction of FACCH section
int
ez_rs28_facch(int payload[156], int parity[114]) {
    //do something!
    int ec = -2;
    int i, j, k, b;

    //init HB
    for (i = 0; i < (int)HB.size(); i++) {
        HB[i] = 0;
    }

    //Erasures for FACCH
    Erasures = {0, 1, 2, 3, 4, 5, 6, 7, 8, 54, 55, 56, 57, 58, 59, 60, 61, 62};

    //convert bits to hexbits, 156 for payload, 114 parity
    j = 9; //starting position according to OP25
    for (i = 0; i < 156; i += 6) {
        HB[j] = (payload[i] << 5) + (payload[i + 1] << 4) + (payload[i + 2] << 3) + (payload[i + 3] << 2)
                + (payload[i + 4] << 1) + payload[i + 5];
        j++;
    }
    //j should continue from its last increment
    for (i = 0; i < 114; i += 6) {
        HB[j] = (parity[i] << 5) + (parity[i + 1] << 4) + (parity[i + 2] << 3) + (parity[i + 3] << 2)
                + (parity[i + 4] << 1) + parity[i + 5];
        j++;
    }

    ec = rs28.decode(HB, Erasures);

    //convert HB back to bits
    //fprintf (stderr, "\n");
    k = 0;
    for (i = 0; i < 26; i++) //26*6=156 bits
    {
        for (j = 0; j < 6; j++) {
            b = (HB[i + 9] >> (5 - j) & 0x1); //+9 to mach our starting position
            payload[k] = b;
            //fprintf (stderr, "%d", payload[k]);
            k++;
        }
    }

    return (ec);
}

//Reed-Solomon Correction of SACCH section
int
ez_rs28_sacch(int payload[180], int parity[132]) {
    //do something!
    int ec = -2;
    int i, j, k, b;

    //init HBS
    for (i = 0; i < (int)HBS.size(); i++) {
        HBS[i] = 0;
    }

    //Erasures for SACCH
    Erasures = {0, 1, 2, 3, 4, 57, 58, 59, 60, 61, 62};

    //convert bits to hexbits, 156 for payload, 114 parity
    j = 5; //starting position according to OP25
    for (i = 0; i < 180; i += 6) {
        HBS[j] = (payload[i] << 5) + (payload[i + 1] << 4) + (payload[i + 2] << 3) + (payload[i + 3] << 2)
                 + (payload[i + 4] << 1) + payload[i + 5];
        j++;
    }
    //j should continue from its last increment
    for (i = 0; i < 132; i += 6) {
        HBS[j] = (parity[i] << 5) + (parity[i + 1] << 4) + (parity[i + 2] << 3) + (parity[i + 3] << 2)
                 + (parity[i + 4] << 1) + parity[i + 5];
        j++;
    }

    ec = rs28.decode(HBS, Erasures);

    //convert HBS back to bits
    // fprintf (stderr, "\n");
    k = 0;
    for (i = 0; i < 30; i++) //30*6=180 bits
    {
        for (j = 0; j < 6; j++) {
            b = (HBS[i + 5] >> (5 - j) & 0x1); //+5 to mach our starting position
            payload[k] = b;
            // fprintf (stderr, "%d", payload[k]);
            k++;
        }
    }
    return (ec);
}

/**
 * Reed-Solomon correction of FACCH section with dynamic erasures.
 *
 * @param payload   156-bit payload array.
 * @param parity    114-bit parity array.
 * @param erasures  Erasure position array (RS symbol positions 0-62).
 * @param n_erasures Number of erasures (must be <=28).
 * @return Correction count, or -1 on failure.
 */
int
ez_rs28_facch_soft(int payload[156], int parity[114], const int* erasures, int n_erasures) {
    int ec = -2;
    int i, j, k, b;

    /* Initialize HB */
    for (i = 0; i < (int)HB.size(); i++) {
        HB[i] = 0;
    }

    /* Build erasure vector from provided positions */
    std::vector<int> ErasuresDyn;
    for (i = 0; i < n_erasures && i < 28; i++) {
        ErasuresDyn.push_back(erasures[i]);
    }

    /* Convert bits to hexbits (same as ez_rs28_facch) */
    j = 9;
    for (i = 0; i < 156; i += 6) {
        HB[j] = (payload[i] << 5) + (payload[i + 1] << 4) + (payload[i + 2] << 3) + (payload[i + 3] << 2)
                + (payload[i + 4] << 1) + payload[i + 5];
        j++;
    }
    for (i = 0; i < 114; i += 6) {
        HB[j] = (parity[i] << 5) + (parity[i + 1] << 4) + (parity[i + 2] << 3) + (parity[i + 3] << 2)
                + (parity[i + 4] << 1) + parity[i + 5];
        j++;
    }

    ec = rs28.decode(HB, ErasuresDyn);

    /* Convert back to bits */
    k = 0;
    for (i = 0; i < 26; i++) {
        for (j = 0; j < 6; j++) {
            b = (HB[i + 9] >> (5 - j)) & 0x1;
            payload[k] = b;
            k++;
        }
    }

    return ec;
}

/**
 * Reed-Solomon correction of SACCH section with dynamic erasures.
 *
 * @param payload   180-bit payload array.
 * @param parity    132-bit parity array.
 * @param erasures  Erasure position array (RS symbol positions 0-62).
 * @param n_erasures Number of erasures (must be <=28).
 * @return Correction count, or -1 on failure.
 */
int
ez_rs28_sacch_soft(int payload[180], int parity[132], const int* erasures, int n_erasures) {
    int ec = -2;
    int i, j, k, b;

    /* Initialize HBS */
    for (i = 0; i < (int)HBS.size(); i++) {
        HBS[i] = 0;
    }

    /* Build erasure vector from provided positions */
    std::vector<int> ErasuresDyn;
    for (i = 0; i < n_erasures && i < 28; i++) {
        ErasuresDyn.push_back(erasures[i]);
    }

    /* Convert bits to hexbits (same as ez_rs28_sacch) */
    j = 5;
    for (i = 0; i < 180; i += 6) {
        HBS[j] = (payload[i] << 5) + (payload[i + 1] << 4) + (payload[i + 2] << 3) + (payload[i + 3] << 2)
                 + (payload[i + 4] << 1) + payload[i + 5];
        j++;
    }
    for (i = 0; i < 132; i += 6) {
        HBS[j] = (parity[i] << 5) + (parity[i + 1] << 4) + (parity[i + 2] << 3) + (parity[i + 3] << 2)
                 + (parity[i + 4] << 1) + parity[i + 5];
        j++;
    }

    ec = rs28.decode(HBS, ErasuresDyn);

    /* Convert back to bits */
    k = 0;
    for (i = 0; i < 30; i++) {
        for (j = 0; j < 6; j++) {
            b = (HBS[i + 5] >> (5 - j)) & 0x1;
            payload[k] = b;
            k++;
        }
    }

    return ec;
}

/* One-time initialized ISCH lookup table (P25 (40,9,16) codewords) */
static inline const std::unordered_map<uint64_t, int>&
isch_table() {
    static const std::unordered_map<uint64_t, int> m = {
        {0x184229d461ULL, 0},   {0x18761451f6ULL, 1},   {0x181ae27e2fULL, 2},   {0x182edffbb8ULL, 3},
        {0x18df8a7510ULL, 4},   {0x18ebb7f087ULL, 5},   {0x188741df5eULL, 6},   {0x18b37c5ac9ULL, 7},
        {0x1146a44f13ULL, 8},   {0x117299ca84ULL, 9},   {0x111e6fe55dULL, 10},  {0x112a5260caULL, 11},
        {0x11db07ee62ULL, 12},  {0x11ef3a6bf5ULL, 13},  {0x1183cc442cULL, 14},  {0x11b7f1c1bbULL, 15},
        {0x1a4a2e239eULL, 16},  {0x1a7e13a609ULL, 17},  {0x1a12e589d0ULL, 18},  {0x1a26d80c47ULL, 19},
        {0x1ad78d82efULL, 20},  {0x1ae3b00778ULL, 21},  {0x1a8f4628a1ULL, 22},  {0x1abb7bad36ULL, 23},
        {0x134ea3b8ecULL, 24},  {0x137a9e3d7bULL, 25},  {0x13166812a2ULL, 26},  {0x1322559735ULL, 27},
        {0x13d300199dULL, 28},  {0x13e73d9c0aULL, 29},  {0x138bcbb3d3ULL, 30},  {0x13bff63644ULL, 31},
        {0x1442f705efULL, 32},  {0x1476ca8078ULL, 33},  {0x141a3cafa1ULL, 34},  {0x142e012a36ULL, 35},
        {0x14df54a49eULL, 36},  {0x14eb692109ULL, 37},  {0x14879f0ed0ULL, 38},  {0x14b3a28b47ULL, 39},
        {0x1d467a9e9dULL, 40},  {0x1d72471b0aULL, 41},  {0x1d1eb134d3ULL, 42},  {0x1d2a8cb144ULL, 43},
        {0x1ddbd93fecULL, 44},  {0x1defe4ba7bULL, 45},  {0x1d831295a2ULL, 46},  {0x1db72f1035ULL, 47},
        {0x164af0f210ULL, 48},  {0x167ecd7787ULL, 49},  {0x16123b585eULL, 50},  {0x162606ddc9ULL, 51},
        {0x16d7535361ULL, 52},  {0x16e36ed6f6ULL, 53},  {0x168f98f92fULL, 54},  {0x16bba57cb8ULL, 55},
        {0x1f4e7d6962ULL, 56},  {0x1f7a40ecf5ULL, 57},  {0x1f16b6c32cULL, 58},  {0x1f228b46bbULL, 59},
        {0x1fd3dec813ULL, 60},  {0x1fe7e34d84ULL, 61},  {0x1f8b15625dULL, 62},  {0x1fbf28e7caULL, 63},
        {0x084d62c339ULL, 64},  {0x08795f46aeULL, 65},  {0x0815a96977ULL, 66},  {0x082194ece0ULL, 67},
        {0x08d0c16248ULL, 68},  {0x08e4fce7dfULL, 69},  {0x08880ac806ULL, 70},  {0x08bc374d91ULL, 71},
        {0x0149ef584bULL, 72},  {0x017dd2dddcULL, 73},  {0x011124f205ULL, 74},  {0x0125197792ULL, 75},
        {0x01d44cf93aULL, 76},  {0x01e0717cadULL, 77},  {0x018c875374ULL, 78},  {0x01b8bad6e3ULL, 79},
        {0x0a456534c6ULL, 80},  {0x0a7158b151ULL, 81},  {0x0a1dae9e88ULL, 82},  {0x0a29931b1fULL, 83},
        {0x0ad8c695b7ULL, 84},  {0x0aecfb1020ULL, 85},  {0x0a800d3ff9ULL, 86},  {0x0ab430ba6eULL, 87},
        {0x0341e8afb4ULL, 88},  {0x0375d52a23ULL, 89},  {0x03192305faULL, 90},  {0x032d1e806dULL, 91},
        {0x03dc4b0ec5ULL, 92},  {0x03e8768b52ULL, 93},  {0x038480a48bULL, 94},  {0x03b0bd211cULL, 95},
        {0x044dbc12b7ULL, 96},  {0x0479819720ULL, 97},  {0x041577b8f9ULL, 98},  {0x04214a3d6eULL, 99},
        {0x04d01fb3c6ULL, 100}, {0x04e4223651ULL, 101}, {0x0488d41988ULL, 102}, {0x04bce99c1fULL, 103},
        {0x0d493189c5ULL, 104}, {0x0d7d0c0c52ULL, 105}, {0x0d11fa238bULL, 106}, {0x0d25c7a61cULL, 107},
        {0x0dd49228b4ULL, 108}, {0x0de0afad23ULL, 109}, {0x0d8c5982faULL, 110}, {0x0db864076dULL, 111},
        {0x0645bbe548ULL, 112}, {0x06718660dfULL, 113}, {0x061d704f06ULL, 114}, {0x06294dca91ULL, 115},
        {0x06d8184439ULL, 116}, {0x06ec25c1aeULL, 117}, {0x0680d3ee77ULL, 118}, {0x06b4ee6be0ULL, 119},
        {0x0f41367e3aULL, 120}, {0x0f750bfbadULL, 121}, {0x0f19fdd474ULL, 122}, {0x0f2dc051e3ULL, 123},
        {0x0fdc95df4bULL, 124}, {0x0fe8a85adcULL, 125}, {0x0f845e7505ULL, 126}, {0x0fb063f092ULL, 127},
        {0x575d57f7ffULL, -2}}; // S-ISCH
    return m;
}

//I-ISCH Lookup, borrowed from OP25 (now w/ error correction)
int
isch_lookup(uint64_t isch) {
    const auto& m = isch_table();
    auto it = m.find(isch);
    if (it != m.end()) {
        return it->second; // exact match
    }
    // No exact match: find smallest Hamming distance, correct up to 7 bits
    int decoded = -2;
    int popmin = 40;
    for (const auto& kv : m) {
        int popct = dsd_popcount64(isch ^ kv.first);
        if ((popct <= 7) && (popct < popmin)) {
            decoded = kv.second;
            popmin = popct;
        }
    }
    return decoded;
}
