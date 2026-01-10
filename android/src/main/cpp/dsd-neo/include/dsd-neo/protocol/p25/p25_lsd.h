// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief P25 Low Speed Data (LSD) structures and helpers.
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#ifndef DSD_NEO_PROTOCOL_P25_P25_LSD_H
#define DSD_NEO_PROTOCOL_P25_P25_LSD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * P25p1 Low Speed Data FEC (16,8).
 *
 * bits16: array of 16 bits stored as bytes 0/1 (MSB-first):
 *   bits16[0..7]   = data bits (MSB..LSB)
 *   bits16[8..15]  = parity bits (MSB..LSB)
 *
 * Performs syndrome-based single-bit correction across the full 16-bit codeword
 * (data and parity). Returns 1 if the codeword is valid after optional single-
 * bit correction, or 0 if uncorrectable (e.g., multi-bit errors).
 */
/**
 * @brief Validate and optionally correct a P25p1 Low Speed Data (16,8) codeword.
 *
 * @param bits16 Pointer to 16 entries (0/1) representing the codeword (MSB-first).
 * @return 1 if valid after optional single-bit correction; 0 if uncorrectable.
 */
int p25_lsd_fec_16x8(uint8_t* bits16);

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_PROTOCOL_P25_P25_LSD_H */
