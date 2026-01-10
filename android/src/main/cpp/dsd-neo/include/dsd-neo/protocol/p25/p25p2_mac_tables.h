// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief P25 Phase 2 MAC opcode/field lookup tables.
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 2 MAC opcode length table and vendor overrides.
 *
 * Exposes p25p2_mac_len_for for callers that need to determine the
 * message-carrying octet length for a given (mfid, opcode) pair.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the MAC message length for a given opcode with vendor overrides.
 *
 * Length semantics match the internal table: number of octets following the
 * opcode byte (i.e., includes MFID and payload, excludes the opcode itself).
 */
int p25p2_mac_len_for(uint8_t mfid, uint8_t opcode);

#ifdef __cplusplus
}
#endif
