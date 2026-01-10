// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Use existing bridge in p25 CRC module
int crc16_lb_bridge(const int* payload, int len);

// Local copy of the CCITT-16 (0x1021) bitwise CRC used by LCCH (matches src/protocol/p25/p25_crc.c)
static unsigned short
crc16_ccitt_bits(const uint8_t bits[], unsigned int len) {
    unsigned short CRC = 0x0000; // initial
    const unsigned short Polynome = 0x1021;
    for (unsigned int i = 0; i < len; i++) {
        if (((CRC >> 15) & 1) ^ (bits[i] & 1)) {
            CRC = (CRC << 1) ^ Polynome;
        } else {
            CRC <<= 1;
        }
    }
    CRC ^= 0xFFFF; // invert
    return CRC;
}

static void
set_crc16_on_frame(int payload[190], int data_len_bits) {
    uint8_t tmp[190] = {0};
    for (int i = 0; i < data_len_bits; i++) {
        tmp[i] = (uint8_t)(payload[i] & 1);
    }
    unsigned short c = crc16_ccitt_bits(tmp, (unsigned int)data_len_bits);
    // write CRC MSB first into payload[data_len_bits..data_len_bits+15]
    for (int i = 0; i < 16; i++) {
        int bit = (c >> (15 - i)) & 1;
        payload[data_len_bits + i] = bit;
    }
}

int
main(void) {
    // Vector 1: all zeros (header/data), CRC over 164 bits
    {
        int bits[190];
        memset(bits, 0, sizeof(bits));
        set_crc16_on_frame(bits, 164);
        int rc = crc16_lb_bridge(bits, 164);
        if (rc != 0) {
            fprintf(stderr, "LCCH CRC16 all-zero vector failed (rc=%d)\n", rc);
            return 1;
        }
    }

    // Vector 1b: zero-length payload (header-only), CRC over 0 bits
    {
        int bits[190];
        memset(bits, 0, sizeof(bits));
        set_crc16_on_frame(bits, 0);
        int rc = crc16_lb_bridge(bits, 0);
        if (rc != 0) {
            fprintf(stderr, "LCCH CRC16 zero-length payload failed (rc=%d)\n", rc);
            return 11;
        }
        // Tamper one CRC bit and expect failure
        bits[0] ^= 1;
        rc = crc16_lb_bridge(bits, 0);
        if (rc == 0) {
            fprintf(stderr, "LCCH CRC16 zero-length tamper unexpectedly passed\n");
            return 12;
        }
    }

    // Vector 2: non-zero header and patterned payload, expect pass
    {
        int bits[190];
        memset(bits, 0, sizeof(bits));
        // Header: opcode=3 (011), offset=1 (001), res=00, b=10, mco=0x12 (010010)
        int p = 0;
        int hdr[16] = {
            0, 1, 1,         // opcode
            0, 0, 1,         // offset
            0, 0,            // reserved
            1, 0,            // b1b2
            0, 1, 0, 0, 1, 0 // mco_a
        };
        for (int i = 0; i < 16; i++) {
            bits[p++] = hdr[i];
        }
        // Payload fill to 164 bits total before CRC: use alternating 0/1 pattern
        for (; p < 164; p++) {
            bits[p] = (p & 1);
        }
        set_crc16_on_frame(bits, 164);
        int rc = crc16_lb_bridge(bits, 164);
        if (rc != 0) {
            fprintf(stderr, "LCCH CRC16 patterned vector failed (rc=%d)\n", rc);
            return 2;
        }
        // Tamper one bit and expect failure
        bits[32] ^= 1;
        rc = crc16_lb_bridge(bits, 164);
        if (rc == 0) {
            fprintf(stderr, "LCCH CRC16 tamper check unexpectedly passed\n");
            return 3;
        }
    }

    // Vector 3: short payload span (48 bits) with alternating header
    {
        int bits[190];
        memset(bits, 0, sizeof(bits));
        // Header bits: opcode=001, offset=000, res=00, b=10, mco=000000
        int p = 0;
        int hdr[16] = {0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i < 16; i++) {
            bits[p++] = hdr[i];
        }
        // 48 data bits
        for (int i = 0; i < 48; i++) {
            bits[p++] = (i & 1);
        }
        set_crc16_on_frame(bits, 16 + 48);
        int rc = crc16_lb_bridge(bits, 16 + 48);
        if (rc != 0) {
            fprintf(stderr, "LCCH CRC16 short span failed (rc=%d)\n", rc);
            return 4;
        }
        // Tamper and expect failure
        bits[20] ^= 1;
        rc = crc16_lb_bridge(bits, 16 + 48);
        if (rc == 0) {
            fprintf(stderr, "LCCH CRC16 short span tamper unexpectedly passed\n");
            return 5;
        }
    }

    fprintf(stderr, "LCCH CRC16 vectors passed\n");
    return 0;
}
