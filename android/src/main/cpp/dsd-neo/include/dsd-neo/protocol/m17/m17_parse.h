// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief M17 frame parsing helpers.
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * M17 LSF and payload parsing helpers.
 *
 * Provides small typed result structs so higher-level protocol handlers
 * can consume decoded metadata without directly manipulating bit buffers
 * or printing to stderr.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct m17_lsf_result {
    /* Raw destination and source IDs (numeric). */
    unsigned long long dst;
    unsigned long long src;

    /* Decoded type fields from the LSF type word. */
    uint8_t dt;
    uint8_t et;
    uint8_t es;
    uint8_t cn;
    uint8_t rs;

    /* Decoded callsign strings (base-40) for dst/src. */
    char dst_csd[10];
    char src_csd[10];

    /* Optional 14-byte Meta/IV field when present. */
    uint8_t has_meta;
    uint8_t meta[14];
};

/**
 * Parse an M17 LSF bit buffer into a typed result.
 *
 * @param lsf_bits  Pointer to 240 bits (LSB=0/1) in MSB-first order.
 * @param bit_len   Number of bits available; must be at least 240.
 * @param out       Result struct to fill on success.
 *
 * @return 0 on success, negative on error (e.g., invalid args or length).
 */
int m17_parse_lsf(const uint8_t* lsf_bits, size_t bit_len, struct m17_lsf_result* out);

#ifdef __cplusplus
}
#endif
