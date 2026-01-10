// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 CRC12/CRC16 smoke tests (LCCH/SACCH/FACCH bit-span checks)
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Bridges from src/protocol/p25/p25_crc.c
int crc12_xb_bridge(const int* payload, int len);
int crc16_lb_bridge(const int* payload, int len);

// Local CRC12 (matches src/protocol/p25/p25_crc.c)
static unsigned short
crc12_bits(const uint8_t bits[], unsigned int len) {
    uint16_t crc = 0;
    static const unsigned int K = 12;
    static const uint8_t poly[13] = {1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1};
    uint8_t buf[256];
    if (len + K > sizeof(buf)) {
        return 0;
    }
    memset(buf, 0, sizeof(buf));
    for (unsigned int i = 0; i < len; i++) {
        buf[i] = bits[i];
    }
    for (unsigned int i = 0; i < len; i++) {
        if (buf[i]) {
            for (unsigned int j = 0; j < K + 1; j++) {
                buf[i + j] ^= poly[j];
            }
        }
    }
    for (unsigned int i = 0; i < K; i++) {
        crc = (uint16_t)((crc << 1) + buf[len + i]);
    }
    return crc ^ 0xFFF;
}

// Local CRC16 CCITT (matches src/protocol/p25/p25_crc.c)
static unsigned short
crc16_ccitt_bits(const uint8_t bits[], unsigned int len) {
    unsigned short CRC = 0x0000;
    const unsigned short Polynome = 0x1021;
    for (unsigned int i = 0; i < len; i++) {
        if (((CRC >> 15) & 1) ^ (bits[i] & 1)) {
            CRC = (unsigned short)((CRC << 1) ^ Polynome);
        } else {
            CRC = (unsigned short)(CRC << 1);
        }
    }
    CRC ^= 0xFFFF;
    return CRC;
}

static void
set_crc12_on_frame(int payload[190], int data_len_bits) {
    uint8_t tmp[190] = {0};
    for (int i = 0; i < data_len_bits; i++) {
        tmp[i] = (uint8_t)(payload[i] & 1);
    }
    unsigned short c = crc12_bits(tmp, (unsigned int)data_len_bits);
    for (int i = 0; i < 12; i++) {
        int bit = (c >> (11 - i)) & 1;
        payload[data_len_bits + i] = bit;
    }
}

static void
set_crc16_on_frame(int payload[190], int data_len_bits) {
    uint8_t tmp[190] = {0};
    for (int i = 0; i < data_len_bits; i++) {
        tmp[i] = (uint8_t)(payload[i] & 1);
    }
    unsigned short c = crc16_ccitt_bits(tmp, (unsigned int)data_len_bits);
    for (int i = 0; i < 16; i++) {
        int bit = (c >> (15 - i)) & 1;
        payload[data_len_bits + i] = bit;
    }
}

int
main(void) {
    // CRC16 (LCCH-like span): two vectors and a tamper check
    {
        int bits[190];
        memset(bits, 0, sizeof(bits));
        set_crc16_on_frame(bits, 164);
        if (crc16_lb_bridge(bits, 164) != 0) {
            fprintf(stderr, "CRC16 all-zero failed\n");
            return 1;
        }
    }
    {
        int bits[190];
        memset(bits, 0, sizeof(bits));
        int p = 0;
        for (; p < 164; p++) {
            bits[p] = (p & 1);
        }
        set_crc16_on_frame(bits, 164);
        if (crc16_lb_bridge(bits, 164) != 0) {
            fprintf(stderr, "CRC16 patterned failed\n");
            return 2;
        }
        bits[17] ^= 1; // tamper
        if (crc16_lb_bridge(bits, 164) == 0) {
            fprintf(stderr, "CRC16 tamper unexpectedly passed\n");
            return 3;
        }
    }

    // CRC12 (xCCH-like span): two vectors and a tamper check
    {
        int bits[190];
        memset(bits, 0, sizeof(bits));
        // keep within bridge buffer (len+12 <= 190)
        const int len12 = 176;
        set_crc12_on_frame(bits, len12);
        if (crc12_xb_bridge(bits, len12) != 0) {
            fprintf(stderr, "CRC12 all-zero failed\n");
            return 4;
        }
    }
    {
        int bits[190];
        memset(bits, 0, sizeof(bits));
        const int len12 = 176;
        for (int i = 0; i < len12; i++) {
            bits[i] = ((i * 3) ^ (i >> 1)) & 1;
        }
        set_crc12_on_frame(bits, len12);
        if (crc12_xb_bridge(bits, len12) != 0) {
            fprintf(stderr, "CRC12 patterned failed\n");
            return 5;
        }
        bits[77] ^= 1; // tamper
        if (crc12_xb_bridge(bits, len12) == 0) {
            fprintf(stderr, "CRC12 tamper unexpectedly passed\n");
            return 6;
        }
    }

    fprintf(stderr, "CRC12/16 smoke passed\n");
    return 0;
}
