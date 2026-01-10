// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/dsp/sync_hamming.h>

int
dsd_sync_hamming_distance(const char* buf, const char* pat, int len) {
    int ham = 0;
    for (int i = 0; i < len; i++) {
        int d = (unsigned char)buf[i];
        if (d >= '0' && d <= '3') {
            d -= '0';
        }
        int expect = (unsigned char)pat[i] - '0';
        if (d != expect) {
            ham++;
        }
    }
    return ham;
}

int
dsd_qpsk_sync_hamming_with_remaps(const char* buf, const char* pat_norm, const char* pat_inv, int len) {
    int ham_ident_n = 0, ham_ident_i = 0;
    int ham_invert_n = 0, ham_invert_i = 0;
    int ham_swap_n = 0, ham_swap_i = 0;
    int ham_xor3_n = 0, ham_xor3_i = 0;
    int ham_rot_n = 0, ham_rot_i = 0;
    for (int k = 0; k < len; k++) {
        int d = (unsigned char)buf[k];
        if (d >= '0' && d <= '3') {
            d -= '0';
        }
        int expect_n = pat_norm[k] - '0';
        int expect_i = pat_inv[k] - '0';
        int d_inv = (d == 0) ? 2 : (d == 1) ? 3 : (d == 2) ? 0 : 1;
        int d_swap = ((d & 1) << 1) | ((d & 2) >> 1); /* swap bit order */
        int d_xor3 = d ^ 0x3;                         /* bitwise not in 2-bit space */
        /* 90° rotation (cyclic remap 0->1->3->2->0) */
        int d_rot;
        switch (d & 0x3) {
            case 0: d_rot = 1; break;
            case 1: d_rot = 3; break;
            case 2: d_rot = 0; break;
            default: d_rot = 2; break; /* d==3 */
        }
        if (d != expect_n) {
            ham_ident_n++;
        }
        if (d != expect_i) {
            ham_ident_i++;
        }
        if (d_inv != expect_n) {
            ham_invert_n++;
        }
        if (d_inv != expect_i) {
            ham_invert_i++;
        }
        if (d_swap != expect_n) {
            ham_swap_n++;
        }
        if (d_swap != expect_i) {
            ham_swap_i++;
        }
        if (d_xor3 != expect_n) {
            ham_xor3_n++;
        }
        if (d_xor3 != expect_i) {
            ham_xor3_i++;
        }
        if (d_rot != expect_n) {
            ham_rot_n++;
        }
        if (d_rot != expect_i) {
            ham_rot_i++;
        }
    }
    int best = ham_ident_n;
    int ham_candidates[] = {ham_ident_i, ham_invert_n, ham_invert_i, ham_swap_n, ham_swap_i,
                            ham_xor3_n,  ham_xor3_i,   ham_rot_n,    ham_rot_i};
    for (int idx = 0; idx < (int)(sizeof(ham_candidates) / sizeof(ham_candidates[0])); idx++) {
        if (ham_candidates[idx] < best) {
            best = ham_candidates[idx];
        }
    }
    return best;
}
