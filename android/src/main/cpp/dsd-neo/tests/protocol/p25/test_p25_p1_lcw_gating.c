// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 P1 LCW gating tests: verify Packet (0x10) and Encrypted (0x40)
 * service options block tuning via the trunk SM when tuning policies are
 * disabled.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>

// Strong stub to capture VC tuning attempts from the SM path
static int g_tunes = 0;

void
trunk_tune_to_freq(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    (void)opts;
    (void)state;
    (void)freq;
    (void)ted_sps;
    g_tunes++;
}

// No-op stubs to satisfy link of LCW path helpers
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
struct RtlSdrContext* g_rtl_ctx = 0;

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

static void
install_trunk_tuning_hooks(void) {
    dsd_trunk_tuning_hooks hooks = {0};
    hooks.tune_to_freq = trunk_tune_to_freq;
    hooks.return_to_cc = return_to_cc;
    dsd_trunk_tuning_hooks_set(hooks);
}

// Minimal ConvertBitIntoBytes (MSB-first) and no-op alias/GPS helpers for LCW
uint64_t
ConvertBitIntoBytes(uint8_t* BufferIn, uint32_t BitLength) {
    uint64_t out = 0;
    for (uint32_t i = 0; i < BitLength; i++) {
        out = (out << 1) | (uint64_t)(BufferIn[i] & 1);
    }
    return out;
}

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

static void
set_bits_msb(uint8_t* bits, int start, int width, unsigned value) {
    for (int i = 0; i < width; i++) {
        int bit = (value >> (width - 1 - i)) & 1;
        bits[start + i] = (uint8_t)bit;
    }
}

static int
expect_eq_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

void p25_lcw(dsd_opts* opts, dsd_state* state, uint8_t LCW_bits[], uint8_t irrecoverable_errors);

int
main(void) {
    int rc = 0;
    static dsd_opts opts;
    static dsd_state st;
    install_trunk_tuning_hooks();
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);
    opts.p25_trunk = 1;
    opts.p25_lcw_retune = 1;
    opts.trunk_tune_group_calls = 1;
    opts.trunk_tune_enc_calls = 0;
    st.p25_cc_freq = 851000000;
    // Seed IDEN 1 (FDMA): base in 5 kHz units, spacing 100 (5 kHz → 500 kHz)
    st.p25_chan_tdma[1] = 0;
    st.p25_base_freq[1] = 851000000 / 5;
    st.p25_chan_spac[1] = 100;
    st.p25_iden_trust[1] = 2;

    // Base LCW for 0x44 (Group Voice Channel Update – Explicit)
    uint8_t lcw[72];
    memset(lcw, 0, sizeof lcw);
    const int svc = 0x00;
    const int tg = 0x1234;
    const int ch = 0x100A;
    set_bits_msb(lcw, 0, 8, 0x44);
    set_bits_msb(lcw, 8, 8, 0x00);
    set_bits_msb(lcw, 16, 8, (unsigned)svc);
    set_bits_msb(lcw, 24, 16, (unsigned)tg);
    set_bits_msb(lcw, 40, 16, (unsigned)ch);

    // Control case: clear SVC should tune once
    g_tunes = 0;
    p25_lcw(&opts, &st, lcw, 0);
    rc |= expect_eq_int("clear->tune", g_tunes, 1);

    // Packet bit set: tuning disabled by default policy (trunk_tune_data_calls=0)
    set_bits_msb(lcw, 16, 8, (unsigned)(svc | 0x10));
    g_tunes = 0;
    p25_lcw(&opts, &st, lcw, 0);
    rc |= expect_eq_int("packet->no-tune", g_tunes, 0);

    // Encrypted bit set: tuning disabled by default (trunk_tune_enc_calls=0)
    set_bits_msb(lcw, 16, 8, (unsigned)(svc | 0x40));
    g_tunes = 0;
    p25_lcw(&opts, &st, lcw, 0);
    rc |= expect_eq_int("enc->no-tune", g_tunes, 0);

    return rc;
}
