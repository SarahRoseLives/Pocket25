// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 regroup/patch tracking tests.
 * Covers: add/update, dedup, membership counts, summary/details formatting,
 * TTL sweep of stale entries, and clear/remove deactivation.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
expect_eq_str(const char* tag, const char* got, const char* want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "%s: got '%s' want '%s'\n", tag, got, want);
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
find_idx(const dsd_state* st, uint16_t sgid) {
    for (int i = 0; i < st->p25_patch_count && i < 8; i++) {
        if (st->p25_patch_sgid[i] == sgid) {
            return i;
        }
    }
    return -1;
}

int
main(void) {
    int rc = 0;
    static dsd_state st;
    memset(&st, 0, sizeof st);

    // Create a patch SG=069 with WG membership and crypt context
    p25_patch_update(&st, 69, /*is_patch*/ 1, /*active*/ 1);
    p25_patch_add_wgid(&st, 69, 0x0345);
    p25_patch_add_wgid(&st, 69, 0x0789);
    p25_patch_add_wgid(&st, 69, 0x0ABC);
    // Dedup
    p25_patch_add_wgid(&st, 69, 0x0345);
    // Crypt
    p25_patch_set_kas(&st, 69, 0x1234, 0x84, 17);

    // Create a simulselect SG=077 with 3 unit members
    p25_patch_update(&st, 77, /*is_patch*/ 0, /*active*/ 1);
    p25_patch_add_wuid(&st, 77, 1001);
    p25_patch_add_wuid(&st, 77, 1002);
    p25_patch_add_wuid(&st, 77, 1003);

    // Create another patch SG=142 with no membership
    p25_patch_update(&st, 142, /*is_patch*/ 1, /*active*/ 1);

    char sum[64];
    int sl = p25_patch_compose_summary(&st, sum, sizeof sum);
    rc |= expect_true("summary len>0", sl > 0);
    rc |= expect_eq_str("summary content", sum, "P: 069,142");

    char det[256];
    int dl = p25_patch_compose_details(&st, det, sizeof det);
    rc |= expect_true("details len>0", dl > 0);
    // SG069 shows WG list up to 3 and crypt context
    rc |= expect_true("details includes SG069[P]", strstr(det, "SG069[P]") != NULL);
    rc |= expect_true("details includes WG list", strstr(det, "WG:0837,1929,2748") != NULL);
    rc |= expect_true("details includes crypt", strstr(det, "K:1234 A:84 S:17") != NULL);
    // SG077 simulselect appears with U:3 (but not in summary)
    rc |= expect_true("details includes SG077[S]", strstr(det, "SG077[S]") != NULL);
    rc |= expect_true("details includes U:3", strstr(det, " U:3") != NULL);
    // SG142 shows as patch with no WG/U context
    rc |= expect_true("details includes SG142[P]", strstr(det, "SG142[P]") != NULL);

    // Add a 4th WGID to SG069 to trigger compact summary form WG:4(a,b+)
    p25_patch_add_wgid(&st, 69, 0x0DEF);
    (void)p25_patch_compose_details(&st, det, sizeof det);
    rc |= expect_true("details compact WG", strstr(det, "WG:4(0837,1929+") != NULL);

    // TTL sweep: mark SG142 stale, ensure it disappears from summary/details
    // (op25 uses 20s PATCH_EXPIRY_TIME)
    int idx142 = find_idx(&st, 142);
    if (idx142 >= 0) {
        st.p25_patch_last_update[idx142] = time(NULL) - 21; // >20s ago (op25 aligned)
    }
    (void)p25_patch_compose_summary(&st, sum, sizeof sum);
    rc |= expect_eq_str("summary after TTL", sum, "P: 069");
    (void)p25_patch_compose_details(&st, det, sizeof det);
    rc |= expect_true("details dropped SG142", strstr(det, "SG142[") == NULL);

    // Clear SG069; expect no summary and SG069 inactive
    p25_patch_clear_sg(&st, 69);
    sl = p25_patch_compose_summary(&st, sum, sizeof sum);
    rc |= expect_true("summary empty after clear", sl == 0 || sum[0] == '\0');

    // Removal makes entry inactive when last member removed
    // Re-add as patch and remove members one-by-one
    p25_patch_update(&st, 69, 1, 1);
    p25_patch_add_wgid(&st, 69, 0x1111);
    p25_patch_add_wgid(&st, 69, 0x2222);
    p25_patch_remove_wgid(&st, 69, 0x1111);
    p25_patch_remove_wgid(&st, 69, 0x2222);
    // Compose details should not include SG069 anymore (inactive)
    (void)p25_patch_compose_details(&st, det, sizeof det);
    rc |= expect_true("SG069 inactive after removals", strstr(det, "SG069[") == NULL);

    // SG077[S] still present (simulselect) with U:3
    rc |= expect_true("SG077 remains", strstr(det, "SG077[S]") != NULL);

    (void)errno;
    return rc;
}
