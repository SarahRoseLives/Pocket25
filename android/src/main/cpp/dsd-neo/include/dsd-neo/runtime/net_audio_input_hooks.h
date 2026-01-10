// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime hook table for optional TCP/UDP PCM input backends.
 *
 * Lower layers should not depend on IO backend headers directly. The engine
 * installs real hook functions at startup; the runtime provides safe wrappers
 * with defaults when hooks are not installed.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/platform/sockets.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tcp_input_ctx tcp_input_ctx;

typedef struct {
    tcp_input_ctx* (*tcp_open)(dsd_socket_t sockfd, int samplerate);
    void (*tcp_close)(tcp_input_ctx* ctx);
    int (*tcp_read_sample)(tcp_input_ctx* ctx, int16_t* out);
    int (*tcp_is_valid)(tcp_input_ctx* ctx);
    dsd_socket_t (*tcp_get_socket)(tcp_input_ctx* ctx);

    int (*udp_start)(dsd_opts* opts, const char* bindaddr, int port, int samplerate);
    void (*udp_stop)(dsd_opts* opts);
    int (*udp_read_sample)(dsd_opts* opts, int16_t* out);
} dsd_net_audio_input_hooks;

void dsd_net_audio_input_hooks_set(dsd_net_audio_input_hooks hooks);

tcp_input_ctx* dsd_net_audio_input_hook_tcp_open(dsd_socket_t sockfd, int samplerate);
void dsd_net_audio_input_hook_tcp_close(tcp_input_ctx* ctx);
int dsd_net_audio_input_hook_tcp_read_sample(tcp_input_ctx* ctx, int16_t* out);
int dsd_net_audio_input_hook_tcp_is_valid(tcp_input_ctx* ctx);
dsd_socket_t dsd_net_audio_input_hook_tcp_get_socket(tcp_input_ctx* ctx);

int dsd_net_audio_input_hook_udp_start(dsd_opts* opts, const char* bindaddr, int port, int samplerate);
void dsd_net_audio_input_hook_udp_stop(dsd_opts* opts);
int dsd_net_audio_input_hook_udp_read_sample(dsd_opts* opts, int16_t* out);

#ifdef __cplusplus
}
#endif
