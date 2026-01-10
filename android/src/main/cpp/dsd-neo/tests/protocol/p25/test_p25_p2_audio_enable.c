// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 2 audio enable on MAC_PTT via SACCH.
 *
 * Constructs a valid SACCH MAC PDU with opcode=MAC_PTT (001) and zeroed
 * encryption fields so that audio gating is enabled for the affected slot.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

// Gate policy helper (from test shim)
int p25_test_p2_gate(int algid, unsigned long long key, int aes_loaded);

// Stubs to satisfy linked references from P25 Phase 2 XCCH path
void
watchdog_event_current(dsd_opts* opts, dsd_state* state, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)slot;
}

void
closeMbeOutFile(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
closeMbeOutFileR(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
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
struct RtlSdrContext* g_rtl_ctx = 0;

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

// Stubs for alias helpers referenced by MAC VPDU path (pulled in via test shim TU)
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
        buf[i] = bits[i] & 1;
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

static void
set_crc12_on_payload(int payload[180]) {
    uint8_t tmp[180];
    for (int i = 0; i < 168; i++) {
        tmp[i] = (uint8_t)(payload[i] & 1);
    }
    unsigned short c = crc12_bits(tmp, 168);
    for (int i = 0; i < 12; i++) {
        int bit = (c >> (11 - i)) & 1;
        payload[168 + i] = bit;
    }
}

static int
expect_eq(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    // Clear: ALGID=0 → allow
    rc |= expect_eq("clear gate", p25_test_p2_gate(0x00, 0ULL, 0), 1);
    // RC4 with key → allow; without key → mute
    rc |= expect_eq("RC4 with key", p25_test_p2_gate(0xAA, 0x123ULL, 0), 1);
    rc |= expect_eq("RC4 no key", p25_test_p2_gate(0xAA, 0ULL, 0), 0);
    // AES with aes_loaded=1 → allow; otherwise mute
    rc |= expect_eq("AES loaded", p25_test_p2_gate(0x84, 0ULL, 1), 1);
    rc |= expect_eq("AES not loaded", p25_test_p2_gate(0x84, 0ULL, 0), 0);

    return rc;
}
