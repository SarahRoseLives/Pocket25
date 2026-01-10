// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Unit tests for P25P2 soft-decision RS erasure marking.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Define reliability buffers that p25p2_soft.c will extern */
uint8_t p2reliab[700] = {0};
uint8_t p2xreliab[700] = {0};

/* Import test targets from p25p2_soft.c (linked directly) */
extern uint8_t p25p2_hexbit_reliability(const uint16_t bit_offsets[6], int ts_counter, const uint8_t* reliab);
extern int p25p2_facch_soft_erasures(int ts_counter, int scrambled, int* erasures, int n_fixed, int max_add);
extern int p25p2_sacch_soft_erasures(int ts_counter, int scrambled, int* erasures, int n_fixed, int max_add);

int
main(void) {
    int failures = 0;

    printf("P25P2 Soft Erasure Unit Tests\n");
    printf("==============================\n\n");

    /* Precomputed offset samples */
    static const uint16_t hex0_offsets[6] = {2, 3, 4, 5, 6, 7};
    static const uint16_t oob_offsets[6] = {1396, 1397, 1398, 1399, 1400, 1401};

    /* Test 1: Hexbit reliability with uniform high reliability */
    printf("Test 1: Hexbit reliability (uniform high)... ");
    memset(p2reliab, 200, sizeof(p2reliab));
    uint8_t r1 = p25p2_hexbit_reliability(hex0_offsets, 0, p2reliab);
    if (r1 == 200) {
        printf("PASS (rel=%u)\n", r1);
    } else {
        printf("FAIL (expected 200, got %u)\n", r1);
        failures++;
    }

    /* Test 2: Hexbit reliability with one low dibit */
    printf("Test 2: Hexbit reliability (one low dibit)... ");
    memset(p2reliab, 200, sizeof(p2reliab));
    /* hex0_offsets map to dibits 1,2,3. Set dibit 2 to low reliability */
    p2reliab[2] = 50;
    uint8_t r2 = p25p2_hexbit_reliability(hex0_offsets, 0, p2reliab);
    if (r2 == 50) {
        printf("PASS (rel=%u)\n", r2);
    } else {
        printf("FAIL (expected 50, got %u)\n", r2);
        failures++;
    }

    /* Test 3: Hexbit reliability across the FACCH boundary (hexbit 22) */
    printf("Test 3: Hexbit reliability (FACCH hexbit 22 cross-segment)... ");
    static const uint16_t hex22_offsets[6] = {136, 137, 180, 181, 182, 183};
    memset(p2reliab, 200, sizeof(p2reliab));
    /* hexbit 22 uses dibits 68, 90, 91; make dibit 90 the weakest */
    p2reliab[90] = 40;
    uint8_t r3 = p25p2_hexbit_reliability(hex22_offsets, 0, p2reliab);
    if (r3 == 40) {
        printf("PASS (rel=%u)\n", r3);
    } else {
        printf("FAIL (expected 40, got %u)\n", r3);
        failures++;
    }

    /* Test 4: Hexbit reliability out of bounds */
    printf("Test 4: Hexbit reliability (out of bounds)... ");
    uint8_t r4 = p25p2_hexbit_reliability(oob_offsets, 0, p2reliab);
    if (r4 == 0) {
        printf("PASS (rel=%u, forced erasure)\n", r4);
    } else {
        printf("FAIL (expected 0, got %u)\n", r4);
        failures++;
    }

    /* Test 5: FACCH soft erasures with all high reliability - should add no dynamic erasures */
    printf("Test 5: FACCH soft erasures (all high reliability)... ");
    memset(p2reliab, 200, sizeof(p2reliab));
    int erasures[28] = {0};
    int n_fixed = 0;
    /* Add fixed erasures for FACCH: 0-8, 54-62 */
    for (int e = 0; e <= 8; e++) {
        erasures[n_fixed++] = e;
    }
    for (int e = 54; e <= 62; e++) {
        erasures[n_fixed++] = e;
    }
    /* n_fixed should now be 18 */
    int n_total = p25p2_facch_soft_erasures(0, 0, erasures, n_fixed, 10);
    if (n_total == n_fixed) {
        printf("PASS (no dynamic erasures added, total=%d)\n", n_total);
    } else {
        printf("FAIL (expected %d, got %d erasures)\n", n_fixed, n_total);
        failures++;
    }

    /* Test 6: FACCH soft erasures with some low-reliability symbols */
    printf("Test 6: FACCH soft erasures (some low reliability)... ");
    memset(p2reliab, 200, sizeof(p2reliab));
    /* Set first payload hexbit (bit_offset=2, dibits 1,2,3) to low reliability */
    p2reliab[1] = 30;
    p2reliab[2] = 30;
    p2reliab[3] = 30;
    /* Reset erasure array */
    memset(erasures, 0, sizeof(erasures));
    n_fixed = 0;
    for (int e = 0; e <= 8; e++) {
        erasures[n_fixed++] = e;
    }
    for (int e = 54; e <= 62; e++) {
        erasures[n_fixed++] = e;
    }
    n_total = p25p2_facch_soft_erasures(0, 0, erasures, n_fixed, 10);
    if (n_total > n_fixed) {
        printf("PASS (added %d dynamic erasures, total=%d)\n", n_total - n_fixed, n_total);
    } else {
        printf("FAIL (expected dynamic erasures, got total=%d)\n", n_total);
        failures++;
    }

    /* Test 7: Max erasure cap is respected */
    printf("Test 7: FACCH soft erasures (max cap)... ");
    memset(p2reliab, 10, sizeof(p2reliab)); /* All low reliability */
    memset(erasures, 0, sizeof(erasures));
    n_fixed = 0;
    for (int e = 0; e <= 8; e++) {
        erasures[n_fixed++] = e;
    }
    for (int e = 54; e <= 62; e++) {
        erasures[n_fixed++] = e;
    }
    n_total = p25p2_facch_soft_erasures(0, 0, erasures, n_fixed, 5);
    if (n_total == n_fixed + 5) {
        printf("PASS (capped at %d dynamic, total=%d)\n", 5, n_total);
    } else {
        printf("FAIL (expected %d, got %d)\n", n_fixed + 5, n_total);
        failures++;
    }

    /* Test 8: SACCH soft erasures basic test */
    printf("Test 8: SACCH soft erasures (all high reliability)... ");
    memset(p2reliab, 200, sizeof(p2reliab));
    memset(erasures, 0, sizeof(erasures));
    n_fixed = 0;
    /* Add fixed erasures for SACCH: 0-4, 57-62 */
    for (int e = 0; e <= 4; e++) {
        erasures[n_fixed++] = e;
    }
    for (int e = 57; e <= 62; e++) {
        erasures[n_fixed++] = e;
    }
    /* n_fixed should now be 11 */
    n_total = p25p2_sacch_soft_erasures(0, 0, erasures, n_fixed, 16);
    if (n_total == n_fixed) {
        printf("PASS (no dynamic erasures added, total=%d)\n", n_total);
    } else {
        printf("FAIL (expected %d, got %d erasures)\n", n_fixed, n_total);
        failures++;
    }

    /* Test 9: Timeslot offset affects dibit lookup correctly */
    printf("Test 9: Timeslot offset (ts_counter=1)... ");
    memset(p2reliab, 200, sizeof(p2reliab));
    /* With ts_counter=1, hex0_offsets[0]=2 -> abs_bit = 2 + 360 = 362, dibit_idx = 181 */
    p2reliab[181] = 42; /* Set the target dibit to a known value */
    p2reliab[182] = 42;
    p2reliab[183] = 42;
    uint8_t r8 = p25p2_hexbit_reliability(hex0_offsets, 1, p2reliab);
    if (r8 == 42) {
        printf("PASS (rel=%u at ts=1)\n", r8);
    } else {
        printf("FAIL (expected 42, got %u)\n", r8);
        failures++;
    }

    printf("\n==============================\n");
    printf("%d test(s) failed\n", failures);
    return failures > 0 ? 1 : 0;
}
