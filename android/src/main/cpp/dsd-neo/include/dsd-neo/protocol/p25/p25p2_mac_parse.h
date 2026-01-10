// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief P25 Phase 2 MAC parser interfaces.
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 2 MAC VPDU/TSBK parsing helpers.
 *
 * Provides a small typed result struct describing the MAC header and
 * message lengths so higher-level code can make decisions without
 * re-implementing table lookups and MCO fallbacks.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct p25p2_mac_result {
    /* Channel type: 0 = FACCH, 1 = SACCH. */
    int type;
    /* MFID and opcode from the MAC header. */
    uint8_t mfid;
    uint8_t opcode;
    /* Message-carrying octet lengths (excluding opcode byte). */
    int len_a;
    int len_b;
    int len_c;
};

/**
 * Derive MAC message lengths and header fields from a VPDU/TSBK buffer.
 *
 * This helper centralizes table lookups and MCO-based fallbacks. It does not
 * depend on full dsd_state/opts to keep the interface test-friendly.
 *
 * @param type   Channel type: 0 = FACCH, 1 = SACCH.
 * @param mac    Pointer to up to 24 MAC words (one octet per entry).
 * @param out    Result struct to fill on success.
 *
 * @return 0 on success, negative on error.
 */
int p25p2_mac_parse(int type, const unsigned long long mac[24], struct p25p2_mac_result* out);

#ifdef __cplusplus
}
#endif
