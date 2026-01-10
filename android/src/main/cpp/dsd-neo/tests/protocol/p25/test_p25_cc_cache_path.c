// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Control-channel cache path formatting tests.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_cc_candidates.h>
#include <dsd-neo/runtime/config.h>

#include "test_support.h"

#define setenv dsd_test_setenv

static int
expect_eq_str(const char* tag, const char* a, const char* b) {
    if (strcmp(a ? a : "", b ? b : "") != 0) {
        fprintf(stderr, "%s: got '%s' want '%s'\n", tag, a ? a : "(null)", b ? b : "(null)");
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
    char dir[DSD_TEST_PATH_MAX];
    if (!dsd_test_mkdtemp(dir, sizeof(dir), "dsdneo_cc_path")) {
        fprintf(stderr, "dsd_test_mkdtemp failed: %s\n", strerror(errno));
        return 100;
    }
    setenv("DSD_NEO_CACHE_DIR", dir, 1);
    dsd_neo_config_init(NULL);

    static dsd_state st;
    memset(&st, 0, sizeof st);

    // No identity -> no path
    char out[1024];
    int ok = p25_cc_build_cache_path(&st, out, sizeof out);
    rc |= expect_eq_int("no identity", ok, 0);

    // With WACN/SYSID only
    st.p2_wacn = 0xABCDEULL;
    st.p2_sysid = 0x123ULL;
    ok = p25_cc_build_cache_path(&st, out, sizeof out);
    rc |= expect_eq_int("iden only ok", ok, 1);
    char want1[1024];
    snprintf(want1, sizeof want1, "%s/p25_cc_%05lX_%03lX.txt", dir, (unsigned long)st.p2_wacn,
             (unsigned long)st.p2_sysid);
    rc |= expect_eq_str("iden only path", out, want1);

    // With RFSS/SITE
    st.p2_rfssid = 7ULL;
    st.p2_siteid = 11ULL;
    ok = p25_cc_build_cache_path(&st, out, sizeof out);
    rc |= expect_eq_int("rfss/site ok", ok, 1);
    char want2[1024];
    snprintf(want2, sizeof want2, "%s/p25_cc_%05lX_%03lX_R%03llu_S%03llu.txt", dir, (unsigned long)st.p2_wacn,
             (unsigned long)st.p2_sysid, st.p2_rfssid, st.p2_siteid);
    rc |= expect_eq_str("rfss/site path", out, want2);

    return rc;
}
