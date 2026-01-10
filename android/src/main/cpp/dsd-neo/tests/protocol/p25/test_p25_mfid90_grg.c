// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 MFID90 Group Regroup (GRG) handler tests.
 * Tests the patch state management that backs MFID90 GRG Add/Delete commands.
 * The actual TSBK/MAC parsing is covered by integration, but this verifies
 * the underlying patch API contracts match what the handlers expect.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>

// Stubs for external hooks referenced in the linked library
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
expect_eq(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
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

static int
find_sg_idx(const dsd_state* st, uint16_t sgid) {
    for (int i = 0; i < st->p25_patch_count && i < 8; i++) {
        if (st->p25_patch_sgid[i] == sgid) {
            return i;
        }
    }
    return -1;
}

static int
sg_has_wgid(const dsd_state* st, int idx, uint16_t wgid) {
    if (idx < 0 || idx >= 8) {
        return 0;
    }
    for (int i = 0; i < st->p25_patch_wgid_count[idx] && i < 8; i++) {
        if (st->p25_patch_wgid[idx][i] == wgid) {
            return 1;
        }
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    static dsd_state st;
    memset(&st, 0, sizeof st);

    // Test 1: MFID90 GRG Add Command pattern (sg=100, ga1=200, ga2=300, ga3=400)
    // Simulates the field extraction from p25p1_tsbk.c opcode 0x00 handler
    {
        int sg = 100;
        int ga1 = 200, ga2 = 300, ga3 = 400;

        // Add non-zero group addresses (matching handler logic)
        if (ga1 != 0) {
            p25_patch_add_wgid(&st, sg, ga1);
        }
        if (ga2 != 0) {
            p25_patch_add_wgid(&st, sg, ga2);
        }
        if (ga3 != 0) {
            p25_patch_add_wgid(&st, sg, ga3);
        }
        p25_patch_update(&st, sg, /*is_patch*/ 1, /*active*/ 1);

        int idx = find_sg_idx(&st, (uint16_t)sg);
        rc |= expect_true("GRG Add: SG 100 exists", idx >= 0);
        if (idx >= 0) {
            rc |= expect_eq("GRG Add: wgid count", st.p25_patch_wgid_count[idx], 3);
            rc |= expect_true("GRG Add: has GA1", sg_has_wgid(&st, idx, 200));
            rc |= expect_true("GRG Add: has GA2", sg_has_wgid(&st, idx, 300));
            rc |= expect_true("GRG Add: has GA3", sg_has_wgid(&st, idx, 400));
            rc |= expect_eq("GRG Add: is_patch", st.p25_patch_is_patch[idx], 1);
            rc |= expect_eq("GRG Add: active", st.p25_patch_active[idx], 1);
        }
    }

    // Test 2: MFID90 GRG Delete Command pattern (sg=100, remove ga2=300)
    // Simulates the field extraction from p25p1_tsbk.c opcode 0x01 handler
    {
        int sg = 100;
        int ga1 = 0, ga2 = 300, ga3 = 0; // Only ga2 non-zero

        if (ga1 != 0) {
            p25_patch_remove_wgid(&st, sg, ga1);
        }
        if (ga2 != 0) {
            p25_patch_remove_wgid(&st, sg, ga2);
        }
        if (ga3 != 0) {
            p25_patch_remove_wgid(&st, sg, ga3);
        }

        int idx = find_sg_idx(&st, (uint16_t)sg);
        rc |= expect_true("GRG Del: SG 100 still exists", idx >= 0);
        if (idx >= 0) {
            rc |= expect_eq("GRG Del: wgid count after removal", st.p25_patch_wgid_count[idx], 2);
            rc |= expect_true("GRG Del: still has GA1", sg_has_wgid(&st, idx, 200));
            rc |= expect_true("GRG Del: GA2 removed", !sg_has_wgid(&st, idx, 300));
            rc |= expect_true("GRG Del: still has GA3", sg_has_wgid(&st, idx, 400));
        }
    }

    // Test 3: P2 MAC GRG Add with variable workgroup list
    // Simulates parsing wg_len and iterating workgroups
    {
        int sg = 200;
        int wg_list[] = {1001, 1002, 1003, 1004};
        int num_wg = 4;

        for (int i = 0; i < num_wg; i++) {
            if (wg_list[i] != 0) {
                p25_patch_add_wgid(&st, sg, wg_list[i]);
            }
        }
        p25_patch_update(&st, sg, /*is_patch*/ 1, /*active*/ 1);

        int idx = find_sg_idx(&st, (uint16_t)sg);
        rc |= expect_true("P2 MAC Add: SG 200 exists", idx >= 0);
        if (idx >= 0) {
            rc |= expect_eq("P2 MAC Add: wgid count", st.p25_patch_wgid_count[idx], 4);
        }
    }

    // Test 4: P2 MAC GRG Delete removes multiple workgroups
    {
        int sg = 200;
        int del_list[] = {1001, 1003};
        int num_del = 2;

        for (int i = 0; i < num_del; i++) {
            if (del_list[i] != 0) {
                p25_patch_remove_wgid(&st, sg, del_list[i]);
            }
        }

        int idx = find_sg_idx(&st, (uint16_t)sg);
        rc |= expect_true("P2 MAC Del: SG 200 exists", idx >= 0);
        if (idx >= 0) {
            rc |= expect_eq("P2 MAC Del: wgid count after", st.p25_patch_wgid_count[idx], 2);
            rc |= expect_true("P2 MAC Del: 1001 removed", !sg_has_wgid(&st, idx, 1001));
            rc |= expect_true("P2 MAC Del: 1002 remains", sg_has_wgid(&st, idx, 1002));
            rc |= expect_true("P2 MAC Del: 1003 removed", !sg_has_wgid(&st, idx, 1003));
            rc |= expect_true("P2 MAC Del: 1004 remains", sg_has_wgid(&st, idx, 1004));
        }
    }

    // Test 5: Deduplication - adding same WGID twice should not increase count
    {
        static dsd_state st2;
        memset(&st2, 0, sizeof st2);

        p25_patch_add_wgid(&st2, 300, 500);
        p25_patch_add_wgid(&st2, 300, 500); // duplicate
        p25_patch_add_wgid(&st2, 300, 501);
        p25_patch_update(&st2, 300, 1, 1);

        int idx = find_sg_idx(&st2, 300);
        rc |= expect_true("Dedup: SG 300 exists", idx >= 0);
        if (idx >= 0) {
            rc |= expect_eq("Dedup: wgid count", st2.p25_patch_wgid_count[idx], 2);
        }
    }

    // Test 6: Clear SG removes all membership
    {
        static dsd_state st3;
        memset(&st3, 0, sizeof st3);

        p25_patch_add_wgid(&st3, 400, 600);
        p25_patch_add_wgid(&st3, 400, 601);
        p25_patch_update(&st3, 400, 1, 1);

        int idx = find_sg_idx(&st3, 400);
        rc |= expect_true("Clear pre: SG 400 exists", idx >= 0);

        p25_patch_clear_sg(&st3, 400);

        // After clear, the SG should be inactive
        idx = find_sg_idx(&st3, 400);
        if (idx >= 0) {
            rc |= expect_eq("Clear: SG inactive", st3.p25_patch_active[idx], 0);
        }
    }

    (void)errno;
    return rc;
}
