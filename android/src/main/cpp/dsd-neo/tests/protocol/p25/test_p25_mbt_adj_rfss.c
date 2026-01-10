// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 1 MBT decode tests: RFSS Status Broadcast (0x3A) and
 * Adjacent Status Broadcast (0x3C). Verifies neighbor frequency updates
 * using pre-seeded IDEN tables.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/p25/p25_trunk_sm_api.h>

// Shim: decode an MBT with pre-seeded iden tables
int p25_test_decode_mbt_with_iden(const unsigned char* mbt, int mbt_len, int iden, int type, int tdma, long base,
                                  int spac, long* out_cc, long* out_wacn, int* out_sysid);

static void
sm_noop_init(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

static void
sm_noop_on_group_grant(dsd_opts* o, dsd_state* s, int ch, int svc, int tg, int src) {
    (void)o;
    (void)s;
    (void)ch;
    (void)svc;
    (void)tg;
    (void)src;
}

static void
sm_noop_on_indiv_grant(dsd_opts* o, dsd_state* s, int ch, int svc, int dst, int src) {
    (void)o;
    (void)s;
    (void)ch;
    (void)svc;
    (void)dst;
    (void)src;
}

static void
sm_noop_on_release(dsd_opts* o, dsd_state* s) {
    (void)o;
    (void)s;
}

static void
sm_noop_tick(dsd_opts* o, dsd_state* s) {
    (void)o;
    (void)s;
}

static int
sm_noop_next_cc_candidate(dsd_state* s, long* f) {
    (void)s;
    (void)f;
    return 0;
}

// Capture last neighbor-update frequencies
static long g_neigh[16];
static int g_neigh_count = 0;

static void
sm_on_neighbor_update_capture(dsd_opts* opts, dsd_state* state, const long* freqs, int count) {
    (void)opts;
    (void)state;
    if (count > (int)(sizeof g_neigh / sizeof g_neigh[0])) {
        count = (int)(sizeof g_neigh / sizeof g_neigh[0]);
    }
    for (int i = 0; i < count; i++) {
        g_neigh[i] = freqs[i];
    }
    g_neigh_count = count;
}

static p25_sm_api
sm_test_api(void) {
    p25_sm_api api = {0};
    api.init = sm_noop_init;
    api.on_group_grant = sm_noop_on_group_grant;
    api.on_indiv_grant = sm_noop_on_indiv_grant;
    api.on_release = sm_noop_on_release;
    api.on_neighbor_update = sm_on_neighbor_update_capture;
    api.next_cc_candidate = sm_noop_next_cc_candidate;
    api.tick = sm_noop_tick;
    return api;
}

bool
SetFreq(int sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return false;
}

bool
SetModulation(int sockfd, int bw) {
    (void)sockfd;
    (void)bw;
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

int
main(void) {
    int rc = 0;

    p25_sm_set_api(sm_test_api());

    // Common iden config: iden=1 FDMA, base=851.000 MHz, spacing=12.5 kHz
    const int iden = 1, type = 1, tdma = 0;
    const long base5 = 851000000 / 5; // store base in units of 5 Hz
    const int spac125 = 100;          // 100*125 = 12.5 kHz

    // Case A: RFSS Status Broadcast (0x3A)
    {
        uint8_t mbt[48];
        memset(mbt, 0, sizeof(mbt));
        mbt[0] = 0x17;  // ALT format
        mbt[2] = 0x00;  // MFID standard
        mbt[3] = 0x01;  // LRA
        mbt[4] = 0x01;  // SYSID hi-nibble
        mbt[5] = 0x23;  // SYSID lo
        mbt[6] = 0x02;  // blks
        mbt[7] = 0x3A;  // opcode
        mbt[12] = 0x02; // RFSS
        mbt[13] = 0x03; // SITE
        mbt[14] = 0x10; // CHAN-T hi
        mbt[15] = 0x01; // CHAN-T lo (0x1001)
        mbt[16] = 0x10; // CHAN-R hi
        mbt[17] = 0x02; // CHAN-R lo (0x1002)
        mbt[18] = 0x00; // SYS CLASS

        g_neigh_count = 0;
        g_neigh[0] = g_neigh[1] = 0;
        long cc = 0, w = 0;
        int sid = 0;
        (void)cc;
        (void)w;
        (void)sid; // unused outputs here
        int sh = p25_test_decode_mbt_with_iden(mbt, (int)sizeof(mbt), iden, type, tdma, base5, spac125, &cc, &w, &sid);
        if (sh != 0) {
            return 20;
        }

        long want1 = 851000000 + 1 * 100 * 125; // 851.0125 MHz
        long want2 = 851000000 + 2 * 100 * 125; // 851.0250 MHz
        rc |= expect_eq_long("neigh count", g_neigh_count, 2);
        rc |= expect_eq_long("neigh f1", g_neigh[0], want1);
        rc |= expect_eq_long("neigh f2", g_neigh[1], want2);
    }

    // Case B: Adjacent Status Broadcast (0x3C)
    {
        uint8_t mbt[48];
        memset(mbt, 0, sizeof(mbt));
        mbt[0] = 0x17;  // ALT format
        mbt[2] = 0x00;  // MFID standard
        mbt[3] = 0x02;  // LRA
        mbt[4] = 0x21;  // CFVA=2 <<4, SYSID hi-nibble=1
        mbt[5] = 0x23;  // SYSID lo
        mbt[6] = 0x02;  // blks
        mbt[7] = 0x3C;  // opcode
        mbt[8] = 0x04;  // RFSS
        mbt[9] = 0x05;  // SITE
        mbt[12] = 0x10; // CHAN-T hi
        mbt[13] = 0x0A; // CHAN-T lo (0x100A)
        mbt[14] = 0x10; // CHAN-R hi
        mbt[15] = 0x05; // CHAN-R lo (0x1005)
        mbt[16] = 0x00; // SSC
        // WACN fields at [17..19] ignored here

        g_neigh_count = 0;
        g_neigh[0] = g_neigh[1] = 0;
        long cc = 0, w = 0;
        int sid = 0;
        (void)cc;
        (void)w;
        (void)sid;
        int sh = p25_test_decode_mbt_with_iden(mbt, (int)sizeof(mbt), iden, type, tdma, base5, spac125, &cc, &w, &sid);
        if (sh != 0) {
            return 30;
        }

        long want1 = 851000000 + 10 * 100 * 125; // 851.1250 MHz
        long want2 = 851000000 + 5 * 100 * 125;  // 851.0625 MHz
        rc |= expect_eq_long("adj count", g_neigh_count, 2);
        rc |= expect_eq_long("adj f1", g_neigh[0], want1);
        rc |= expect_eq_long("adj f2", g_neigh[1], want2);
    }

    return rc;
}
