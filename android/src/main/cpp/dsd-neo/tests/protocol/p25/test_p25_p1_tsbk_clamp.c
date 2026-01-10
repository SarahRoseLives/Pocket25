// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25p1 TSBK clamp: ensure TSBK-mapped Group Voice Grant does not tune
 * when channel→frequency mapping is invalid (unseeded iden).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/p25/p25_trunk_sm_api.h>

// Stubs referenced by linked paths
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

static void
sm_noop_init(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

static void
sm_noop_on_group_grant(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int tg, int src) {
    (void)opts;
    (void)state;
    (void)channel;
    (void)svc_bits;
    (void)tg;
    (void)src;
}

static void
sm_noop_on_indiv_grant(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int dst, int src) {
    (void)opts;
    (void)state;
    (void)channel;
    (void)svc_bits;
    (void)dst;
    (void)src;
}

static void
sm_noop_on_release(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

static void
sm_noop_on_neighbor_update(dsd_opts* opts, dsd_state* state, const long* freqs, int count) {
    (void)opts;
    (void)state;
    (void)freqs;
    (void)count;
}

static void
sm_noop_tick(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

static int
sm_noop_next_cc_candidate(dsd_state* state, long* out_freq) {
    (void)state;
    (void)out_freq;
    return 0;
}

static p25_sm_api
sm_noop_api(void) {
    p25_sm_api api = {0};
    api.init = sm_noop_init;
    api.on_group_grant = sm_noop_on_group_grant;
    api.on_indiv_grant = sm_noop_on_indiv_grant;
    api.on_release = sm_noop_on_release;
    api.on_neighbor_update = sm_noop_on_neighbor_update;
    api.next_cc_candidate = sm_noop_next_cc_candidate;
    api.tick = sm_noop_tick;
    return api;
}

// Shim to invoke VPDU and capture tuned flag and vc0
void p25_test_invoke_mac_vpdu_capture(const unsigned char* mac_bytes, int mac_len, int p25_trunk, long p25_cc_freq,
                                      int iden, int type, int tdma, long base, int spac, long* out_vc0, int* out_tuned);

static int
expect_true(const char* tag, int cond) {
    if (!cond) {
        fprintf(stderr, "%s: expected true\n", tag);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    p25_sm_set_api(sm_noop_api());
    // Build a TSBK-mapped vPDU Group Voice Channel Grant with channel 0x100A (iden=1)
    unsigned char mac[24] = {0};
    mac[0] = 0x07; // TSBK marker
    mac[1] = 0x40; // Group Voice Channel Grant
    mac[2] = 0x00; // svc
    mac[3] = 0x10; // channel hi
    mac[4] = 0x0A; // channel lo
    mac[5] = 0x45;
    mac[6] = 0x67; // group
    mac[7] = 0x00;
    mac[8] = 0x00;
    mac[9] = 0x01; // source

    long vc0 = -1;
    int tuned = -1;
    // Seed only iden=0 (not matching channel’s iden=1). Expect no tuning and vc0 remains 0.
    p25_test_invoke_mac_vpdu_capture(mac, 10, /*trunk*/ 1, /*cc*/ 851000000,
                                     /*iden*/ 0, /*type*/ 1, /*tdma*/ 0, /*base*/ 851000000 / 5, /*spac*/ 100, &vc0,
                                     &tuned);
    rc |= expect_true("not tuned", tuned == 0);
    rc |= expect_true("vc0 not set", vc0 == 0);
    return rc;
}
