// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/p25/p25_lsd.h>

// Use parity map from the P25 proto lib
extern uint8_t lsd_parity[256];

static void
byte_to_bits_msbf(uint8_t b, uint8_t out8[8]) {
    for (int i = 0; i < 8; i++) {
        out8[i] = (uint8_t)((b >> (7 - i)) & 1);
    }
}

static uint8_t
bits_to_byte_msbf(const uint8_t in8[8]) {
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) {
        b = (uint8_t)((b << 1) | (in8[i] & 1));
    }
    return b;
}

static void
make_codeword(uint8_t data, uint8_t out_bits16[16]) {
    uint8_t p = lsd_parity[data];
    byte_to_bits_msbf(data, out_bits16 + 0);
    byte_to_bits_msbf(p, out_bits16 + 8);
}

static int
eq_bits(const uint8_t* a, const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) {
        if ((a[i] & 1) != (b[i] & 1)) {
            return 0;
        }
    }
    return 1;
}

int
main(void) {
    uint8_t datas[] = {0x00, 0x12, 0xA5, 0xFF};
    uint8_t cw[16];
    uint8_t tmp[16];

    for (unsigned int t = 0; t < sizeof(datas); t++) {
        uint8_t d = datas[t];
        make_codeword(d, cw);

        // 1) Valid word passes and remains unchanged
        memcpy(tmp, cw, 16);
        int rc = p25_lsd_fec_16x8(tmp);
        if (rc != 1 || !eq_bits(tmp, cw, 16)) {
            fprintf(stderr, "valid cw failed: d=%02X rc=%d\n", d, rc);
            return 10;
        }

        // 2) Single-bit flips across all 16 positions are corrected
        for (int i = 0; i < 16; i++) {
            memcpy(tmp, cw, 16);
            tmp[i] ^= 1; // flip bit
            rc = p25_lsd_fec_16x8(tmp);
            if (rc != 1 || !eq_bits(tmp, cw, 16)) {
                fprintf(stderr, "single-bit correction failed at pos %d for d=%02X rc=%d\n", i, d, rc);
                return 20 + i;
            }
        }

        // 3) Two-bit flips should be detected as uncorrectable
        memcpy(tmp, cw, 16);
        tmp[0] ^= 1;
        tmp[8] ^= 1;
        rc = p25_lsd_fec_16x8(tmp);
        if (rc != 0) {
            fprintf(stderr, "two-bit error not detected for d=%02X rc=%d\n", d, rc);
            return 40;
        }
        memcpy(tmp, cw, 16);
        tmp[3] ^= 1;
        tmp[5] ^= 1;
        rc = p25_lsd_fec_16x8(tmp);
        if (rc != 0) {
            fprintf(stderr, "two-bit error(2) not detected for d=%02X rc=%d\n", d, rc);
            return 41;
        }
    }

    return 0;
}
