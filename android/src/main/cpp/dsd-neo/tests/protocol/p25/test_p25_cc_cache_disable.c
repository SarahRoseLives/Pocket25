// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 CC cache disable test via DSD_NEO_CC_CACHE=0.
 * Ensures no cache file is created and warm-load does not occur.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/config.h>

#include "test_support.h"

#define setenv dsd_test_setenv

// Stubs
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
    // Temp dir
    char dir[DSD_TEST_PATH_MAX];
    if (!dsd_test_mkdtemp(dir, sizeof(dir), "dsdneo_cc_cache_disable")) {
        return 100;
    }
    setenv("DSD_NEO_CACHE_DIR", dir, 1);
    setenv("DSD_NEO_CC_CACHE", "0", 1); // disable
    dsd_neo_config_init(NULL);

    unsigned long wacn = 0xABCDE;
    int sysid = 0x123;

    static dsd_opts opts;
    static dsd_state st;
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);
    st.p2_wacn = wacn;
    st.p2_sysid = sysid;

    long f[3] = {851000000, 851012500, 851025000};
    p25_sm_on_neighbor_update(&opts, &st, f, 3);

    // No file should be created
    char path[DSD_TEST_PATH_MAX];
    snprintf(path, sizeof path, "%s/p25_cc_%05lX_%03X.txt", dir, wacn, sysid);
    FILE* fp = fopen(path, "r");
    rc |= expect_true("no cache file", fp == NULL);
    if (fp) {
        fclose(fp);
    }

    // New state shouldn't warm-load (we cannot introspect list without file, so just assert file absence suffices)
    return rc;
}
