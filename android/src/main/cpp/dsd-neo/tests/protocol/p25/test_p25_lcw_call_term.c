// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 LCW 0x4F (Call Termination) unit test.
 * Feeds a minimal LCW bit array to p25_lcw() and verifies that
 * p25_sm_on_release -> return_to_cc is invoked when tuned.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>

void p25_lcw(dsd_opts* opts, dsd_state* state, uint8_t LCW_bits[], uint8_t irrecoverable_errors);

// Strong stubs
static int g_return_to_cc_called = 0;

bool
SetFreq(int sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return true;
}

bool
SetModulation(int sockfd, int bandwidth) {
    (void)sockfd;
    (void)bandwidth;
    return true;
}
struct RtlSdrContext* g_rtl_ctx = 0;

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    g_return_to_cc_called++;
    if (opts) {
        opts->p25_is_tuned = 0;
        opts->trunk_is_tuned = 0;
    }
    if (state) {
        state->p25_vc_freq[0] = state->p25_vc_freq[1] = 0;
    }
}

static void
install_trunk_tuning_hooks(void) {
    dsd_trunk_tuning_hooks hooks = {0};
    hooks.return_to_cc = return_to_cc;
    dsd_trunk_tuning_hooks_set(hooks);
}

// LCW path external helpers we don't exercise here (provide no-op stubs)
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

// Minimal ConvertBitIntoBytes (MSB-first) used by LCW
uint64_t
ConvertBitIntoBytes(uint8_t* BufferIn, uint32_t BitLength) {
    uint64_t out = 0;
    for (uint32_t i = 0; i < BitLength; i++) {
        out = (out << 1) | (uint64_t)(BufferIn[i] & 1);
    }
    return out;
}

static void
set_bits_msb(uint8_t* b, int off, int n, uint32_t v) {
    for (int i = 0; i < n; i++) {
        int bit = (v >> (n - 1 - i)) & 1;
        b[off + i] = (uint8_t)bit;
    }
}

static int
expect_true(const char* tag, int cond) {
    if (!cond) {
        fprintf(stderr, "%s: failed\n", tag);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    static dsd_opts opts;
    static dsd_state st;
    install_trunk_tuning_hooks();
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);

    // Minimal conditions for release on LCW 0x4F
    opts.p25_trunk = 1;
    opts.p25_is_tuned = 1;
    st.p25_cc_freq = 851000000;

    // Prepare LCW bits: format 0x4F at bits [0..7], MFID=0 at [8..15]
    uint8_t lcw[96];
    memset(lcw, 0, sizeof lcw);
    set_bits_msb(lcw, 0, 8, 0x4F);  // lc_format
    set_bits_msb(lcw, 8, 8, 0x00);  // lc_mfid
    set_bits_msb(lcw, 16, 8, 0x00); // lc_svcopt
    // Target field present at [48..71]; any value OK
    set_bits_msb(lcw, 48, 24, 0x00FFEE);

    g_return_to_cc_called = 0;
    p25_lcw(&opts, &st, lcw, /*irrecoverable_errors*/ 0);
    rc |= expect_true("LCW_0x4F_release", g_return_to_cc_called >= 1 && opts.p25_is_tuned == 0);

    // Regression: some systems populate the MFID/reserved octet even when the LCW is standard
    // (e.g., implicit MFID formats). Ensure 0x4F still releases even when byte 1 is non-zero.
    opts.p25_is_tuned = 1;
    set_bits_msb(lcw, 8, 8, 0x90); // non-standard value in octet 1
    g_return_to_cc_called = 0;
    p25_lcw(&opts, &st, lcw, /*irrecoverable_errors*/ 0);
    rc |= expect_true("LCW_0x4F_release_nonzero_mfid_octet", g_return_to_cc_called >= 1 && opts.p25_is_tuned == 0);

    // Motorola systems may emit MFID90 Talker EOT (format 0x0F, MFID 0x90) in place of standard call termination.
    // Ensure it also triggers an explicit release when trunk-following.
    opts.p25_is_tuned = 1;
    set_bits_msb(lcw, 0, 8, 0x0F);       // lc_format (PB=0,SF=0,LCO=0x0F)
    set_bits_msb(lcw, 8, 8, 0x90);       // lc_mfid (Motorola)
    set_bits_msb(lcw, 16, 8, 0x00);      // lc_svcopt
    set_bits_msb(lcw, 48, 24, 0x000123); // SRC (for logging)
    g_return_to_cc_called = 0;
    p25_lcw(&opts, &st, lcw, /*irrecoverable_errors*/ 0);
    rc |= expect_true("LCW_MFID90_TalkerEOT_release", g_return_to_cc_called >= 1 && opts.p25_is_tuned == 0);

    return rc;
}
