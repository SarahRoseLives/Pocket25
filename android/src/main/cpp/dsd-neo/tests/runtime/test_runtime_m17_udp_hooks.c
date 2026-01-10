// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dsd-neo/runtime/m17_udp_hooks.h>

static int g_udp_bind_calls = 0;
static int g_connect_calls = 0;
static int g_receiver_calls = 0;
static int g_blaster_calls = 0;
static char* g_last_hostname = NULL;
static int g_last_portno = 0;
static dsd_opts* g_last_opts = NULL;
static dsd_state* g_last_state = NULL;
static size_t g_last_nsam = 0;
static void* g_last_data = NULL;

static void
reset_state(void) {
    g_udp_bind_calls = 0;
    g_connect_calls = 0;
    g_receiver_calls = 0;
    g_blaster_calls = 0;
    g_last_hostname = NULL;
    g_last_portno = 0;
    g_last_opts = NULL;
    g_last_state = NULL;
    g_last_nsam = 0;
    g_last_data = NULL;
}

static dsd_socket_t
fake_udp_bind(char* hostname, int portno) {
    g_udp_bind_calls++;
    g_last_hostname = hostname;
    g_last_portno = portno;
    return (dsd_socket_t)123;
}

static int
fake_connect(dsd_opts* opts, dsd_state* state) {
    g_connect_calls++;
    g_last_opts = opts;
    g_last_state = state;
    return 11;
}

static int
fake_receiver(dsd_opts* opts, void* data) {
    g_receiver_calls++;
    g_last_opts = opts;
    g_last_data = data;
    return 22;
}

static int
fake_blaster(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    g_blaster_calls++;
    g_last_opts = opts;
    g_last_state = state;
    g_last_nsam = nsam;
    g_last_data = data;
    return 33;
}

int
main(void) {
    dsd_m17_udp_hooks_set((dsd_m17_udp_hooks){0});
    assert(dsd_m17_udp_hook_udp_bind(NULL, 0) == DSD_INVALID_SOCKET);
    assert(dsd_m17_udp_hook_connect(NULL, NULL) == -1);
    assert(dsd_m17_udp_hook_receiver(NULL, NULL) == -1);
    assert(dsd_m17_udp_hook_blaster(NULL, NULL, 0, NULL) == -1);

    dsd_opts* opts = (dsd_opts*)calloc(1, 1);
    dsd_state* state = (dsd_state*)calloc(1, 1);
    assert(opts != NULL);
    assert(state != NULL);

    unsigned char data[16] = {0};
    char hostname[] = "127.0.0.1";

    reset_state();

    dsd_m17_udp_hooks_set((dsd_m17_udp_hooks){
        .udp_bind = fake_udp_bind,
        .connect = fake_connect,
        .receiver = fake_receiver,
        .blaster = fake_blaster,
    });

    dsd_socket_t sock = dsd_m17_udp_hook_udp_bind(hostname, 789);
    assert(sock == (dsd_socket_t)123);
    assert(g_udp_bind_calls == 1);
    assert(g_last_hostname == hostname);
    assert(g_last_portno == 789);

    reset_state();
    assert(dsd_m17_udp_hook_connect(opts, state) == 11);
    assert(g_connect_calls == 1);
    assert(g_last_opts == opts);
    assert(g_last_state == state);

    reset_state();
    assert(dsd_m17_udp_hook_receiver(opts, data) == 22);
    assert(g_receiver_calls == 1);
    assert(g_last_opts == opts);
    assert(g_last_data == data);

    reset_state();
    assert(dsd_m17_udp_hook_blaster(opts, state, 456u, data) == 33);
    assert(g_blaster_calls == 1);
    assert(g_last_opts == opts);
    assert(g_last_state == state);
    assert(g_last_nsam == 456u);
    assert(g_last_data == data);

    free(state);
    free(opts);
    return 0;
}
