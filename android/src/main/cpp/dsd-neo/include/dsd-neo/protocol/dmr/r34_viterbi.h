// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Rate 3/4 Viterbi decoder helpers for DMR.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Normative DMR Rate 3/4 decoder (hard-decision Viterbi), compatible with
// existing dmr_34() packing (18-byte payload from the first 48 tribits).
//
// Input: 98 dibits (values 0..3).
// Output: 18 payload bytes.
// Returns 0 on success; non-zero on internal errors.
/**
 * @brief Decode DMR rate 3/4 codewords using hard-decision Viterbi.
 *
 * @param dibits98 Input dibits (98 entries, values 0..3).
 * @param out_bytes18 [out] Decoded 18-byte payload.
 * @return 0 on success; non-zero on error.
 */
int dmr_r34_viterbi_decode(const uint8_t* dibits98, uint8_t out_bytes18[18]);

/**
 * @brief Decode DMR rate 3/4 codewords using hard-decision Viterbi with a forced end state.
 *
 * This is useful for CRC-aided selection on marginal signals where the globally best
 * end state may not yield a payload that satisfies higher-layer CRC checks.
 *
 * @param dibits98 Input dibits (98 entries, values 0..3).
 * @param end_state Forced end state [0..7] for traceback.
 * @param out_bytes18 [out] Decoded 18-byte payload.
 * @return 0 on success; non-zero on error.
 */
int dmr_r34_viterbi_decode_endstate(const uint8_t* dibits98, int end_state, uint8_t out_bytes18[18]);

// Soft-decision variant using per-dibit reliability weights.
// reliab98: 98 entries, 0..255 (higher means more confident) for each dibit.
// Branch metric penalizes mismatches proportionally to reliability.
/**
 * @brief Soft-decision DMR rate 3/4 decoder with reliability weights.
 *
 * @param dibits98 Input dibits (98 entries, values 0..3).
 * @param reliab98 Reliability weights per dibit (0..255; higher is more confident).
 * @param out_bytes18 [out] Decoded 18-byte payload.
 * @return 0 on success; non-zero on error.
 */
int dmr_r34_viterbi_decode_soft(const uint8_t* dibits98, const uint8_t* reliab98, uint8_t out_bytes18[18]);

/**
 * @brief Soft-decision DMR rate 3/4 decoder with a forced end state.
 *
 * @param dibits98 Input dibits (98 entries, values 0..3).
 * @param reliab98 Reliability weights per dibit (0..255; higher is more confident).
 * @param end_state Forced end state [0..7] for traceback.
 * @param out_bytes18 [out] Decoded 18-byte payload.
 * @return 0 on success; non-zero on error.
 */
int dmr_r34_viterbi_decode_soft_endstate(const uint8_t* dibits98, const uint8_t* reliab98, int end_state,
                                         uint8_t out_bytes18[18]);

/**
 * @brief Candidate decode result for list Viterbi.
 */
typedef struct {
    int metric;          /**< Smaller is better (relative within one decode). */
    uint8_t bytes18[18]; /**< Decoded 18-byte payload. */
} dmr_r34_candidate;

/**
 * @brief Decode DMR rate 3/4 codewords and return multiple candidate payloads.
 *
 * Uses a small list-Viterbi (top-K per state) and returns candidates sorted by increasing
 * accumulated metric. This is useful for CRC-aided selection on marginal signals.
 *
 * @param dibits98 Input dibits (98 entries, values 0..3).
 * @param reliab98 Optional reliability weights per dibit (98 entries, 0..255); pass NULL for unweighted cost.
 * @param out_candidates [out] Candidate array.
 * @param max_candidates Capacity of out_candidates (recommended >= 32).
 * @param out_count [out] Number of candidates written.
 * @return 0 on success; non-zero on error.
 */
int dmr_r34_viterbi_decode_list(const uint8_t* dibits98, const uint8_t* reliab98, dmr_r34_candidate* out_candidates,
                                int max_candidates, int* out_count);

// Simple R3/4 encoder helper for tests:
// Input: 18 payload bytes (packed as in dmr_34())
// Output: 98 dibits (0..3), interleaved per the standard.
/**
 * @brief Encode payload bytes using the DMR rate 3/4 convolutional code.
 *
 * Primarily used in tests to generate expected dibit sequences.
 *
 * @param out_bytes18 Input payload bytes (18 bytes).
 * @param dibits98 [out] Encoded dibits (98 entries, values 0..3).
 * @return 0 on success; non-zero on error.
 */
int dmr_r34_encode(const uint8_t out_bytes18[18], uint8_t dibits98[98]);

#ifdef __cplusplus
}
#endif
