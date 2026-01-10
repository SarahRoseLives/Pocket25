// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief P25 WACN/SysID to FCC Callsign conversion utilities.
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert P25 WACN and System ID to 6-character FCC callsign.
 *
 * Uses the Radix-50 encoding algorithm approved by APCO P25 Steering Committee
 * (April 6, 2001). The callsign is derived from the combined WACN (20-bit) and
 * SysID (12-bit) values using base-40 division.
 *
 * Character set (indices 0-39):
 *   0 = space, 1-26 = A-Z, 27 = '$', 28 = '.', 29 = '?', 30-39 = 0-9
 *
 * @param wacn   20-bit Wide Area Communication Network identifier
 * @param sysid  12-bit System identifier
 * @param out    Output buffer for callsign (must be at least 7 bytes for null terminator)
 */
void p25_wacn_sysid_to_callsign(uint32_t wacn, uint16_t sysid, char out[7]);

/**
 * @brief Format WACN/SysID with callsign for display.
 *
 * Produces a string like "WACN [BEE00] SYSID [001] (WBEE1)" where the
 * parenthetical callsign is only appended if it contains valid characters.
 *
 * @param wacn    20-bit WACN
 * @param sysid   12-bit SysID
 * @param out     Output buffer
 * @param out_len Size of output buffer
 * @return        Number of characters written (excluding null terminator)
 */
int p25_format_wacn_sysid(uint32_t wacn, uint16_t sysid, char* out, int out_len);

#ifdef __cplusplus
}
#endif
