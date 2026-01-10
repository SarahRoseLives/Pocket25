// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dsd-neo/runtime/net_audio_input_hooks.h>

static int g_tcp_open_calls = 0;
static int g_tcp_close_calls = 0;
static int g_tcp_read_calls = 0;
static int g_tcp_is_valid_calls = 0;
static int g_tcp_get_socket_calls = 0;
static int g_udp_start_calls = 0;
static int g_udp_stop_calls = 0;
static int g_udp_read_calls = 0;

static dsd_socket_t g_last_sockfd = DSD_INVALID_SOCKET;
static int g_last_samplerate = 0;
static tcp_input_ctx* g_last_tcp_ctx = NULL;
static dsd_opts* g_last_udp_opts = NULL;
static const char* g_last_bindaddr = NULL;
static int g_last_port = 0;
static int16_t* g_last_out = NULL;

static tcp_input_ctx*
fake_tcp_open(dsd_socket_t sockfd, int samplerate) {
    g_tcp_open_calls++;
    g_last_sockfd = sockfd;
    g_last_samplerate = samplerate;
    static int s_dummy = 0;
    return (tcp_input_ctx*)&s_dummy;
}

static void
fake_tcp_close(tcp_input_ctx* ctx) {
    g_tcp_close_calls++;
    g_last_tcp_ctx = ctx;
}

static int
fake_tcp_read_sample(tcp_input_ctx* ctx, int16_t* out) {
    g_tcp_read_calls++;
    g_last_tcp_ctx = ctx;
    g_last_out = out;
    if (out) {
        *out = (int16_t)123;
    }
    return 1;
}

static int
fake_tcp_is_valid(tcp_input_ctx* ctx) {
    g_tcp_is_valid_calls++;
    g_last_tcp_ctx = ctx;
    return 1;
}

static dsd_socket_t
fake_tcp_get_socket(tcp_input_ctx* ctx) {
    g_tcp_get_socket_calls++;
    g_last_tcp_ctx = ctx;
    return (dsd_socket_t)42;
}

static int
fake_udp_start(dsd_opts* opts, const char* bindaddr, int port, int samplerate) {
    g_udp_start_calls++;
    g_last_udp_opts = opts;
    g_last_bindaddr = bindaddr;
    g_last_port = port;
    g_last_samplerate = samplerate;
    return 0;
}

static void
fake_udp_stop(dsd_opts* opts) {
    g_udp_stop_calls++;
    g_last_udp_opts = opts;
}

static int
fake_udp_read_sample(dsd_opts* opts, int16_t* out) {
    g_udp_read_calls++;
    g_last_udp_opts = opts;
    g_last_out = out;
    if (out) {
        *out = (int16_t)-7;
    }
    return 1;
}

static void
reset_fakes(void) {
    g_tcp_open_calls = 0;
    g_tcp_close_calls = 0;
    g_tcp_read_calls = 0;
    g_tcp_is_valid_calls = 0;
    g_tcp_get_socket_calls = 0;
    g_udp_start_calls = 0;
    g_udp_stop_calls = 0;
    g_udp_read_calls = 0;

    g_last_sockfd = DSD_INVALID_SOCKET;
    g_last_samplerate = 0;
    g_last_tcp_ctx = NULL;
    g_last_udp_opts = NULL;
    g_last_bindaddr = NULL;
    g_last_port = 0;
    g_last_out = NULL;
}

int
main(void) {
    dsd_net_audio_input_hooks_set((dsd_net_audio_input_hooks){0});
    assert(dsd_net_audio_input_hook_tcp_open((dsd_socket_t)1, 48000) == NULL);
    dsd_net_audio_input_hook_tcp_close(NULL);
    assert(dsd_net_audio_input_hook_tcp_read_sample(NULL, NULL) == 0);
    assert(dsd_net_audio_input_hook_tcp_is_valid(NULL) == 0);
    assert(dsd_net_audio_input_hook_tcp_get_socket(NULL) == DSD_INVALID_SOCKET);
    assert(dsd_net_audio_input_hook_udp_start(NULL, "127.0.0.1", 7355, 48000) == -1);
    dsd_net_audio_input_hook_udp_stop(NULL);
    assert(dsd_net_audio_input_hook_udp_read_sample(NULL, NULL) == 0);

    dsd_net_audio_input_hooks_set((dsd_net_audio_input_hooks){
        .tcp_open = fake_tcp_open,
        .tcp_close = fake_tcp_close,
        .tcp_read_sample = fake_tcp_read_sample,
        .tcp_is_valid = fake_tcp_is_valid,
        .tcp_get_socket = fake_tcp_get_socket,
        .udp_start = fake_udp_start,
        .udp_stop = fake_udp_stop,
        .udp_read_sample = fake_udp_read_sample,
    });

    reset_fakes();
    dsd_socket_t sock = (dsd_socket_t)7;
    tcp_input_ctx* ctx = dsd_net_audio_input_hook_tcp_open(sock, 12345);
    assert(ctx != NULL);
    assert(g_tcp_open_calls == 1);
    assert(g_last_sockfd == sock);
    assert(g_last_samplerate == 12345);

    dsd_net_audio_input_hook_tcp_close(ctx);
    assert(g_tcp_close_calls == 1);
    assert(g_last_tcp_ctx == ctx);

    int16_t s = 0;
    assert(dsd_net_audio_input_hook_tcp_read_sample(ctx, &s) == 1);
    assert(g_tcp_read_calls == 1);
    assert(g_last_tcp_ctx == ctx);
    assert(s == (int16_t)123);

    assert(dsd_net_audio_input_hook_tcp_is_valid(ctx) == 1);
    assert(g_tcp_is_valid_calls == 1);

    assert(dsd_net_audio_input_hook_tcp_get_socket(ctx) == (dsd_socket_t)42);
    assert(g_tcp_get_socket_calls == 1);

    dsd_opts* opts = (dsd_opts*)calloc(1, 1);
    assert(opts != NULL);
    assert(dsd_net_audio_input_hook_udp_start(opts, "0.0.0.0", 7355, 48000) == 0);
    assert(g_udp_start_calls == 1);
    assert(g_last_udp_opts == opts);
    assert(g_last_bindaddr != NULL);
    assert(g_last_port == 7355);
    assert(g_last_samplerate == 48000);

    dsd_net_audio_input_hook_udp_stop(opts);
    assert(g_udp_stop_calls == 1);
    assert(g_last_udp_opts == opts);

    s = 0;
    assert(dsd_net_audio_input_hook_udp_read_sample(opts, &s) == 1);
    assert(g_udp_read_calls == 1);
    assert(g_last_udp_opts == opts);
    assert(s == (int16_t)-7);

    free(opts);
    return 0;
}
