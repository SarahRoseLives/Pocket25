// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/protocol/p25/p25_trunk_sm_api.h>

#include <stdio.h>
#include <string.h>

typedef struct dsd_opts dsd_opts;
typedef struct dsd_state dsd_state;

void p25_sm_init(dsd_opts* opts, dsd_state* state);
void p25_sm_on_group_grant(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int tg, int src);
void p25_sm_on_indiv_grant(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int dst, int src);
void p25_sm_on_release(dsd_opts* opts, dsd_state* state);
void p25_sm_on_neighbor_update(dsd_opts* opts, dsd_state* state, const long* freqs, int count);
int p25_sm_next_cc_candidate(dsd_state* state, long* out_freq);
void p25_sm_tick(dsd_opts* opts, dsd_state* state);

static int g_init_calls;
static int g_group_calls;
static int g_indiv_calls;
static int g_release_calls;
static int g_neighbor_calls;
static int g_next_calls;
static int g_tick_calls;

static dsd_opts* g_last_opts;
static dsd_state* g_last_state;

static int g_last_group_channel;
static int g_last_group_svc_bits;
static int g_last_group_tg;
static int g_last_group_src;

static int g_last_indiv_channel;
static int g_last_indiv_svc_bits;
static int g_last_indiv_dst;
static int g_last_indiv_src;

static const long* g_last_neighbor_freqs;
static int g_last_neighbor_count;

static long* g_last_out_freq;

static void
fake_init(dsd_opts* opts, dsd_state* state) {
    g_init_calls++;
    g_last_opts = opts;
    g_last_state = state;
}

static void
fake_on_group_grant(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int tg, int src) {
    g_group_calls++;
    g_last_opts = opts;
    g_last_state = state;
    g_last_group_channel = channel;
    g_last_group_svc_bits = svc_bits;
    g_last_group_tg = tg;
    g_last_group_src = src;
}

static void
fake_on_indiv_grant(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int dst, int src) {
    g_indiv_calls++;
    g_last_opts = opts;
    g_last_state = state;
    g_last_indiv_channel = channel;
    g_last_indiv_svc_bits = svc_bits;
    g_last_indiv_dst = dst;
    g_last_indiv_src = src;
}

static void
fake_on_release(dsd_opts* opts, dsd_state* state) {
    g_release_calls++;
    g_last_opts = opts;
    g_last_state = state;
}

static void
fake_on_neighbor_update(dsd_opts* opts, dsd_state* state, const long* freqs, int count) {
    g_neighbor_calls++;
    g_last_opts = opts;
    g_last_state = state;
    g_last_neighbor_freqs = freqs;
    g_last_neighbor_count = count;
}

static int
fake_next_cc_candidate(dsd_state* state, long* out_freq) {
    g_next_calls++;
    g_last_state = state;
    g_last_out_freq = out_freq;
    if (out_freq) {
        *out_freq = 424242;
    }
    return 1;
}

static void
fake_tick(dsd_opts* opts, dsd_state* state) {
    g_tick_calls++;
    g_last_opts = opts;
    g_last_state = state;
}

static int
expect_eq_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
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
expect_eq_ptr(const char* tag, const void* got, const void* want) {
    if (got != want) {
        fprintf(stderr, "%s: got %p want %p\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;

    unsigned char opts_blob[32];
    unsigned char state_blob[32];
    memset(opts_blob, 0xA5, sizeof(opts_blob));
    memset(state_blob, 0x5A, sizeof(state_blob));

    dsd_opts* opts = (dsd_opts*)(void*)opts_blob;
    dsd_state* state = (dsd_state*)(void*)state_blob;

    long freqs[] = {851000000L, 852000000L, 853000000L};
    long out_freq = 0;

    p25_sm_reset_api();

    p25_sm_api api = {0};
    api.init = fake_init;
    api.on_group_grant = fake_on_group_grant;
    api.on_indiv_grant = fake_on_indiv_grant;
    api.on_release = fake_on_release;
    api.on_neighbor_update = fake_on_neighbor_update;
    api.next_cc_candidate = fake_next_cc_candidate;
    api.tick = fake_tick;
    p25_sm_set_api(api);

    p25_sm_init(opts, state);
    rc |= expect_eq_int("init_calls", g_init_calls, 1);
    rc |= expect_eq_ptr("init_opts", g_last_opts, opts);
    rc |= expect_eq_ptr("init_state", g_last_state, state);

    p25_sm_on_group_grant(opts, state, 7, 0x12, 100, 200);
    rc |= expect_eq_int("group_calls", g_group_calls, 1);
    rc |= expect_eq_int("group_channel", g_last_group_channel, 7);
    rc |= expect_eq_int("group_svc_bits", g_last_group_svc_bits, 0x12);
    rc |= expect_eq_int("group_tg", g_last_group_tg, 100);
    rc |= expect_eq_int("group_src", g_last_group_src, 200);

    p25_sm_on_indiv_grant(opts, state, 8, 0x34, 300, 400);
    rc |= expect_eq_int("indiv_calls", g_indiv_calls, 1);
    rc |= expect_eq_int("indiv_channel", g_last_indiv_channel, 8);
    rc |= expect_eq_int("indiv_svc_bits", g_last_indiv_svc_bits, 0x34);
    rc |= expect_eq_int("indiv_dst", g_last_indiv_dst, 300);
    rc |= expect_eq_int("indiv_src", g_last_indiv_src, 400);

    p25_sm_on_release(opts, state);
    rc |= expect_eq_int("release_calls", g_release_calls, 1);

    p25_sm_on_neighbor_update(opts, state, freqs, (int)(sizeof(freqs) / sizeof(freqs[0])));
    rc |= expect_eq_int("neighbor_calls", g_neighbor_calls, 1);
    rc |= expect_eq_ptr("neighbor_freqs", g_last_neighbor_freqs, freqs);
    rc |= expect_eq_int("neighbor_count", g_last_neighbor_count, (int)(sizeof(freqs) / sizeof(freqs[0])));

    int ok = p25_sm_next_cc_candidate(state, &out_freq);
    rc |= expect_eq_int("next_ok", ok, 1);
    rc |= expect_eq_int("next_calls", g_next_calls, 1);
    rc |= expect_eq_ptr("next_state", g_last_state, state);
    rc |= expect_eq_ptr("next_out_ptr", g_last_out_freq, &out_freq);
    rc |= expect_eq_long("next_out_freq", out_freq, 424242);

    p25_sm_tick(opts, state);
    rc |= expect_eq_int("tick_calls", g_tick_calls, 1);

    return rc;
}
