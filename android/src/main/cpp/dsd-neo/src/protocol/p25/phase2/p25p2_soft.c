// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25P2 soft-decision RS erasure helpers.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/runtime/config.h>

/* Import reliability buffers from p25p2_frame.c */
extern uint8_t p2reliab[700];
extern uint8_t p2xreliab[700];

/* Configuration: erasure threshold (0-255). Symbols with reliability below
 * this are marked as erasures for RS decoding. Default 64 (~25%).
 */
static int g_erasure_thresh = -1; /* -1 = uninitialized */

static int
get_erasure_threshold(void) {
    if (g_erasure_thresh < 0) {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();

        g_erasure_thresh = 64; /* default */
        if (cfg && cfg->p25p2_soft_erasure_thresh_is_set) {
            g_erasure_thresh = cfg->p25p2_soft_erasure_thresh;
        }
    }
    return g_erasure_thresh;
}

/**
 * Compute reliability for a single hexbit (6 bits = 3 dibits).
 *
 * @param bit_offsets Six p2xbit/p2bit indices for this hexbit (relative to TS start).
 * @param ts_counter  Current timeslot counter (0-3).
 * @param reliab      Per-dibit reliability array (700 entries).
 * @return Minimum reliability; returns 0 if any dibit is out of bounds (forces erasure).
 */
uint8_t
p25p2_hexbit_reliability(const uint16_t bit_offsets[6], int ts_counter, const uint8_t* reliab) {
    if (reliab == NULL) {
        return 0;
    }

    uint8_t min_r = 255;
    for (int i = 0; i < 6; i++) {
        int abs_bit = (int)bit_offsets[i] + (ts_counter * 360);
        int dibit_idx = abs_bit / 2;

        if (dibit_idx < 0 || dibit_idx >= 700) {
            return 0; /* out of bounds -> mark as erasure */
        }

        uint8_t r = reliab[dibit_idx];
        if (r < min_r) {
            min_r = r;
        }
    }

    return min_r;
}

/*
 * FACCH bit offset tables - see "Bit Layout Analysis" section for derivation.
 */
static const uint16_t facch_payload_bit_offsets[26][6] = {
    /* 00 */ {2, 3, 4, 5, 6, 7},
    /* 01 */ {8, 9, 10, 11, 12, 13},
    /* 02 */ {14, 15, 16, 17, 18, 19},
    /* 03 */ {20, 21, 22, 23, 24, 25},
    /* 04 */ {26, 27, 28, 29, 30, 31},
    /* 05 */ {32, 33, 34, 35, 36, 37},
    /* 06 */ {38, 39, 40, 41, 42, 43},
    /* 07 */ {44, 45, 46, 47, 48, 49},
    /* 08 */ {50, 51, 52, 53, 54, 55},
    /* 09 */ {56, 57, 58, 59, 60, 61},
    /* 10 */ {62, 63, 64, 65, 66, 67},
    /* 11 */ {68, 69, 70, 71, 72, 73},
    /* 12 */ {76, 77, 78, 79, 80, 81},
    /* 13 */ {82, 83, 84, 85, 86, 87},
    /* 14 */ {88, 89, 90, 91, 92, 93},
    /* 15 */ {94, 95, 96, 97, 98, 99},
    /* 16 */ {100, 101, 102, 103, 104, 105},
    /* 17 */ {106, 107, 108, 109, 110, 111},
    /* 18 */ {112, 113, 114, 115, 116, 117},
    /* 19 */ {118, 119, 120, 121, 122, 123},
    /* 20 */ {124, 125, 126, 127, 128, 129},
    /* 21 */ {130, 131, 132, 133, 134, 135},
    /* 22 */ {136, 137, 180, 181, 182, 183}, /* cross-segment */
    /* 23 */ {184, 185, 186, 187, 188, 189},
    /* 24 */ {190, 191, 192, 193, 194, 195},
    /* 25 */ {196, 197, 198, 199, 200, 201},
};

static const uint16_t facch_parity_bit_offsets[19][6] = {
    /* 00 */ {202, 203, 204, 205, 206, 207},
    /* 01 */ {208, 209, 210, 211, 212, 213},
    /* 02 */ {214, 215, 216, 217, 218, 219},
    /* 03 */ {220, 221, 222, 223, 224, 225},
    /* 04 */ {226, 227, 228, 229, 230, 231},
    /* 05 */ {232, 233, 234, 235, 236, 237},
    /* 06 */ {238, 239, 240, 241, 242, 243},
    /* 07 */ {246, 247, 248, 249, 250, 251},
    /* 08 */ {252, 253, 254, 255, 256, 257},
    /* 09 */ {258, 259, 260, 261, 262, 263},
    /* 10 */ {264, 265, 266, 267, 268, 269},
    /* 11 */ {270, 271, 272, 273, 274, 275},
    /* 12 */ {276, 277, 278, 279, 280, 281},
    /* 13 */ {282, 283, 284, 285, 286, 287},
    /* 14 */ {288, 289, 290, 291, 292, 293},
    /* 15 */ {294, 295, 296, 297, 298, 299},
    /* 16 */ {300, 301, 302, 303, 304, 305},
    /* 17 */ {306, 307, 308, 309, 310, 311},
    /* 18 */ {312, 313, 314, 315, 316, 317},
};

/*
 * SACCH bit offset tables - see "Bit Layout Analysis" section for derivation.
 */
static const uint16_t sacch_payload_bit_offsets[30][6] = {
    /* 00 */ {2, 3, 4, 5, 6, 7},
    /* 01 */ {8, 9, 10, 11, 12, 13},
    /* 02 */ {14, 15, 16, 17, 18, 19},
    /* 03 */ {20, 21, 22, 23, 24, 25},
    /* 04 */ {26, 27, 28, 29, 30, 31},
    /* 05 */ {32, 33, 34, 35, 36, 37},
    /* 06 */ {38, 39, 40, 41, 42, 43},
    /* 07 */ {44, 45, 46, 47, 48, 49},
    /* 08 */ {50, 51, 52, 53, 54, 55},
    /* 09 */ {56, 57, 58, 59, 60, 61},
    /* 10 */ {62, 63, 64, 65, 66, 67},
    /* 11 */ {68, 69, 70, 71, 72, 73},
    /* 12 */ {76, 77, 78, 79, 80, 81},
    /* 13 */ {82, 83, 84, 85, 86, 87},
    /* 14 */ {88, 89, 90, 91, 92, 93},
    /* 15 */ {94, 95, 96, 97, 98, 99},
    /* 16 */ {100, 101, 102, 103, 104, 105},
    /* 17 */ {106, 107, 108, 109, 110, 111},
    /* 18 */ {112, 113, 114, 115, 116, 117},
    /* 19 */ {118, 119, 120, 121, 122, 123},
    /* 20 */ {124, 125, 126, 127, 128, 129},
    /* 21 */ {130, 131, 132, 133, 134, 135},
    /* 22 */ {136, 137, 138, 139, 140, 141},
    /* 23 */ {142, 143, 144, 145, 146, 147},
    /* 24 */ {148, 149, 150, 151, 152, 153},
    /* 25 */ {154, 155, 156, 157, 158, 159},
    /* 26 */ {160, 161, 162, 163, 164, 165},
    /* 27 */ {166, 167, 168, 169, 170, 171},
    /* 28 */ {172, 173, 174, 175, 176, 177},
    /* 29 */ {178, 179, 180, 181, 182, 183},
};

static const uint16_t sacch_parity_bit_offsets[22][6] = {
    /* 00 */ {184, 185, 186, 187, 188, 189},
    /* 01 */ {190, 191, 192, 193, 194, 195},
    /* 02 */ {196, 197, 198, 199, 200, 201},
    /* 03 */ {202, 203, 204, 205, 206, 207},
    /* 04 */ {208, 209, 210, 211, 212, 213},
    /* 05 */ {214, 215, 216, 217, 218, 219},
    /* 06 */ {220, 221, 222, 223, 224, 225},
    /* 07 */ {226, 227, 228, 229, 230, 231},
    /* 08 */ {232, 233, 234, 235, 236, 237},
    /* 09 */ {238, 239, 240, 241, 242, 243},
    /* 10 */ {246, 247, 248, 249, 250, 251},
    /* 11 */ {252, 253, 254, 255, 256, 257},
    /* 12 */ {258, 259, 260, 261, 262, 263},
    /* 13 */ {264, 265, 266, 267, 268, 269},
    /* 14 */ {270, 271, 272, 273, 274, 275},
    /* 15 */ {276, 277, 278, 279, 280, 281},
    /* 16 */ {282, 283, 284, 285, 286, 287},
    /* 17 */ {288, 289, 290, 291, 292, 293},
    /* 18 */ {294, 295, 296, 297, 298, 299},
    /* 19 */ {300, 301, 302, 303, 304, 305},
    /* 20 */ {306, 307, 308, 309, 310, 311},
    /* 21 */ {312, 313, 314, 315, 316, 317},
};

/**
 * Build dynamic erasure list for FACCH based on reliability.
 *
 * @param ts_counter     Current timeslot counter (0-3).
 * @param scrambled      1 if using descrambled buffers (p2xreliab), 0 for p2reliab.
 * @param erasures       Output: array to append erasures (must have space for at least 28 entries).
 * @param n_fixed        Number of fixed erasures already in the array.
 * @param max_add        Maximum dynamic erasures to add (recommend <=10 for FACCH).
 * @return Total erasure count (fixed + dynamic).
 */
int
p25p2_facch_soft_erasures(int ts_counter, int scrambled, int* erasures, int n_fixed, int max_add) {
    const uint8_t* reliab = scrambled ? p2xreliab : p2reliab;
    int thresh = get_erasure_threshold();
    int added = 0;
    int total = n_fixed;

    /* Check each of the 26 payload hexbits (RS positions 9-34) */
    for (int hb = 0; hb < 26 && added < max_add; hb++) {
        const uint16_t* bits = facch_payload_bit_offsets[hb];
        uint8_t rel = p25p2_hexbit_reliability(bits, ts_counter, reliab);

        if (rel < thresh) {
            int rs_pos = 9 + hb; /* RS codeword position */
            /* Check not already in erasure list */
            int dup = 0;
            for (int e = 0; e < total; e++) {
                if (erasures[e] == rs_pos) {
                    dup = 1;
                    break;
                }
            }
            if (!dup) {
                erasures[total++] = rs_pos;
                added++;
            }
        }
    }

    /* Check parity hexbits (RS positions 35-53) while under cap */
    for (int hb = 0; hb < 19 && added < max_add; hb++) {
        const uint16_t* bits = facch_parity_bit_offsets[hb];
        uint8_t rel = p25p2_hexbit_reliability(bits, ts_counter, reliab);

        if (rel < thresh) {
            int rs_pos = 35 + hb;
            int dup = 0;
            for (int e = 0; e < total; e++) {
                if (erasures[e] == rs_pos) {
                    dup = 1;
                    break;
                }
            }
            if (!dup) {
                erasures[total++] = rs_pos;
                added++;
            }
        }
    }

    return total;
}

/**
 * Build dynamic erasure list for SACCH based on reliability.
 *
 * @param ts_counter     Current timeslot counter (0-3).
 * @param scrambled      1 if using descrambled buffers, 0 otherwise.
 * @param erasures       Output: array to append erasures (must have space for at least 28 entries).
 * @param n_fixed        Number of fixed erasures already in the array.
 * @param max_add        Maximum dynamic erasures to add (recommend <=16 for SACCH).
 * @return Total erasure count (fixed + dynamic).
 */
int
p25p2_sacch_soft_erasures(int ts_counter, int scrambled, int* erasures, int n_fixed, int max_add) {
    const uint8_t* reliab = scrambled ? p2xreliab : p2reliab;
    int thresh = get_erasure_threshold();
    int added = 0;
    int total = n_fixed;

    /* Check each of the 30 payload hexbits (RS positions 5-34) */
    for (int hb = 0; hb < 30 && added < max_add; hb++) {
        const uint16_t* bits = sacch_payload_bit_offsets[hb];
        uint8_t rel = p25p2_hexbit_reliability(bits, ts_counter, reliab);

        if (rel < thresh) {
            int rs_pos = 5 + hb;
            int dup = 0;
            for (int e = 0; e < total; e++) {
                if (erasures[e] == rs_pos) {
                    dup = 1;
                    break;
                }
            }
            if (!dup) {
                erasures[total++] = rs_pos;
                added++;
            }
        }
    }

    /* Check parity hexbits (RS positions 35-56) while under cap */
    for (int hb = 0; hb < 22 && added < max_add; hb++) {
        const uint16_t* bits = sacch_parity_bit_offsets[hb];
        uint8_t rel = p25p2_hexbit_reliability(bits, ts_counter, reliab);

        if (rel < thresh) {
            int rs_pos = 35 + hb;
            int dup = 0;
            for (int e = 0; e < total; e++) {
                if (erasures[e] == rs_pos) {
                    dup = 1;
                    break;
                }
            }
            if (!dup) {
                erasures[total++] = rs_pos;
                added++;
            }
        }
    }

    return total;
}

/*
 * ESS bit offset tables.
 *
 * ESS_B (4V mode): 96 payload bits across 4 frames, 24 bits per frame.
 * Each frame contributes 4 hexbits at offset 148-171 relative to vc_counter.
 * Frame 0: vc_counter=0, bits 148-171
 * Frame 1: vc_counter=360, bits 508-531 (148+360)
 * Frame 2: vc_counter=720, bits 868-891
 * Frame 3: vc_counter=1080, bits 1228-1251
 */
static const uint16_t ess_b_payload_bit_offsets[16][6] = {
    /* Frame 0, hexbits 0-3 */
    /* 00 */ {148, 149, 150, 151, 152, 153},
    /* 01 */ {154, 155, 156, 157, 158, 159},
    /* 02 */ {160, 161, 162, 163, 164, 165},
    /* 03 */ {166, 167, 168, 169, 170, 171},
    /* Frame 1, hexbits 4-7 (add 360 to base) */
    /* 04 */ {508, 509, 510, 511, 512, 513},
    /* 05 */ {514, 515, 516, 517, 518, 519},
    /* 06 */ {520, 521, 522, 523, 524, 525},
    /* 07 */ {526, 527, 528, 529, 530, 531},
    /* Frame 2, hexbits 8-11 (add 720 to base) */
    /* 08 */ {868, 869, 870, 871, 872, 873},
    /* 09 */ {874, 875, 876, 877, 878, 879},
    /* 10 */ {880, 881, 882, 883, 884, 885},
    /* 11 */ {886, 887, 888, 889, 890, 891},
    /* Frame 3, hexbits 12-15 (add 1080 to base) */
    /* 12 */ {1228, 1229, 1230, 1231, 1232, 1233},
    /* 13 */ {1234, 1235, 1236, 1237, 1238, 1239},
    /* 14 */ {1240, 1241, 1242, 1243, 1244, 1245},
    /* 15 */ {1246, 1247, 1248, 1249, 1250, 1251},
};

/*
 * ESS_A (2V mode): 168 bits = 28 hexbits for parity.
 * First 96 bits (hexbits 0-15) at 148..243
 * Next 72 bits (hexbits 16-27) at 246..317
 * Note: bits 244-245 are UNUSED and must be skipped.
 */
static const uint16_t ess_a_parity_bit_offsets[28][6] = {
    /* Hexbits 0-15: bits 148-243 */
    /* 00 */ {148, 149, 150, 151, 152, 153},
    /* 01 */ {154, 155, 156, 157, 158, 159},
    /* 02 */ {160, 161, 162, 163, 164, 165},
    /* 03 */ {166, 167, 168, 169, 170, 171},
    /* 04 */ {172, 173, 174, 175, 176, 177},
    /* 05 */ {178, 179, 180, 181, 182, 183},
    /* 06 */ {184, 185, 186, 187, 188, 189},
    /* 07 */ {190, 191, 192, 193, 194, 195},
    /* 08 */ {196, 197, 198, 199, 200, 201},
    /* 09 */ {202, 203, 204, 205, 206, 207},
    /* 10 */ {208, 209, 210, 211, 212, 213},
    /* 11 */ {214, 215, 216, 217, 218, 219},
    /* 12 */ {220, 221, 222, 223, 224, 225},
    /* 13 */ {226, 227, 228, 229, 230, 231},
    /* 14 */ {232, 233, 234, 235, 236, 237},
    /* 15 */ {238, 239, 240, 241, 242, 243},
    /* Hexbits 16-27: bits 246-317 (skipping 244-245) */
    /* 16 */ {246, 247, 248, 249, 250, 251},
    /* 17 */ {252, 253, 254, 255, 256, 257},
    /* 18 */ {258, 259, 260, 261, 262, 263},
    /* 19 */ {264, 265, 266, 267, 268, 269},
    /* 20 */ {270, 271, 272, 273, 274, 275},
    /* 21 */ {276, 277, 278, 279, 280, 281},
    /* 22 */ {282, 283, 284, 285, 286, 287},
    /* 23 */ {288, 289, 290, 291, 292, 293},
    /* 24 */ {294, 295, 296, 297, 298, 299},
    /* 25 */ {300, 301, 302, 303, 304, 305},
    /* 26 */ {306, 307, 308, 309, 310, 311},
    /* 27 */ {312, 313, 314, 315, 316, 317},
};

/**
 * Build dynamic erasure list for ESS based on reliability.
 *
 * ESS uses RS(44,16,29):
 *   - 16 payload hexbits (ESS_B) at RS positions 0-15
 *   - 28 parity hexbits (ESS_A) at RS positions 16-43
 *
 * In 4V mode, ESS_B payload bits are spread across 4 frames.
 * In 2V mode, ESS_A parity bits come from a single 2V frame.
 *
 * @param ts_counter     Current timeslot counter (0-3).
 * @param is_4v          1 for ESS_B (4V mode), 0 for ESS_A parity (2V mode).
 * @param erasures       Output: array to append erasures (must have space for at least 44 entries).
 * @param n_fixed        Number of fixed erasures already in the array.
 * @param max_add        Maximum dynamic erasures to add.
 * @return Total erasure count (fixed + dynamic).
 */
int
p25p2_ess_soft_erasures(int ts_counter, int is_4v, int* erasures, int n_fixed, int max_add) {
    const uint8_t* reliab = p2xreliab; /* ESS uses descrambled buffer */
    int thresh = get_erasure_threshold();
    int added = 0;
    int total = n_fixed;

    if (is_4v) {
        /* 4V mode: check each of the 16 payload hexbits (RS positions 0-15) */
        for (int hb = 0; hb < 16 && added < max_add; hb++) {
            const uint16_t* bits = ess_b_payload_bit_offsets[hb];
            uint8_t rel = p25p2_hexbit_reliability(bits, ts_counter, reliab);

            if (rel < thresh) {
                int rs_pos = hb; /* ESS_B maps to positions 0-15 */
                /* Check not already in erasure list */
                int dup = 0;
                for (int e = 0; e < total; e++) {
                    if (erasures[e] == rs_pos) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) {
                    erasures[total++] = rs_pos;
                    added++;
                }
            }
        }
    } else {
        /* 2V mode: check each of the 28 parity hexbits (RS positions 16-43) */
        for (int hb = 0; hb < 28 && added < max_add; hb++) {
            const uint16_t* bits = ess_a_parity_bit_offsets[hb];
            uint8_t rel = p25p2_hexbit_reliability(bits, ts_counter, reliab);

            if (rel < thresh) {
                int rs_pos = 16 + hb; /* ESS_A maps to positions 16-43 */
                /* Check not already in erasure list */
                int dup = 0;
                for (int e = 0; e < total; e++) {
                    if (erasures[e] == rs_pos) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) {
                    erasures[total++] = rs_pos;
                    added++;
                }
            }
        }
    }

    return total;
}
