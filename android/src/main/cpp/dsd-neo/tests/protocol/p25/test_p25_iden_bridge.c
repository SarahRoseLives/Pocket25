// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Verify P25 Phase 1 MBT → MAC bridging for Identifier Update PDUs populates
 * IDEN tables and drives the channel→frequency calculator.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/p25/p25_trunk_sm_api.h>

// Test shim helper (exposed by protocol library)
int p25_test_mbt_iden_bridge(const unsigned char* mbt, int mbt_len, long* out_base, int* out_spac, int* out_type,
                             int* out_tdma, long* out_freq);

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

// Additional stubs referenced by MAC VPDU path (unused in this test)
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

static int
expect_eq_long(const char* tag, long got, long want) {
    if (got != want) {
        fprintf(stderr, "%s: got %ld want %ld\n", tag, got, want);
        return 1;
    }
    return 0;
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

    p25_sm_set_api(sm_noop_api());

    // Craft a minimal ALT MBT PDU carrying Identifier Update (UHF/VHF, opcode 0x74)
    // IDEN=1, spacing=100 (12.5 kHz), base=851.000000 MHz (base field in 5 Hz units)
    uint8_t mbt[48];
    memset(mbt, 0, sizeof(mbt));

    mbt[0] = 0x17; // ALT format
    mbt[2] = 0x00; // MFID (standard)
    mbt[6] = 0x02; // blks=2 (3x12=36 total bytes), ample for payload
    mbt[7] = 0x74; // Identifier Update VHF/UHF (MAC-coded opcode)

    // Payload directly after opcode (bridging places payload at MAC[2..])
    // Byte layout per decoder in p25p2_vpdu.c (for 0x74):
    // [2]: (IDEN<<4) | BW, [3..4]: tx_off (14 bits), [4..5]: spacing, [6..9]: base (32 bits)
    mbt[8] = 0x10;  // IDEN=1, BW=0
    mbt[9] = 0x00;  // tx_off hi
    mbt[10] = 0x00; // tx_off lo + spacing hi (top 2 bits)
    mbt[11] = 0x64; // spacing lo = 100 (12.5 kHz)
    mbt[12] = 0x0A; // base (851000000 / 5) = 0x0A250BC0
    mbt[13] = 0x25;
    mbt[14] = 0x0B;
    mbt[15] = 0xC0;

    long base = 0;
    int spac = 0;
    int type = -1;
    int tdma = -1;
    long freq = 0;

    // Exercise the bridge via the shim and extract state results
    int sh = p25_test_mbt_iden_bridge(mbt, (int)sizeof(mbt), &base, &spac, &type, &tdma, &freq);
    if (sh != 0) {
        fprintf(stderr, "shim invocation failed (%d)\n", sh);
        return 99;
    }

    // Verify IDEN tables were populated (iden=1)
    rc |= expect_eq_int("chan_type[1]", type, 1);
    rc |= expect_eq_int("chan_tdma[1]", tdma, 0);
    rc |= expect_eq_long("spacing[1]", spac, 100);
    rc |= expect_eq_long("base[1]", base, 851000000 / 5);

    // Verify frequency calculation (iden=1, ch=10)
    long want_freq = 851000000 + 10 * 100 * 125; // 851.125 MHz
    rc |= expect_eq_long("freq(0x100A)", freq, want_freq);

    return rc;
}
