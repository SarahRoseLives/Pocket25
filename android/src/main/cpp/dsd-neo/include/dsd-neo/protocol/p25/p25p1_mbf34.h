// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief Matched-bit filter coefficients for P25 Phase 1 (MBF3/4).
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#ifndef DSD_NEO_PROTOCOL_P25_P25P1_MBF34_H
#define DSD_NEO_PROTOCOL_P25_P25P1_MBF34_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * P25 Phase 1 Confirmed Data (3/4) decoder (MBF).
 *
 * Scaffold API: decodes 98 input dibits (already deinterleaved at the symbol
 * capture boundary) into 18 output bytes representing the de‑trellised 3/4
 * encoded payload chunk as found in the MBF Confirmed Data block.
 *
 * Return value: 0 on success; negative on failure. This is a stub suitable for
 * wiring and unit test scaffolding and is intended to be replaced by a
 * spec‑accurate implementation.
 */
/**
 * @brief Decode a P25 Phase 1 Confirmed Data MBF (3/4) block.
 *
 * @param dibits 98 input dibits (already deinterleaved at the symbol boundary).
 * @param out [out] Decoded 18-byte payload.
 * @return 0 on success; negative on failure (stub implementation).
 */
int p25_mbf34_decode(const uint8_t dibits[98], uint8_t out[18]);

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_PROTOCOL_P25_P25P1_MBF34_H */
