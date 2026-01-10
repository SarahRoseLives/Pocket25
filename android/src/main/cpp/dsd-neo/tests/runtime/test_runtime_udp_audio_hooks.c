// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dsd-neo/runtime/udp_audio_hooks.h>

static int g_blast_calls = 0;
static int g_blast_analog_calls = 0;
static dsd_opts* g_last_opts = NULL;
static dsd_state* g_last_state = NULL;
static size_t g_last_nsam = 0;
static void* g_last_data = NULL;

static void
fake_blast(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    g_blast_calls++;
    g_last_opts = opts;
    g_last_state = state;
    g_last_nsam = nsam;
    g_last_data = data;
}

static void
fake_blast_analog(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    g_blast_analog_calls++;
    g_last_opts = opts;
    g_last_state = state;
    g_last_nsam = nsam;
    g_last_data = data;
}

int
main(void) {
    dsd_udp_audio_hooks_set((dsd_udp_audio_hooks){0});
    dsd_udp_audio_hook_blast(NULL, NULL, 0, NULL);
    dsd_udp_audio_hook_blast_analog(NULL, NULL, 0, NULL);

    dsd_opts* opts = (dsd_opts*)calloc(1, 1);
    dsd_state* state = (dsd_state*)calloc(1, 1);
    assert(opts != NULL);
    assert(state != NULL);
    unsigned char data[16] = {0};

    g_blast_calls = 0;
    g_blast_analog_calls = 0;
    g_last_opts = NULL;
    g_last_state = NULL;
    g_last_nsam = 0;
    g_last_data = NULL;

    dsd_udp_audio_hooks_set((dsd_udp_audio_hooks){
        .blast = fake_blast,
        .blast_analog = fake_blast_analog,
    });

    dsd_udp_audio_hook_blast(opts, state, 123u, data);
    assert(g_blast_calls == 1);
    assert(g_blast_analog_calls == 0);
    assert(g_last_opts == opts);
    assert(g_last_state == state);
    assert(g_last_nsam == 123u);
    assert(g_last_data == data);

    dsd_udp_audio_hook_blast_analog(opts, state, 456u, data);
    assert(g_blast_calls == 1);
    assert(g_blast_analog_calls == 1);
    assert(g_last_opts == opts);
    assert(g_last_state == state);
    assert(g_last_nsam == 456u);
    assert(g_last_data == data);

    free(state);
    free(opts);
    return 0;
}
