// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Soft-decision FEC decoders for P25 Phase 1 voice.
 */

#include <dsd-neo/protocol/p25/p25p1_soft.h>

#include <dsd-neo/fec/Golay24.hpp>
#include <dsd-neo/fec/Hamming.hpp>
#include <dsd-neo/runtime/config.h>

#include <algorithm>
#include <cstring>

/* Erasure threshold: symbols with reliability below this are marked as erasures.
 * Range: 0-255. Default: 64 (~25% confidence).
 */
static int g_p25p1_erasure_thresh = -1; /* -1 = uninitialized */

extern "C" int
p25p1_get_erasure_threshold(void) {
    if (g_p25p1_erasure_thresh < 0) {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();

        g_p25p1_erasure_thresh = 64; /* default */
        if (cfg) {
            if (cfg->p25p1_soft_erasure_thresh_is_set) {
                g_p25p1_erasure_thresh = cfg->p25p1_soft_erasure_thresh;
            } else if (cfg->p25p2_soft_erasure_thresh_is_set) {
                g_p25p1_erasure_thresh = cfg->p25p2_soft_erasure_thresh;
            }
        }
    }
    return g_p25p1_erasure_thresh;
}

/* Helper: find indices of k smallest values in reliab[0..n-1] */
static void
find_k_least_reliable(const int* reliab, int n, int k, int* out_indices) {
    /* Simple selection: copy indices, partial sort by reliability */
    int indices[32]; /* max n we'll handle */
    for (int i = 0; i < n && i < 32; i++) {
        indices[i] = i;
    }

    /* Bubble sort first k elements to front (good enough for small k) */
    for (int i = 0; i < k && i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (reliab[indices[j]] < reliab[indices[i]]) {
                int tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
        out_indices[i] = indices[i];
    }
}

/* Helper: compute syndrome for Hamming(10,6,3) */
static int
hamming_syndrome(const char* bits) {
    /* Syndrome bits from parity check matrix H:
     * h0 = 1110011000 -> bits 9,8,7,4,3
     * h1 = 1101010100 -> bits 9,8,6,4,2
     * h2 = 1011100010 -> bits 9,7,6,5,1
     * h3 = 0111100001 -> bits 8,7,6,5,0
     * bits[0]=bit9 (MSB), bits[9]=bit0 (LSB)
     */
    int s0 = bits[0] ^ bits[1] ^ bits[2] ^ bits[5] ^ bits[6];
    int s1 = bits[0] ^ bits[1] ^ bits[3] ^ bits[5] ^ bits[7];
    int s2 = bits[0] ^ bits[2] ^ bits[3] ^ bits[4] ^ bits[8];
    int s3 = bits[1] ^ bits[2] ^ bits[3] ^ bits[4] ^ bits[9];
    return (s0 << 3) | (s1 << 2) | (s2 << 1) | s3;
}

/* Compute penalty for flipping bits: sum of (255 - reliab[i]) for each flipped bit */
static int
compute_penalty(const char* orig, const char* candidate, const int* reliab, int n) {
    int penalty = 0;
    for (int i = 0; i < n; i++) {
        if (orig[i] != candidate[i]) {
            penalty += (255 - reliab[i]);
        }
    }
    return penalty;
}

extern "C" int
hamming_10_6_3_soft(const char* bits, const int* reliab, char* out_bits) {
    char candidate[10];
    std::memcpy(candidate, bits, 10);

    /* Try hard decode first */
    int syndrome = hamming_syndrome(candidate);
    if (syndrome == 0) {
        /* No errors detected */
        std::memcpy(out_bits, bits, 10);
        return 0;
    }

    /* Use table implementation for hard decode */
    Hamming_10_6_3_TableImpl hamming;
    char hex[6], parity[4];
    std::memcpy(hex, bits, 6);
    std::memcpy(parity, bits + 6, 4);

    int hard_result = hamming.decode(hex, parity);
    if (hard_result == 1) {
        /* Single error corrected by hard decoder */
        std::memcpy(out_bits, hex, 6);
        std::memcpy(out_bits + 6, parity, 4);
        return 1;
    }

    if (hard_result == 0) {
        /* No error - shouldn't happen if syndrome != 0, but handle it */
        std::memcpy(out_bits, bits, 10);
        return 0;
    }

    /* Hard decode failed (2+ errors). Try soft decode with Chase-II. */
    /* Find 3 least reliable bit positions */
    int least_rel[3];
    find_k_least_reliable(reliab, 10, 3, least_rel);

    int best_penalty = 999999;
    char best_candidate[10];
    int found_valid = 0;
    int best_flips = 99;

    /* Try all 2^3 = 8 combinations of flipping these bits */
    for (int mask = 0; mask < 8; mask++) {
        std::memcpy(candidate, bits, 10);

        int num_flips = 0;
        for (int b = 0; b < 3; b++) {
            if (mask & (1 << b)) {
                candidate[least_rel[b]] ^= 1;
                num_flips++;
            }
        }

        /* Check if this candidate has valid syndrome */
        int syn = hamming_syndrome(candidate);
        if (syn == 0) {
            int penalty = compute_penalty(bits, candidate, reliab, 10);
            /* Prefer fewer flips if penalties are close */
            if (penalty < best_penalty || (penalty == best_penalty && num_flips < best_flips)) {
                best_penalty = penalty;
                best_flips = num_flips;
                std::memcpy(best_candidate, candidate, 10);
                found_valid = 1;
            }
        }
    }

    if (found_valid) {
        std::memcpy(out_bits, best_candidate, 10);
        return 1;
    }

    /* No valid candidate found */
    std::memcpy(out_bits, bits, 10);
    return 2;
}

extern "C" int
check_and_fix_golay_24_6_soft(char* data, char* parity, const int* reliab, int* fixed) {
    *fixed = 0;

    /* Try hard decode first */
    char hex_copy[6], parity_copy[12];
    std::memcpy(hex_copy, data, 6);
    std::memcpy(parity_copy, parity, 12);

    DSDGolay24 golay;
    int hard_fixed = 0;
    int hard_result = golay.decode_6(hex_copy, parity_copy, &hard_fixed);

    if (hard_result == 0) {
        /* Hard decode succeeded */
        std::memcpy(data, hex_copy, 6);
        *fixed = hard_fixed;
        return 0;
    }

    /* Hard decode failed. Try soft decode with Chase-style algorithm. */
    /* Find 5 least reliable bit positions across all 18 bits (6 data + 12 parity) */
    int least_rel[5];
    find_k_least_reliable(reliab, 18, 5, least_rel);

    int best_penalty = 999999;
    char best_data[6];
    int best_fixed = 0;
    int found_valid = 0;

    /* Combine data and parity for candidate generation */
    char orig[18], candidate[18];
    std::memcpy(orig, data, 6);
    std::memcpy(orig + 6, parity, 12);

    /* Try all weight-1, weight-2, weight-3 combinations: C(5,1)+C(5,2)+C(5,3) = 25 candidates */
    /* Plus original = 26 total */
    for (int mask = 0; mask < 32; mask++) {
        int weight = 0;
        for (int b = 0; b < 5; b++) {
            if (mask & (1 << b)) {
                weight++;
            }
        }
        if (weight > 3) {
            continue; /* Skip weight-4 and weight-5 */
        }

        std::memcpy(candidate, orig, 18);
        for (int b = 0; b < 5; b++) {
            if (mask & (1 << b)) {
                candidate[least_rel[b]] ^= 1;
            }
        }

        /* Try hard decode on this candidate */
        char cand_data[6], cand_parity[12];
        std::memcpy(cand_data, candidate, 6);
        std::memcpy(cand_parity, candidate + 6, 12);

        int cand_fixed = 0;
        int cand_result = golay.decode_6(cand_data, cand_parity, &cand_fixed);

        if (cand_result == 0) {
            /* Valid decode. Compute penalty based on original bits. */
            char decoded[18];
            std::memcpy(decoded, cand_data, 6);
            /* Re-encode to get corrected parity for penalty calc */
            char enc_parity[12];
            golay.encode_6(cand_data, enc_parity);
            std::memcpy(decoded + 6, enc_parity, 12);

            int penalty = compute_penalty(orig, decoded, reliab, 18);
            if (penalty < best_penalty) {
                best_penalty = penalty;
                std::memcpy(best_data, cand_data, 6);
                best_fixed = cand_fixed + weight;
                found_valid = 1;
            }
        }
    }

    if (found_valid) {
        std::memcpy(data, best_data, 6);
        *fixed = best_fixed;
        return 0;
    }

    /* No valid candidate found */
    return 1;
}

extern "C" int
check_and_fix_golay_24_12_soft(char* data, char* parity, const int* reliab, int* fixed) {
    *fixed = 0;

    /* Try hard decode first */
    char dodeca_copy[12], parity_copy[12];
    std::memcpy(dodeca_copy, data, 12);
    std::memcpy(parity_copy, parity, 12);

    DSDGolay24 golay;
    int hard_fixed = 0;
    int hard_result = golay.decode_12(dodeca_copy, parity_copy, &hard_fixed);

    if (hard_result == 0) {
        /* Hard decode succeeded */
        std::memcpy(data, dodeca_copy, 12);
        *fixed = hard_fixed;
        return 0;
    }

    /* Hard decode failed. Try soft decode with Chase-style algorithm. */
    /* Find 6 least reliable bit positions across all 24 bits */
    int least_rel[6];
    find_k_least_reliable(reliab, 24, 6, least_rel);

    int best_penalty = 999999;
    char best_data[12];
    int best_fixed = 0;
    int found_valid = 0;

    /* Combine data and parity for candidate generation */
    char orig[24], candidate[24];
    std::memcpy(orig, data, 12);
    std::memcpy(orig + 12, parity, 12);

    /* Try all weight-1..4 combinations: C(6,1)+C(6,2)+C(6,3)+C(6,4) = 6+15+20+15 = 56 candidates */
    for (int mask = 0; mask < 64; mask++) {
        int weight = 0;
        for (int b = 0; b < 6; b++) {
            if (mask & (1 << b)) {
                weight++;
            }
        }
        if (weight > 4) {
            continue; /* Skip weight-5 and weight-6 */
        }

        std::memcpy(candidate, orig, 24);
        for (int b = 0; b < 6; b++) {
            if (mask & (1 << b)) {
                candidate[least_rel[b]] ^= 1;
            }
        }

        /* Try hard decode on this candidate */
        char cand_data[12], cand_parity[12];
        std::memcpy(cand_data, candidate, 12);
        std::memcpy(cand_parity, candidate + 12, 12);

        int cand_fixed = 0;
        int cand_result = golay.decode_12(cand_data, cand_parity, &cand_fixed);

        if (cand_result == 0) {
            /* Valid decode. Compute penalty based on original bits. */
            char decoded[24];
            std::memcpy(decoded, cand_data, 12);
            /* Re-encode to get corrected parity for penalty calc */
            char enc_parity[12];
            golay.encode_12(cand_data, enc_parity);
            std::memcpy(decoded + 12, enc_parity, 12);

            int penalty = compute_penalty(orig, decoded, reliab, 24);
            if (penalty < best_penalty) {
                best_penalty = penalty;
                std::memcpy(best_data, cand_data, 12);
                best_fixed = cand_fixed + weight;
                found_valid = 1;
            }
        }
    }

    if (found_valid) {
        std::memcpy(data, best_data, 12);
        *fixed = best_fixed;
        return 0;
    }

    /* No valid candidate found */
    return 1;
}
