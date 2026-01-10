// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Soft-decision FEC decoders for P25 Phase 1 voice (HDU/LDU/TDULC).
 *
 * These routines use per-bit reliability values (0-255) to improve decode
 * success at low SNR by implementing Chase-style soft decoding for Hamming
 * and Golay codes.
 */
#ifndef P25P1_SOFT_H_a3b5c7d9e1f24680
#define P25P1_SOFT_H_a3b5c7d9e1f24680

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Soft-decision Hamming(10,6,3) decoder using Chase-II style algorithm.
 *
 * @param bits      Input: 10 bits as char array [0..9], where [0..5]=data, [6..9]=parity.
 * @param reliab    Input: 10 reliability values [0..255] corresponding to bits.
 * @param out_bits  Output: corrected 10 bits (may be same pointer as bits).
 * @return 0=no error, 1=corrected, 2=uncorrectable.
 *
 * Algorithm:
 * 1. Try hard decode first. If syndrome=0, return success.
 * 2. If hard decode corrects (1 bit), return success.
 * 3. If hard decode fails (2+ errors detected):
 *    a. Find the 3 least reliable bit positions.
 *    b. Generate all 2^3=8 candidates by flipping combinations of these bits.
 *    c. For each candidate, compute syndrome. If valid (syndrome=0), compute penalty.
 *    d. Penalty = sum of (255 - reliab[i]) for each bit that differs from original.
 *    e. Pick candidate with lowest penalty. Ties: prefer fewer flips.
 * 4. If no valid candidate found, return uncorrectable.
 */
int hamming_10_6_3_soft(const char* bits, const int* reliab, char* out_bits);

/**
 * Soft-decision Golay(24,6) decoder using small-list Chase algorithm.
 *
 * @param data      Input/Output: 6 data bits as char array.
 * @param parity    Input: 12 parity bits as char array.
 * @param reliab    Input: 18 reliability values [0..255], indices 0-5=data, 6-17=parity.
 * @param fixed     Output: number of bits corrected.
 * @return 0=success, 1=uncorrectable.
 *
 * Algorithm:
 * 1. Try hard decode. If success (<=3 errors), return.
 * 2. Sort bit indices 0..17 by reliability (ascending = least reliable first).
 * 3. Take the 5 least reliable positions.
 * 4. Generate candidates: flip weight-1, weight-2, weight-3 combinations.
 *    Total candidates: C(5,1) + C(5,2) + C(5,3) = 5 + 10 + 10 = 25, plus original = 26 max.
 * 5. For each candidate, run hard decode. If valid:
 *    a. Compute penalty = sum of (255 - reliab[i]) for differing bits.
 *    b. Track minimum penalty candidate.
 * 6. Return best candidate, or uncorrectable if none valid.
 */
int check_and_fix_golay_24_6_soft(char* data, char* parity, const int* reliab, int* fixed);

/**
 * Soft-decision Golay(24,12) decoder using small-list Chase algorithm.
 *
 * Same approach as Golay(24,6) but:
 * - 12 data bits + 12 parity bits
 * - Consider 6 least reliable bits
 * - Generate weight-1..4 flips: C(6,1)+C(6,2)+C(6,3)+C(6,4) = 6+15+20+15 = 56 candidates
 *
 * @param data      Input/Output: 12 data bits as char array.
 * @param parity    Input: 12 parity bits as char array.
 * @param reliab    Input: 24 reliability values [0..255], indices 0-11=data, 12-23=parity.
 * @param fixed     Output: number of bits corrected.
 * @return 0=success, 1=uncorrectable.
 */
int check_and_fix_golay_24_12_soft(char* data, char* parity, const int* reliab, int* fixed);

/**
 * Get the P25P1 soft-decision erasure threshold.
 *
 * Configuration priority:
 * 1. DSD_NEO_P25P1_SOFT_ERASURE_THRESH environment variable (P25P1-specific)
 * 2. DSD_NEO_P25P2_SOFT_ERASURE_THRESH environment variable (shared fallback)
 * 3. Default value: 64
 *
 * @return Threshold value 0-255. Symbols with reliability below this are marked as erasures.
 */
int p25p1_get_erasure_threshold(void);

#ifdef __cplusplus
}
#endif

#endif /* P25P1_SOFT_H_a3b5c7d9e1f24680 */
