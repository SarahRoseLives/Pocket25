// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 Phase 1 trunking state machine core tests.
 *
 * Focus: CC candidate queueing, tune/release counters, TDMA slot set from channel,
 * and next-CC iteration behavior.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

#define setenv dsd_test_setenv

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/config.h>

// Stubs for rigctl/rtl to avoid external I/O
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

static int
expect_eq(const char* tag, long long got, long long want) {
    if (got != want) {
        fprintf(stderr, "%s: got %lld want %lld\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    int rc = 0;

    // Use a temp cache dir to avoid touching HOME
    char dir[DSD_TEST_PATH_MAX];
    if (!dsd_test_mkdtemp(dir, sizeof(dir), "dsdneo_cc_cache")) {
        fprintf(stderr, "dsd_test_mkdtemp failed\n");
        return 100;
    }
    setenv("DSD_NEO_CACHE_DIR", dir, 1);
    dsd_neo_config_init(NULL);

    static dsd_opts opts;
    static dsd_state state;
    memset(&opts, 0, sizeof opts);
    memset(&state, 0, sizeof state);

    // Seed system identity so cache helpers are active but point to temp dir
    state.p2_wacn = 0xABCDE;
    state.p2_sysid = 0x123;
    opts.verbose = 0;

    // Initialize SM and verify counters
    p25_sm_init(&opts, &state);
    rc |= expect_eq("init tune_count", state.p25_sm_tune_count, 0);
    rc |= expect_eq("init release_count", state.p25_sm_release_count, 0);

    // Neighbor update supplies two CC candidates
    long neigh[3] = {851012500, 851537500, 0};
    p25_sm_on_neighbor_update(&opts, &state, neigh, 2);

    // Iterate candidates (order preserved)
    long cand = 0;
    int ok1 = p25_sm_next_cc_candidate(&state, &cand);
    rc |= expect_eq("cand ok1", ok1, 1);
    rc |= expect_eq("cand1", cand, neigh[0]);
    ok1 = p25_sm_next_cc_candidate(&state, &cand);
    rc |= expect_eq("cand ok2", ok1, 1);
    rc |= expect_eq("cand2", cand, neigh[1]);
    ok1 = p25_sm_next_cc_candidate(&state, &cand);
    rc |= expect_eq("cand cycle ok3", ok1, 1);
    rc |= expect_eq("cand3", cand, neigh[0]);

    // Simulate a group grant: enable trunking and a non-zero CC freq
    opts.p25_trunk = 1;
    opts.trunk_tune_group_calls = 1;
    state.p25_cc_freq = 851012500;

    // Mark IDEN 1 as TDMA to exercise slot detection; choose odd channel number => slot 1
    int iden = 1;
    state.p25_chan_tdma[iden] = 1;
    state.p25_chan_type[iden] = 1;         // FDMA/TDMA mapping type (not critical for this test)
    state.p25_chan_spac[iden] = 1250;      // 12.5 kHz
    state.p25_base_freq[iden] = 851000000; // 851.000 MHz
    int channel = (iden << 12) | 0x0001;   // low bit = 1 → slot 1
    int svc = 0;                           // service bits not used here
    int tg = 1234;
    int src = 5678;
    p25_sm_on_group_grant(&opts, &state, channel, svc, tg, src);

    // Expect one tune and active slot set to 1 for TDMA
    rc |= expect_eq("tune_count after grant", state.p25_sm_tune_count, 1);
    rc |= expect_eq("active slot", state.p25_p2_active_slot, 1);

    // Release path: ensure it increments release count. Force no active slots to avoid deferral.
    state.p25_p2_audio_allowed[0] = 0;
    state.p25_p2_audio_allowed[1] = 0;
    state.dmrburstL = 24;
    state.dmrburstR = 24;
    p25_sm_on_release(&opts, &state);
    rc |= expect_eq("release_count", state.p25_sm_release_count, 1);

    return rc;
}
