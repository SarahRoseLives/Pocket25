// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 1 TSBK → vPDU bridge test (Group Voice Channel Grant).
 *
 * Builds a minimal TSBK-mapped vPDU (DUID=0x07, opcode=0x40) and feeds it to
 * process_MAC_VPDU. Verifies that p25_sm_on_group_grant is invoked with the
 * expected channel, service bits, talkgroup, and source when trunking is
 * enabled and IDEN tables allow channel→frequency mapping.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/p25/p25_trunk_sm_api.h>

// Alias decode helper stubs referenced by VPDU handler
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

// Rigctl/rtl stubs referenced by linked objects
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

// Trunk SM hooks we assert on
static int g_called = 0;
static int g_channel = -1;
static int g_svc = -1;
static int g_tg = -1;
static int g_src = -1;

static void
sm_noop_init(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

static void
sm_on_group_grant_capture(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int tg, int src) {
    (void)opts;
    (void)state;
    g_called++;
    g_channel = channel;
    g_svc = svc_bits;
    g_tg = tg;
    g_src = src;
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
sm_test_api(void) {
    p25_sm_api api = {0};
    api.init = sm_noop_init;
    api.on_group_grant = sm_on_group_grant_capture;
    api.on_indiv_grant = sm_noop_on_indiv_grant;
    api.on_release = sm_noop_on_release;
    api.on_neighbor_update = sm_noop_on_neighbor_update;
    api.next_cc_candidate = sm_noop_next_cc_candidate;
    api.tick = sm_noop_tick;
    return api;
}

static int
expect_eq(const char* tag, long got, long want) {
    if (got != want) {
        fprintf(stderr, "%s: got %ld want %ld\n", tag, got, want);
        return 1;
    }
    return 0;
}

// Test shim entry (implemented in src/protocol/p25/p25_test_shim.c)
void p25_test_invoke_mac_vpdu_with_state(const unsigned char* mac_bytes, int mac_len, int p25_trunk, long p25_cc_freq,
                                         int iden, int type, int tdma, long base, int spac);

int
main(void) {
    int rc = 0;

    p25_sm_set_api(sm_test_api());

    // Build TSBK-mapped vPDU: DUID=0x07, opcode=0x40 (Group Voice)
    // svc=0x00 (clear), channel=0x100A (iden=1, ch=10), group=0x4567, source=0x00ABCDEF
    unsigned char mac[24] = {0};
    mac[0] = 0x07; // TSBK marker
    mac[1] = 0x40; // Group Voice Channel Grant (MAC-coded)
    mac[2] = 0x00; // svc bits
    mac[3] = 0x10; // channel hi
    mac[4] = 0x0A; // channel lo
    mac[5] = 0x45; // group hi
    mac[6] = 0x67; // group lo
    mac[7] = 0xAB; // source hi
    mac[8] = 0xCD;
    mac[9] = 0xEF; // source lo

    // Invoke decoder under a seeded state (trunking enabled, iden populated)
    p25_test_invoke_mac_vpdu_with_state((const unsigned char*)mac, 10, /*trunk*/ 1, /*cc*/ 851000000,
                                        /*iden*/ 1, /*type*/ 1, /*tdma*/ 0, /*base*/ 851000000 / 5, /*spac*/ 100);

    // Expect trunk SM callback with same channel/svc/group/src
    rc |= expect_eq("grant called", g_called, 1);
    rc |= expect_eq("grant channel", g_channel, 0x100A);
    rc |= expect_eq("grant svc", g_svc, 0x00);
    rc |= expect_eq("grant tg", g_tg, 0x4567);
    rc |= expect_eq("grant src", g_src, 0x00ABCDEF);

    // Case 2: Non-zero service options propagate to trunk SM
    g_called = 0;
    g_channel = g_svc = g_tg = g_src = -1;
    unsigned char mac2[24] = {0};
    mac2[0] = 0x07; // TSBK marker
    mac2[1] = 0x40; // Group Voice Channel Grant
    mac2[2] = 0x87; // SVC: Emergency, priority=7 (no ENC gating)
    mac2[3] = 0x10;
    mac2[4] = 0x0A;
    mac2[5] = 0x12;
    mac2[6] = 0x34;
    mac2[7] = 0x00;
    mac2[8] = 0x00;
    mac2[9] = 0x01;
    p25_test_invoke_mac_vpdu_with_state((const unsigned char*)mac2, 10, /*trunk*/ 1, /*cc*/ 851000000,
                                        /*iden*/ 1, /*type*/ 1, /*tdma*/ 0, /*base*/ 851000000 / 5, /*spac*/ 100);
    rc |= expect_eq("grant2 called", g_called, 1);
    rc |= expect_eq("grant2 svc", g_svc, 0x87);
    rc |= expect_eq("grant2 channel", g_channel, 0x100A);

    return rc;
}
