// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute Hamming distance between a sync buffer and a pattern.
 *
 * Inputs are expected to be dibits represented as either numeric values 0..3 or
 * ASCII '0'..'3'. Patterns are expected to be ASCII '0'..'3'.
 *
 * @param buf Pointer to received dibit buffer.
 * @param pat Pointer to expected pattern.
 * @param len Number of dibits to compare.
 * @return Number of mismatched dibits (0 = perfect match).
 */
int dsd_sync_hamming_distance(const char* buf, const char* pat, int len);

/**
 * @brief Compute best-case CQPSK Hamming distance to a sync pattern.
 *
 * Accounts for common dibit remaps (inversion, bit swap, XOR, 90° rotation)
 * that arise from differing slicer conventions.
 *
 * @param buf      Pointer to received dibit buffer (0..3 or ASCII '0'..'3').
 * @param pat_norm Pointer to expected pattern in normal polarity (ASCII '0'..'3').
 * @param pat_inv  Pointer to expected pattern in inverted polarity (ASCII '0'..'3').
 * @param len      Number of dibits to compare.
 * @return Best-case Hamming distance across supported remaps.
 */
int dsd_qpsk_sync_hamming_with_remaps(const char* buf, const char* pat_norm, const char* pat_inv, int len);

#ifdef __cplusplus
}
#endif
