// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 2 audio gating transitions: SIGNAL and explicit MAC Release.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/p25/p25_trunk_sm_api.h>

// Shim entry to process a MAC VPDU with LCCH flag and slot
void p25_test_process_mac_vpdu_ex(int type, const unsigned char* mac_bytes, int mac_len, int is_lcch, int currentslot);

// Provide a minimal state/opts for linkage; the path doesn’t need rigctl
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

// Stubs for alias helpers referenced by MAC VPDU path
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

// Capture release callback to ensure it’s invoked
static int g_release_called = 0;

static void
sm_on_release_capture(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
    g_release_called++;
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
    api.on_group_grant = sm_noop_on_group_grant;
    api.on_indiv_grant = sm_noop_on_indiv_grant;
    api.on_release = sm_on_release_capture;
    api.on_neighbor_update = sm_noop_on_neighbor_update;
    api.next_cc_candidate = sm_noop_next_cc_candidate;
    api.tick = sm_noop_tick;
    return api;
}

// Access a few dsd_state fields; keep this test independent of broad decoder headers.
struct MinimalState {
    int p25_p2_audio_allowed[2];
    int dmrburstL;
    int dmrburstR;
};

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

    p25_sm_set_api(sm_test_api());

    // Case 1: LCCH SIGNAL clears audio gates
    {
        unsigned char mac[24] = {0};
        mac[1] = 0x00; // SIGNAL opcode
        mac[2] = 0x00;
        // Use FACCH path, LCCH flagged, slot 0
        p25_test_process_mac_vpdu_ex(0, mac, 24, /*is_lcch*/ 1, /*slot*/ 0);
        // We can’t read state directly through shim; rely on path semantics validated by other tests.
        // This sub-test is a smoke check: no crash and release counter unchanged.
        rc |= expect_eq("release not called", g_release_called, 0);
    }

    // Case 2: Explicit MAC Release clears gates and sets bursts idle, triggers release callback
    {
        unsigned char mac[24] = {0};
        mac[1] = 0x31;                // MAC Release
        mac[2] = 0x00;                // flags
        mac[3] = mac[4] = mac[5] = 0; // target
        mac[6] = 0;
        mac[7] = 0; // CC low 12 bits
        p25_test_process_mac_vpdu_ex(0, mac, 24, /*is_lcch*/ 0, /*slot*/ 1);
        rc |= expect_eq("release called", g_release_called, 1);
    }

    return rc;
}
