// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * dmr_34_viterbi.c
 * Normative DMR 3/4 decoder (hard-decision Viterbi) compatible with existing dmr_34() packing.
 */

#include <stdint.h>
#include <string.h>

#include <dsd-neo/protocol/dmr/r34_viterbi.h>

// Deinterleave schedule (copy of dmr_34.c interleave[])
static const uint8_t s_interleave[98] = {0,  1,  8,  9,  16, 17, 24, 25, 32, 33, 40, 41, 48, 49, 56, 57, 64, 65, 72, 73,
                                         80, 81, 88, 89, 96, 97, 2,  3,  10, 11, 18, 19, 26, 27, 34, 35, 42, 43, 50, 51,
                                         58, 59, 66, 67, 74, 75, 82, 83, 90, 91, 4,  5,  12, 13, 20, 21, 28, 29, 36, 37,
                                         44, 45, 52, 53, 60, 61, 68, 69, 76, 77, 84, 85, 92, 93, 6,  7,  14, 15, 22, 23,
                                         30, 31, 38, 39, 46, 47, 54, 55, 62, 63, 70, 71, 78, 79, 86, 87, 94, 95};

// Nibble (dibit pair) to constellation point mapping (copy of dmr_34.c)
static const uint8_t s_constellation_map[16] = {11, 12, 0, 7, 14, 9, 5, 2, 10, 13, 1, 6, 15, 8, 4, 3};

// FSM mapping: for prev_state in [0..7], and tribit t in [0..7],
// expected constellation point code = s_fsm[prev_state*8 + t]. (copy of dmr_34.c)
static const uint8_t s_fsm[64] = {0, 8,  4, 12, 2, 10, 6, 14, 4, 12, 2, 10, 6, 14, 0, 8, 1, 9,  5, 13, 3, 11,
                                  7, 15, 5, 13, 3, 11, 7, 15, 1, 9,  3, 11, 7, 15, 1, 9, 5, 13, 7, 15, 1, 9,
                                  5, 13, 3, 11, 2, 10, 6, 14, 0, 8,  4, 12, 6, 14, 0, 8, 4, 12, 2, 10};

static inline int
hamming4(uint8_t a, uint8_t b) {
    static const uint8_t pop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
    return (int)pop4[((a ^ b) & 0x0F)];
}

// Inverse map: point code (0..15) -> nibble (0..15) such that
// s_constellation_map[nibble] == point
static const uint8_t s_unmap_point_to_nibble[16] = {2, 10, 7, 15, 14, 6, 11, 3, 13, 5, 8, 0, 1, 9, 4, 12};

static int
decode_hard_impl(const uint8_t* dibits98, int force_end_state, int end_state, uint8_t out_bytes18[18]) {
    if (!dibits98 || !out_bytes18) {
        return -1;
    }
    if (force_end_state && (end_state < 0 || end_state > 7)) {
        return -1;
    }

    // Step 1: deinterleave dibits using schedule
    uint8_t dibits_dei[98];
    for (int i = 0; i < 98; i++) {
        dibits_dei[s_interleave[i]] = (uint8_t)(dibits98[i] & 0x3u);
    }

    // Step 2: pack dibits into 49 nibbles: nib = (dibit0<<2)|(dibit1)
    uint8_t nibs[49];
    for (int i = 0; i < 49; i++) {
        uint8_t d0 = dibits_dei[i * 2 + 0] & 0x3u;
        uint8_t d1 = dibits_dei[i * 2 + 1] & 0x3u;
        nibs[i] = (uint8_t)((d0 << 2) | d1);
    }

    // Step 3: map to observed constellation point codes (0..15)
    uint8_t obs_point[49];
    for (int i = 0; i < 49; i++) {
        obs_point[i] = s_constellation_map[nibs[i] & 0x0F];
    }

    // Step 4: Viterbi over 8 states, time 49, transitions: prev_state -> next_state = tribit (0..7)
    // Branch metric: Hamming distance between expected and observed point codes.
    enum { T = 49, S = 8 };

    const int INF = 1000000000;
    int metric_prev[S];
    int metric_curr[S];
    uint8_t backptr[T][S];

    for (int s = 0; s < S; s++) {
        metric_prev[s] = INF;
    }
    metric_prev[0] = 0; // start in state 0

    for (int t = 0; t < T; t++) {
        for (int s = 0; s < S; s++) {
            metric_curr[s] = INF;
        }
        for (int ps = 0; ps < S; ps++) {
            if (metric_prev[ps] >= INF) {
                continue;
            }
            for (int ns = 0; ns < S; ns++) {
                uint8_t expect = s_fsm[ps * 8 + ns];
                int cost = hamming4(expect, obs_point[t]);
                int m = metric_prev[ps] + cost;
                if (m < metric_curr[ns]) {
                    metric_curr[ns] = m;
                    backptr[t][ns] = (uint8_t)ps;
                }
            }
        }
        for (int s = 0; s < S; s++) {
            metric_prev[s] = metric_curr[s];
        }
    }

    // Step 5: traceback from best/forced end state
    int best_s = 0;
    if (force_end_state) {
        best_s = end_state;
        if (metric_prev[best_s] >= INF) {
            return -1;
        }
    } else {
        int best_m = metric_prev[0];
        for (int s = 1; s < S; s++) {
            if (metric_prev[s] < best_m) {
                best_m = metric_prev[s];
                best_s = s;
            }
        }
    }

    // Extract tribits: next state equals tribit chosen; reconstruct states s_t for each time t
    uint8_t states[T];
    int s = best_s;
    for (int t = T - 1; t >= 0; t--) {
        states[t] = (uint8_t)s; // state after consuming symbol t
        s = backptr[t][s];
    }

    // Step 6: pack first 48 tribits (states[0..47]) into 18 bytes
    for (int g = 0; g < 6; g++) {
        uint32_t temp = 0;
        for (int k = 0; k < 8; k++) {
            temp = (temp << 3) | (uint32_t)(states[g * 8 + k] & 0x7u);
        }
        out_bytes18[g * 3 + 0] = (uint8_t)((temp >> 16) & 0xFF);
        out_bytes18[g * 3 + 1] = (uint8_t)((temp >> 8) & 0xFF);
        out_bytes18[g * 3 + 2] = (uint8_t)(temp & 0xFF);
    }

    return 0;
}

int
dmr_r34_viterbi_decode(const uint8_t* dibits98, uint8_t out_bytes18[18]) {
    return decode_hard_impl(dibits98, 0, 0, out_bytes18);
}

int
dmr_r34_viterbi_decode_endstate(const uint8_t* dibits98, int end_state, uint8_t out_bytes18[18]) {
    return decode_hard_impl(dibits98, 1, end_state, out_bytes18);
}

static int
decode_soft_impl(const uint8_t* dibits98, const uint8_t* reliab98, int force_end_state, int end_state,
                 uint8_t out_bytes18[18]) {
    if (!dibits98 || !reliab98 || !out_bytes18) {
        return -1;
    }
    if (force_end_state && (end_state < 0 || end_state > 7)) {
        return -1;
    }

    // Step 1: deinterleave dibits using schedule
    uint8_t dibits_dei[98];
    for (int i = 0; i < 98; i++) {
        dibits_dei[s_interleave[i]] = (uint8_t)(dibits98[i] & 0x3u);
    }

    // Step 2: pack dibits into 49 nibbles
    uint8_t nibs[49];
    for (int i = 0; i < 49; i++) {
        uint8_t d0 = dibits_dei[i * 2 + 0] & 0x3u;
        uint8_t d1 = dibits_dei[i * 2 + 1] & 0x3u;
        nibs[i] = (uint8_t)((d0 << 2) | d1);
    }

    // Build reliability arrays for deinterleaved indices
    uint8_t reliab_dei[98];
    for (int i = 0; i < 98; i++) {
        // dei[interleave[i]] = input[i] => reliab_dei[interleave[i]] = reliab98[i]
        reliab_dei[s_interleave[i]] = reliab98[i];
    }

    uint8_t rhi[49];
    uint8_t rlo[49];
    for (int i = 0; i < 49; i++) {
        rhi[i] = reliab_dei[i * 2 + 0];
        rlo[i] = reliab_dei[i * 2 + 1];
    }

    // Step 3: Viterbi with weighted branch metric in nibble space
    enum { T = 49, S = 8 };

    const int INF = 1000000000;
    int metric_prev[S];
    int metric_curr[S];
    uint8_t backptr[T][S];

    for (int s = 0; s < S; s++) {
        metric_prev[s] = INF;
    }
    metric_prev[0] = 0;

    for (int t = 0; t < T; t++) {
        for (int s = 0; s < S; s++) {
            metric_curr[s] = INF;
        }
        for (int ps = 0; ps < S; ps++) {
            if (metric_prev[ps] >= INF) {
                continue;
            }
            for (int ns = 0; ns < S; ns++) {
                uint8_t expect_point = s_fsm[ps * 8 + ns];
                uint8_t expect_nib = s_unmap_point_to_nibble[expect_point];
                uint8_t obs_nib = nibs[t];
                uint8_t x = (uint8_t)(expect_nib ^ obs_nib);
                // Weighted bit mismatch cost (hi bits weight = rhi, lo bits = rlo)
                int cost = 0;
                if (x & 0x8) {
                    cost += rhi[t];
                }
                if (x & 0x4) {
                    cost += rhi[t];
                }
                if (x & 0x2) {
                    cost += rlo[t];
                }
                if (x & 0x1) {
                    cost += rlo[t];
                }
                int m = metric_prev[ps] + cost;
                if (m < metric_curr[ns]) {
                    metric_curr[ns] = m;
                    backptr[t][ns] = (uint8_t)ps;
                }
            }
        }
        for (int s = 0; s < S; s++) {
            metric_prev[s] = metric_curr[s];
        }
    }

    // Step 4: traceback from best/forced end state
    int best_s = 0;
    if (force_end_state) {
        best_s = end_state;
        if (metric_prev[best_s] >= INF) {
            return -1;
        }
    } else {
        int best_m = metric_prev[0];
        for (int s = 1; s < S; s++) {
            if (metric_prev[s] < best_m) {
                best_m = metric_prev[s];
                best_s = s;
            }
        }
    }

    uint8_t states[T];
    int s = best_s;
    for (int t = T - 1; t >= 0; t--) {
        states[t] = (uint8_t)s;
        s = backptr[t][s];
    }

    // Step 5: pack 48 tribits
    for (int g = 0; g < 6; g++) {
        uint32_t temp = 0;
        for (int k = 0; k < 8; k++) {
            temp = (temp << 3) | (uint32_t)(states[g * 8 + k] & 0x7u);
        }
        out_bytes18[g * 3 + 0] = (uint8_t)((temp >> 16) & 0xFF);
        out_bytes18[g * 3 + 1] = (uint8_t)((temp >> 8) & 0xFF);
        out_bytes18[g * 3 + 2] = (uint8_t)(temp & 0xFF);
    }
    return 0;
}

// Soft-decision variant using per-dibit reliability.
int
dmr_r34_viterbi_decode_soft(const uint8_t* dibits98, const uint8_t* reliab98, uint8_t out_bytes18[18]) {
    return decode_soft_impl(dibits98, reliab98, 0, 0, out_bytes18);
}

int
dmr_r34_viterbi_decode_soft_endstate(const uint8_t* dibits98, const uint8_t* reliab98, int end_state,
                                     uint8_t out_bytes18[18]) {
    return decode_soft_impl(dibits98, reliab98, 1, end_state, out_bytes18);
}

int
dmr_r34_viterbi_decode_list(const uint8_t* dibits98, const uint8_t* reliab98, dmr_r34_candidate* out_candidates,
                            int max_candidates, int* out_count) {
    if (!dibits98 || !out_candidates || !out_count || max_candidates <= 0) {
        return -1;
    }

    // Step 1: deinterleave dibits using schedule
    uint8_t dibits_dei[98];
    for (int i = 0; i < 98; i++) {
        dibits_dei[s_interleave[i]] = (uint8_t)(dibits98[i] & 0x3u);
    }

    // Step 2: pack dibits into 49 nibbles
    uint8_t nibs[49];
    for (int i = 0; i < 49; i++) {
        uint8_t d0 = dibits_dei[i * 2 + 0] & 0x3u;
        uint8_t d1 = dibits_dei[i * 2 + 1] & 0x3u;
        nibs[i] = (uint8_t)((d0 << 2) | d1);
    }

    // Step 3: build per-nibble reliability weights (hi/lo dibit)
    uint8_t rhi[49];
    uint8_t rlo[49];
    const int weighted = (reliab98 != NULL);
    if (weighted) {
        uint8_t reliab_dei[98];
        for (int i = 0; i < 98; i++) {
            reliab_dei[s_interleave[i]] = reliab98[i];
        }
        for (int i = 0; i < 49; i++) {
            rhi[i] = reliab_dei[i * 2 + 0];
            rlo[i] = reliab_dei[i * 2 + 1];
        }
    } else {
        // Unweighted: mismatch/no-mismatch in nibble space.
        for (int i = 0; i < 49; i++) {
            rhi[i] = 1;
            rlo[i] = 1;
        }
    }

    // Precompute expected nibbles for all transitions.
    uint8_t expect_nib_tbl[8][8];
    for (int ps = 0; ps < 8; ps++) {
        for (int ns = 0; ns < 8; ns++) {
            uint8_t expect_point = s_fsm[ps * 8 + ns];
            expect_nib_tbl[ps][ns] = s_unmap_point_to_nibble[expect_point];
        }
    }

    enum { T = 49, S = 8, K = 32 };

    const int INF = 1000000000;

    int metric_prev[S][K];
    int metric_curr[S][K];
    uint8_t back_state[T][S][K];
    uint8_t back_rank[T][S][K];

    for (int s = 0; s < S; s++) {
        for (int r = 0; r < K; r++) {
            metric_prev[s][r] = INF;
        }
    }
    metric_prev[0][0] = 0;

    for (int t = 0; t < T; t++) {
        for (int s = 0; s < S; s++) {
            for (int r = 0; r < K; r++) {
                metric_curr[s][r] = INF;
            }
        }

        for (int ps = 0; ps < S; ps++) {
            for (int pr = 0; pr < K; pr++) {
                int m0 = metric_prev[ps][pr];
                if (m0 >= INF) {
                    continue;
                }
                for (int ns = 0; ns < S; ns++) {
                    int cost = 0;
                    if (!weighted) {
                        uint8_t x = (uint8_t)(expect_nib_tbl[ps][ns] ^ nibs[t]);
                        int bitcnt = ((x >> 0) & 1u) + ((x >> 1) & 1u) + ((x >> 2) & 1u) + ((x >> 3) & 1u);
                        // Hard-decision lexicographic metric:
                        // prioritize minimizing symbol mismatches, then break ties by bit mismatches.
                        // Using 256 ensures one symbol error outweighs any possible bitcount difference across the block.
                        cost = (x != 0) ? (256 + bitcnt) : 0;
                    } else {
                        uint8_t x = (uint8_t)(expect_nib_tbl[ps][ns] ^ nibs[t]);
                        if (x & 0x8) {
                            cost += rhi[t];
                        }
                        if (x & 0x4) {
                            cost += rhi[t];
                        }
                        if (x & 0x2) {
                            cost += rlo[t];
                        }
                        if (x & 0x1) {
                            cost += rlo[t];
                        }
                    }
                    int m = m0 + cost;

                    // Insert into top-K list for state ns (sorted ascending).
                    for (int rr = 0; rr < K; rr++) {
                        if (m <= metric_curr[ns][rr]) {
                            for (int sh = K - 1; sh > rr; sh--) {
                                metric_curr[ns][sh] = metric_curr[ns][sh - 1];
                                back_state[t][ns][sh] = back_state[t][ns][sh - 1];
                                back_rank[t][ns][sh] = back_rank[t][ns][sh - 1];
                            }
                            metric_curr[ns][rr] = m;
                            back_state[t][ns][rr] = (uint8_t)ps;
                            back_rank[t][ns][rr] = (uint8_t)pr;
                            break;
                        }
                    }
                }
            }
        }

        for (int s = 0; s < S; s++) {
            for (int r = 0; r < K; r++) {
                metric_prev[s][r] = metric_curr[s][r];
            }
        }
    }

    // Gather final candidates (state, rank) with their metric, then sort.
    struct cand_idx {
        int metric;
        uint8_t state;
        uint8_t rank;
    };
    struct cand_idx idx[S * K];
    int idx_n = 0;
    for (int s = 0; s < S; s++) {
        for (int r = 0; r < K; r++) {
            if (metric_prev[s][r] < INF) {
                idx[idx_n].metric = metric_prev[s][r];
                idx[idx_n].state = (uint8_t)s;
                idx[idx_n].rank = (uint8_t)r;
                idx_n++;
            }
        }
    }

    // Simple insertion sort by metric (idx_n is small, <= 64).
    for (int i = 1; i < idx_n; i++) {
        struct cand_idx key = idx[i];
        int j = i - 1;
        while (j >= 0 && idx[j].metric > key.metric) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = key;
    }

    int out_n = idx_n;
    if (out_n > max_candidates) {
        out_n = max_candidates;
    }

    for (int ci = 0; ci < out_n; ci++) {
        uint8_t states[T];
        int s = idx[ci].state;
        int r = idx[ci].rank;
        for (int t = T - 1; t >= 0; t--) {
            states[t] = (uint8_t)s;
            int ps = (int)back_state[t][s][r];
            int pr = (int)back_rank[t][s][r];
            s = ps;
            r = pr;
        }

        // Pack first 48 tribits (states[0..47]) into 18 bytes.
        for (int g = 0; g < 6; g++) {
            uint32_t temp = 0;
            for (int k = 0; k < 8; k++) {
                temp = (temp << 3) | (uint32_t)(states[g * 8 + k] & 0x7u);
            }
            out_candidates[ci].bytes18[g * 3 + 0] = (uint8_t)((temp >> 16) & 0xFF);
            out_candidates[ci].bytes18[g * 3 + 1] = (uint8_t)((temp >> 8) & 0xFF);
            out_candidates[ci].bytes18[g * 3 + 2] = (uint8_t)(temp & 0xFF);
        }
        out_candidates[ci].metric = idx[ci].metric;
    }

    *out_count = out_n;
    return 0;
}

// Simple encoder helper for tests
int
dmr_r34_encode(const uint8_t out_bytes18[18], uint8_t dibits98[98]) {
    if (!out_bytes18 || !dibits98) {
        return -1;
    }

    // Unpack 48 tribits from 18 bytes
    uint8_t tribits[48];
    for (int g = 0; g < 6; g++) {
        uint32_t temp = ((uint32_t)out_bytes18[g * 3 + 0] << 16) | ((uint32_t)out_bytes18[g * 3 + 1] << 8)
                        | ((uint32_t)out_bytes18[g * 3 + 2] << 0);
        for (int k = 0; k < 8; k++) {
            tribits[g * 8 + k] = (uint8_t)((temp >> (21 - 3 * k)) & 0x7u);
        }
    }

    // Generate deinterleaved dibits from trellis using FSM
    uint8_t de[98];
    uint8_t state = 0;
    for (int t = 0; t < 49; t++) {
        uint8_t tri;
        if (t < 48) {
            tri = tribits[t] & 0x7u;
        } else {
            tri = 0; // tail symbol (simple choice)
        }
        uint8_t point = s_fsm[state * 8 + tri];
        uint8_t nib = s_unmap_point_to_nibble[point];
        uint8_t d0 = (uint8_t)((nib >> 2) & 0x3u);
        uint8_t d1 = (uint8_t)(nib & 0x3u);
        de[t * 2 + 0] = d0;
        de[t * 2 + 1] = d1;
        state = tri;
    }

    // Interleave to output order: input[i] = de[ interleave[i] ]
    for (int i = 0; i < 98; i++) {
        dibits98[i] = de[s_interleave[i]];
    }
    return 0;
}
