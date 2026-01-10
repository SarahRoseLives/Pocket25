// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 1 LCW → Trunk SM dispatch tests.
 *
 * Verifies that an explicit Group Voice Channel Update (format 0x44) invokes
 * p25_sm_on_group_grant with correct channel/service/TG parameters under
 * retune-allowed policy, and does not dispatch when retune is disabled.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/p25/p25_trunk_sm_api.h>

void p25_test_invoke_lcw(const unsigned char* lcw_bits, int len, int enable_retune, long cc_freq);

// Stubs referenced by LCW path (alias helpers and streaming/rigctl)
void
apx_embedded_alias_header_phase1(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
apx_embedded_alias_blocks_phase1(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
l3h_embedded_alias_blocks_phase1(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
tait_iso7_embedded_alias_decode(dsd_opts* opts, dsd_state* state, uint8_t slot, int16_t len, uint8_t* input) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)len;
    (void)input;
}

void
apx_embedded_gps(dsd_opts* opts, dsd_state* state, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)lc_bits;
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

// Minimal replacement for ConvertBitIntoBytes (MSB-first packing)
uint64_t
ConvertBitIntoBytes(uint8_t* BufferIn, uint32_t BitLength) {
    uint64_t out = 0;
    for (uint32_t i = 0; i < BitLength; i++) {
        out = (out << 1) | (uint64_t)(BufferIn[i] & 1);
    }
    return out;
}

// Capture trunk SM group grant invocations
static int g_called = 0;
static int g_last_channel = -1;
static int g_last_svc = -1;
static int g_last_tg = -1;
static int g_last_src = -1;

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
    g_last_channel = channel;
    g_last_svc = svc_bits;
    g_last_tg = tg;
    g_last_src = src;
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

static void
set_bits_msb(uint8_t* bits, int start, int width, unsigned value) {
    for (int i = 0; i < width; i++) {
        int bit = (value >> (width - 1 - i)) & 1;
        bits[start + i] = (uint8_t)bit;
    }
}

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

    p25_sm_set_api(sm_test_api());

    // Build LCW bits for format 0x44 (Group Voice Channel Update – Explicit)
    // Layout (bit indices):
    //   [0..7]  format (0x44), with bit0=PF=0, bit1=SF=1
    //   [8..15] MFID (0)
    //   [16..23] SVC options
    //   [24..39] Group ID
    //   [40..55] CHAN-T (iden:4 | chan:12)
    //   [56..71] CHAN-R (unused here)
    uint8_t lcw[72];
    memset(lcw, 0, sizeof(lcw));
    const int svc = 0x00;  // unencrypted
    const int tg = 0x1234; // talkgroup
    const int ch = 0x100A; // iden=1, chan=0x00A (10)
    set_bits_msb(lcw, 0, 8, 0x44);
    set_bits_msb(lcw, 8, 8, 0x00);
    set_bits_msb(lcw, 16, 8, (unsigned)svc);
    set_bits_msb(lcw, 24, 16, (unsigned)tg);
    set_bits_msb(lcw, 40, 16, (unsigned)ch);
    // CHAN-R left zero

    // Subcase A: retune disabled → no SM dispatch
    g_called = 0;
    g_last_channel = g_last_svc = g_last_tg = g_last_src = -1;
    p25_test_invoke_lcw(lcw, 72, /*enable_retune*/ 0, /*cc*/ 851000000);
    rc |= expect_eq_int("no-dispatch when disabled", g_called, 0);

    // Subcase B: retune enabled and CC set → expect dispatch with exact fields
    g_called = 0;
    g_last_channel = g_last_svc = g_last_tg = g_last_src = -1;
    p25_test_invoke_lcw(lcw, 72, /*enable_retune*/ 1, /*cc*/ 851000000);
    rc |= expect_eq_int("dispatch called", g_called, 1);
    rc |= expect_eq_int("channel", g_last_channel, ch);
    rc |= expect_eq_int("svc", g_last_svc, svc);
    rc |= expect_eq_int("tg", g_last_tg, tg);
    // source may be 0 unless prior LCW set it
    rc |= expect_eq_int("src default", g_last_src, 0);

    // Gating cases are covered in a separate test without overriding
    // p25_sm_on_group_grant so the implementation’s gating logic runs.

    return rc;
}
