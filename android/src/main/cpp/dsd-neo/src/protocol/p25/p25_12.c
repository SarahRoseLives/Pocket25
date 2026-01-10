// SPDX-License-Identifier: ISC
/*-------------------------------------------------------------------------------
 * p25_12.c
 * P25p1 1/2 Rate Simple Trellis Decoder
 *
 * LWVMOBILE
 * 2023-10 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <stdint.h>
#include <string.h>

uint8_t p25_interleave[98] = {0,  1,  8,  9,  16, 17, 24, 25, 32, 33, 40, 41, 48, 49, 56, 57, 64, 65, 72, 73,
                              80, 81, 88, 89, 96, 97, 2,  3,  10, 11, 18, 19, 26, 27, 34, 35, 42, 43, 50, 51,
                              58, 59, 66, 67, 74, 75, 82, 83, 90, 91, 4,  5,  12, 13, 20, 21, 28, 29, 36, 37,
                              44, 45, 52, 53, 60, 61, 68, 69, 76, 77, 84, 85, 92, 93, 6,  7,  14, 15, 22, 23,
                              30, 31, 38, 39, 46, 47, 54, 55, 62, 63, 70, 71, 78, 79, 86, 87, 94, 95};

//this is a convertion table for converting the dibit pairs into constellation points
uint8_t p25_constellation_map[16] = {11, 12, 0, 7, 14, 9, 5, 2, 10, 13, 1, 6, 15, 8, 4, 3};

//digitized dibit to OTA symbol conversion for reference
//0 = +1; 1 = +3;
//2 = -1; 3 = -3;

//finite state machine values
uint8_t p25_fsm[16] = {0, 15, 12, 3, 4, 11, 8, 7, 13, 2, 1, 14, 9, 6, 5, 10};

//this is a dibit-pair to trellis dibit transition matrix (SDRTrunk and Ossmann)
//when evaluating hamming distance, we want to use this xor the dibit-pair nib,
//and not the fsm xor the constellation point
uint8_t p25_dtm[16] = {2, 12, 1, 15, 14, 0, 13, 3, 9, 7, 10, 4, 5, 11, 6, 8};

int
count_bits(uint8_t b, int slen) {
    int i = 0;
    int j = 0;
    for (j = 0; j < slen; j++) {
        if ((b & 1) == 1) {
            i++;
        }
        b = b >> 1;
    }
    return i;
}

uint8_t
find_min(uint8_t list[4], int len) {
    int min = list[0];
    uint8_t index = 0;
    int i;

    for (i = 1; i < len; i++) {
        if (list[i] < min) {
            min = list[i];
            index = (uint8_t)i;
        }

        //NOTE: Disqualifying result on uniqueness can impact decoding
        //let the CRC determine if the result is good or bad
        //its not uncommon for two values of the same min
        //distance to emerge, so its 50/50 each time
    }

    return index;
}

int
p25_12(uint8_t* input, uint8_t treturn[12]) {
    /*
     * Soft-decision (semi-soft) 4-state Viterbi over dibit-pair nibbles.
     * Branch metric = Hamming distance between observed nibble and expected
     * transition nibble from p25_dtm[(prev_state<<2)|next_state].
     * This replaces the greedy FSM walk to improve robustness on marginal SNR.
     */
    enum { N_SYMS = 49, N_ST = 4 };

    int i, j;

    /* Deinterleave input dibits (98) into symbol-ordered dibits */
    uint8_t deinterleaved_dibits[98];
    memset(deinterleaved_dibits, 0, sizeof(deinterleaved_dibits));
    for (i = 0; i < 98; i++) {
        deinterleaved_dibits[p25_interleave[i]] = input[i];
    }

    /* Pack dibit pairs into 4-bit nibbles (observations per trellis step) */
    uint8_t nibs[N_SYMS];
    memset(nibs, 0, sizeof(nibs));
    for (i = 0; i < N_SYMS; i++) {
        nibs[i] = (uint8_t)((deinterleaved_dibits[(i * 2) + 0] << 2) | (deinterleaved_dibits[(i * 2) + 1]));
    }

    /* Viterbi: path metrics and backpointers */
    /* Use uint16_t; worst-case metric <= 49*4 = 196 */
    uint16_t prev_metric[N_ST];
    uint16_t curr_metric[N_ST];
    uint8_t backptr[N_SYMS][N_ST];

    /* Initialize metrics: bias start at state 0 slightly */
    for (j = 0; j < N_ST; j++) {
        prev_metric[j] = (uint16_t)((j == 0) ? 0 : 1);
    }

    for (i = 0; i < N_SYMS; i++) {
        /* For each candidate next state, pick best predecessor */
        for (int st_next = 0; st_next < N_ST; st_next++) {
            uint16_t best = (uint16_t)0xFFFF;
            uint8_t best_prev = 0;
            for (int st_prev = 0; st_prev < N_ST; st_prev++) {
                /* Expected transition nibble from (st_prev -> st_next) */
                uint8_t expect = p25_dtm[(st_prev << 2) | st_next] & 0xF;
                /* Hamming distance on 4-bit nibble */
                uint16_t bm = (uint16_t)count_bits((uint8_t)((nibs[i] ^ expect) & 0xF), 4);
                uint16_t cost = (uint16_t)(prev_metric[st_prev] + bm);
                if (cost < best) {
                    best = cost;
                    best_prev = (uint8_t)st_prev;
                }
            }
            curr_metric[st_next] = best;
            backptr[i][st_next] = best_prev;
        }
        /* Roll metrics */
        for (j = 0; j < N_ST; j++) {
            prev_metric[j] = curr_metric[j];
        }
    }

    /* Select best ending state */
    uint16_t best_final = curr_metric[0];
    int st = 0;
    for (j = 1; j < N_ST; j++) {
        if (curr_metric[j] < best_final) {
            best_final = curr_metric[j];
            st = j;
        }
    }

    /* Traceback to recover tdibits (next states) */
    uint8_t tdibits[N_SYMS];
    for (i = N_SYMS - 1; i >= 0; i--) {
        tdibits[i] = (uint8_t)st;
        st = backptr[i][st];
        if (i == 0) {
            break; /* avoid i underflow for unsigned loop */
        }
    }

    /* Pack first 48 tdibits into 12 bytes (MSB-first as before) */
    for (i = 0; i < 12; i++) {
        treturn[i] = (uint8_t)((tdibits[(i * 4) + 0] << 6) | (tdibits[(i * 4) + 1] << 4) | (tdibits[(i * 4) + 2] << 2)
                               | (tdibits[(i * 4) + 3]));
    }

    /* Return aggregate metric as a rough error indicator (compat: previous returned count) */
    return (int)best_final;
}

/*
 * Soft-decision 1/2-rate trellis decoder.
 * Same algorithm as p25_12() but weights bit mismatches by reliability values.
 * reliab98: per-dibit reliability (0=uncertain, 255=confident) parallel to input.
 */
int
p25_12_soft(uint8_t* input, const uint8_t* reliab98, uint8_t treturn[12]) {
    enum { N_SYMS = 49, N_ST = 4 };

    int i, j;

    /* Deinterleave input dibits (98) into symbol-ordered dibits */
    uint8_t deinterleaved_dibits[98];
    memset(deinterleaved_dibits, 0, sizeof(deinterleaved_dibits));
    for (i = 0; i < 98; i++) {
        deinterleaved_dibits[p25_interleave[i]] = input[i];
    }

    /* Deinterleave reliabilities in parallel */
    uint8_t reliab_dei[98];
    memset(reliab_dei, 0, sizeof(reliab_dei));
    for (i = 0; i < 98; i++) {
        reliab_dei[p25_interleave[i]] = reliab98[i];
    }

    /* Pack dibit pairs into 4-bit nibbles and gather per-dibit reliability */
    uint8_t nibs[N_SYMS];
    uint8_t rhi[N_SYMS], rlo[N_SYMS]; /* reliability for high/low dibit in nibble */
    memset(nibs, 0, sizeof(nibs));
    for (i = 0; i < N_SYMS; i++) {
        nibs[i] = (uint8_t)((deinterleaved_dibits[(i * 2) + 0] << 2) | (deinterleaved_dibits[(i * 2) + 1]));
        rhi[i] = reliab_dei[(i * 2) + 0];
        rlo[i] = reliab_dei[(i * 2) + 1];
    }

    /* Viterbi: path metrics and backpointers */
    /* Worst-case metric: 49 symbols * 4 bits * 255 = ~50k, fits uint32_t easily */
    uint32_t prev_metric[N_ST];
    uint32_t curr_metric[N_ST];
    uint8_t backptr[N_SYMS][N_ST];

    /* Initialize metrics: bias start at state 0 */
    for (j = 0; j < N_ST; j++) {
        prev_metric[j] = (j == 0) ? 0 : 256;
    }

    for (i = 0; i < N_SYMS; i++) {
        /* For each candidate next state, pick best predecessor */
        for (int st_next = 0; st_next < N_ST; st_next++) {
            uint32_t best = 0xFFFFFFFFu;
            uint8_t best_prev = 0;
            for (int st_prev = 0; st_prev < N_ST; st_prev++) {
                /* Expected transition nibble from (st_prev -> st_next) */
                uint8_t expect = p25_dtm[(st_prev << 2) | st_next] & 0xF;
                uint8_t obs = nibs[i];
                uint8_t diff = (uint8_t)((obs ^ expect) & 0xF);

                /* Weighted bit mismatch cost:
                 * High dibit (bits 3,2) weighted by rhi, low dibit (bits 1,0) by rlo.
                 * Each differing bit adds the corresponding reliability as cost. */
                uint32_t cost = 0;
                if (diff & 0x8) {
                    cost += rhi[i];
                }
                if (diff & 0x4) {
                    cost += rhi[i];
                }
                if (diff & 0x2) {
                    cost += rlo[i];
                }
                if (diff & 0x1) {
                    cost += rlo[i];
                }

                uint32_t m = prev_metric[st_prev] + cost;
                if (m < best) {
                    best = m;
                    best_prev = (uint8_t)st_prev;
                }
            }
            curr_metric[st_next] = best;
            backptr[i][st_next] = best_prev;
        }
        /* Roll metrics */
        for (j = 0; j < N_ST; j++) {
            prev_metric[j] = curr_metric[j];
        }
    }

    /* Select best ending state */
    uint32_t best_final = curr_metric[0];
    int st = 0;
    for (j = 1; j < N_ST; j++) {
        if (curr_metric[j] < best_final) {
            best_final = curr_metric[j];
            st = j;
        }
    }

    /* Traceback to recover tdibits (next states) */
    uint8_t tdibits[N_SYMS];
    for (i = N_SYMS - 1; i >= 0; i--) {
        tdibits[i] = (uint8_t)st;
        st = backptr[i][st];
        if (i == 0) {
            break;
        }
    }

    /* Pack first 48 tdibits into 12 bytes (MSB-first) */
    for (i = 0; i < 12; i++) {
        treturn[i] = (uint8_t)((tdibits[(i * 4) + 0] << 6) | (tdibits[(i * 4) + 1] << 4) | (tdibits[(i * 4) + 2] << 2)
                               | (tdibits[(i * 4) + 3]));
    }

    /* Return normalized metric (divide by 256 to roughly match hard-decision scale) */
    return (int)(best_final >> 8);
}
