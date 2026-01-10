// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 1 LDU header gating tests.
 *
 * Validates early audio gating decisions based on ALGID and key presence:
 *  - Clear (0x00/0x80) => allow
 *  - RC4/DES/DES-XL (0xAA/0x81/0x9F) => allow only when R != 0
 *  - AES-256/AES-128 (0x84/0x89) => allow only when AES key loaded
 *  - Other non-zero ALGIDs => mute
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Forward-declare minimal types and provide stubs for linked symbols
typedef struct dsd_opts dsd_opts;
typedef struct dsd_state dsd_state;

void
unpack_byte_array_into_bit_array(uint8_t* input, uint8_t* output, int len) {
    (void)input;
    (void)output;
    (void)len;
}

void
apx_embedded_alias_header_phase2(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
apx_embedded_alias_blocks_phase2(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
l3h_embedded_alias_decode(dsd_opts* opts, dsd_state* state, uint8_t slot, int16_t len, uint8_t* input) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)len;
    (void)input;
}

void
nmea_harris(dsd_opts* opts, dsd_state* state, uint8_t* input, uint32_t src, int slot) {
    (void)opts;
    (void)state;
    (void)input;
    (void)src;
    (void)slot;
}

bool
SetFreq(int sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return false;
}

bool
SetModulation(int sockfd, int bandwidth) {
    (void)sockfd;
    (void)bandwidth;
    return false;
}

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}
struct RtlSdrContext* g_rtl_ctx = 0;

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

int p25_test_p1_ldu_gate(int algid, unsigned long long R, int aes_loaded);

static int
expect_eq_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;

    // Clear audio allowed
    rc |= expect_eq_int("ALGID 0 clear", p25_test_p1_ldu_gate(0x00, 0, 0), 1);
    rc |= expect_eq_int("ALGID 0x80 clear", p25_test_p1_ldu_gate(0x80, 0, 0), 1);

    // RC4/DES/DES-XL: require R != 0
    rc |= expect_eq_int("RC4 no key", p25_test_p1_ldu_gate(0xAA, 0, 0), 0);
    rc |= expect_eq_int("RC4 with key", p25_test_p1_ldu_gate(0xAA, 0x123, 0), 1);
    rc |= expect_eq_int("DES no key", p25_test_p1_ldu_gate(0x81, 0, 0), 0);
    rc |= expect_eq_int("DES with key", p25_test_p1_ldu_gate(0x81, 0x1, 0), 1);
    rc |= expect_eq_int("DES-XL no key", p25_test_p1_ldu_gate(0x9F, 0, 0), 0);
    rc |= expect_eq_int("DES-XL with key", p25_test_p1_ldu_gate(0x9F, 0x999, 0), 1);

    // AES: require AES key loaded
    rc |= expect_eq_int("AES-256 not loaded", p25_test_p1_ldu_gate(0x84, 0, 0), 0);
    rc |= expect_eq_int("AES-256 loaded", p25_test_p1_ldu_gate(0x84, 0, 1), 1);
    rc |= expect_eq_int("AES-128 not loaded", p25_test_p1_ldu_gate(0x89, 0, 0), 0);
    rc |= expect_eq_int("AES-128 loaded", p25_test_p1_ldu_gate(0x89, 0, 1), 1);

    // Unknown non-zero ALGID => mute
    rc |= expect_eq_int("Unknown algid", p25_test_p1_ldu_gate(0x7E, 0, 0), 0);

    return rc;
}
