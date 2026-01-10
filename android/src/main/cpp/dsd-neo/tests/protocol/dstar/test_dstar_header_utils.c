// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <dsd-neo/protocol/dstar/dstar_header_utils.h>

static void
convolution_encode(const int* bits, size_t bit_count, int* symbols) {
    int s0 = 0;
    int s1 = 0;
    for (size_t i = 0; i < bit_count; i++) {
        int b = bits[i] & 0x1;
        symbols[2 * i] = b ^ s0 ^ s1; // G1 = 111 (octal 7)
        symbols[2 * i + 1] = b ^ s1;  // G2 = 101 (octal 5)
        s1 = s0;
        s0 = b;
    }
}

// Inverse of dstar_deinterleave_header_bits: map payload order -> on-air order.
static void
dstar_interleave_header_bits(const int* in, int* out, size_t bit_count) {
    size_t k = 0;
    for (size_t i = 0; i < bit_count; i++) {
        out[i] = in[k];
        k += 24;
        if (k >= 672) {
            k -= 671;
        } else if (k >= 660) {
            k -= 647;
        }
    }
}

static void
test_scrambler_roundtrip(void) {
    int original[DSD_DSTAR_HEADER_CODED_BITS];
    int scrambled[DSD_DSTAR_HEADER_CODED_BITS];
    int recovered[DSD_DSTAR_HEADER_CODED_BITS];

    for (size_t i = 0; i < DSD_DSTAR_HEADER_CODED_BITS; i++) {
        original[i] = (int)((i * 3 + 1) & 0x1);
    }

    dstar_scramble_header_bits(original, scrambled, DSD_DSTAR_HEADER_CODED_BITS);
    dstar_scramble_header_bits(scrambled, recovered, DSD_DSTAR_HEADER_CODED_BITS);

    assert(memcmp(original, recovered, sizeof(original)) == 0);
}

static void
test_interleave_roundtrip(void) {
    int coded[DSD_DSTAR_HEADER_CODED_BITS];
    int on_air[DSD_DSTAR_HEADER_CODED_BITS];
    int recovered[DSD_DSTAR_HEADER_CODED_BITS];

    for (size_t i = 0; i < DSD_DSTAR_HEADER_CODED_BITS; i++) {
        coded[i] = (int)((i + 5) & 0x1);
    }

    dstar_interleave_header_bits(coded, on_air, DSD_DSTAR_HEADER_CODED_BITS);
    dstar_deinterleave_header_bits(on_air, recovered, DSD_DSTAR_HEADER_CODED_BITS);

    assert(memcmp(coded, recovered, sizeof(coded)) == 0);
}

static void
test_decode_pipeline(void) {
    int info_bits[DSD_DSTAR_HEADER_INFO_BITS];
    int coded[DSD_DSTAR_HEADER_CODED_BITS];
    int interleaved[DSD_DSTAR_HEADER_CODED_BITS];
    int scrambled[DSD_DSTAR_HEADER_CODED_BITS];
    int rx_buf[DSD_DSTAR_HEADER_CODED_BITS];
    int decoded[DSD_DSTAR_HEADER_INFO_BITS];

    for (size_t i = 0; i < DSD_DSTAR_HEADER_INFO_BITS; i++) {
        info_bits[i] = (int)((i * 7 + 3) & 0x1);
    }

    convolution_encode(info_bits, DSD_DSTAR_HEADER_INFO_BITS, coded);
    dstar_interleave_header_bits(coded, interleaved, DSD_DSTAR_HEADER_CODED_BITS);
    dstar_scramble_header_bits(interleaved, scrambled, DSD_DSTAR_HEADER_CODED_BITS);

    // Receiver path
    dstar_scramble_header_bits(scrambled, rx_buf, DSD_DSTAR_HEADER_CODED_BITS);
    dstar_deinterleave_header_bits(rx_buf, interleaved, DSD_DSTAR_HEADER_CODED_BITS);
    size_t out_len =
        dstar_header_viterbi_decode(interleaved, DSD_DSTAR_HEADER_CODED_BITS, decoded, DSD_DSTAR_HEADER_INFO_BITS);

    assert(out_len == DSD_DSTAR_HEADER_INFO_BITS);
    assert(memcmp(info_bits, decoded, sizeof(info_bits)) == 0);
}

static void
test_crc16(void) {
    const uint8_t payload[] = "123456789";
    // CRC-16/X25 known value from the spec.
    assert(dstar_crc16(payload, sizeof(payload) - 1) == 0x906e);
}

int
main(void) {
    test_scrambler_roundtrip();
    test_interleave_roundtrip();
    test_decode_pipeline();
    test_crc16();
    return 0;
}
