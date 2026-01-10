// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 IDEN trust promotion tests.
 * Verifies that p25_confirm_idens_for_current_site promotes trust to 2 only
 * when provenance (WACN/SYSID and, if present, RFSS/SITE) matches current site.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>

static int
expect_eq_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

// Stubs for unrelated external hooks
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

void
unpack_byte_array_into_bit_array(uint8_t* input, uint8_t* output, int len) {
    (void)input;
    (void)output;
    (void)len;
}

void
apx_embedded_alias_header_phase2(dsd_opts* o, dsd_state* s, uint8_t slot, uint8_t* b) {
    (void)o;
    (void)s;
    (void)slot;
    (void)b;
}

void
apx_embedded_alias_blocks_phase2(dsd_opts* o, dsd_state* s, uint8_t slot, uint8_t* b) {
    (void)o;
    (void)s;
    (void)slot;
    (void)b;
}

void
l3h_embedded_alias_decode(dsd_opts* o, dsd_state* s, uint8_t slot, int16_t len, uint8_t* in) {
    (void)o;
    (void)s;
    (void)slot;
    (void)len;
    (void)in;
}

void
nmea_harris(dsd_opts* o, dsd_state* s, uint8_t* in, uint32_t src, int slot) {
    (void)o;
    (void)s;
    (void)in;
    (void)src;
    (void)slot;
}

int
main(void) {
    int rc = 0;
    static dsd_state st;
    memset(&st, 0, sizeof st);

    // Current site identity
    st.p2_wacn = 0xABCDE;
    st.p2_sysid = 0x123;
    st.p2_rfssid = 4;
    st.p2_siteid = 7;

    // Case A: WACN/SYSID match; RFSS/SITE unset → promote to 2
    int idA = 1;
    st.p25_iden_wacn[idA] = st.p2_wacn;
    st.p25_iden_sysid[idA] = st.p2_sysid;
    st.p25_iden_rfss[idA] = 0;
    st.p25_iden_site[idA] = 0;
    st.p25_iden_trust[idA] = 1; // seen but unconfirmed

    // Case B: all match → promote to 2
    int idB = 2;
    st.p25_iden_wacn[idB] = st.p2_wacn;
    st.p25_iden_sysid[idB] = st.p2_sysid;
    st.p25_iden_rfss[idB] = st.p2_rfssid;
    st.p25_iden_site[idB] = st.p2_siteid;
    st.p25_iden_trust[idB] = 1;

    // Case C: RFSS mismatch → remain <2
    int idC = 3;
    st.p25_iden_wacn[idC] = st.p2_wacn;
    st.p25_iden_sysid[idC] = st.p2_sysid;
    st.p25_iden_rfss[idC] = st.p2_rfssid + 1;
    st.p25_iden_site[idC] = st.p2_siteid;
    st.p25_iden_trust[idC] = 1;

    // Case D: SITE mismatch → remain <2
    int idD = 4;
    st.p25_iden_wacn[idD] = st.p2_wacn;
    st.p25_iden_sysid[idD] = st.p2_sysid;
    st.p25_iden_rfss[idD] = st.p2_rfssid;
    st.p25_iden_site[idD] = st.p2_siteid + 1;
    st.p25_iden_trust[idD] = 1;

    p25_confirm_idens_for_current_site(&st);

    rc |= expect_eq_int("trust A", st.p25_iden_trust[idA], 2);
    rc |= expect_eq_int("trust B", st.p25_iden_trust[idB], 2);
    rc |= expect_eq_int("trust C", st.p25_iden_trust[idC] == 2, 0);
    rc |= expect_eq_int("trust D", st.p25_iden_trust[idD] == 2, 0);

    return rc;
}
