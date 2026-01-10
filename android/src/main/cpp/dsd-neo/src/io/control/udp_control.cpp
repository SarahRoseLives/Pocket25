// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief UDP-based remote control interface implementation.
 *
 * Provides a background UDP listener to accept retune commands and invokes a
 * user-supplied callback with the requested frequency. Supports clean start/stop
 * semantics and resource management.
 */

#include <dsd-neo/io/udp_control.h>
#include <dsd-neo/platform/sockets.h>
#include <dsd-neo/platform/threading.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/runtime/log.h>

struct udp_control {
    int port;
    dsd_socket_t sockfd;
    dsd_thread_t thread;
    udp_control_retune_cb cb;
    void* user_data;
    volatile int stop_flag;
};

/**
 * @brief Convert 4-byte little-endian payload (following a leading command byte)
 * to a 32-bit unsigned integer.
 *
 * @param buf Pointer to 5-byte buffer where buf[0] is a command and buf[1..4]
 *            encode the value as little-endian.
 * @return Parsed 32-bit value.
 */
static unsigned int
udp_chars_to_int(unsigned char* buf) {
    int i;
    unsigned int val = 0;
    for (i = 1; i < 5; i++) {
        val = val | ((buf[i]) << ((i - 1) * 8));
    }
    return val;
}

/**
 * @brief UDP control thread entry.
 *
 * Binds to INADDR_ANY:udp_port and listens for 5-byte messages. When a valid
 * tune command is received, invokes the registered callback with the new
 * frequency.
 *
 * @param arg Pointer to `udp_control`.
 * @return NULL on exit.
 */
static DSD_THREAD_RETURN_TYPE
#if DSD_PLATFORM_WIN_NATIVE
    __stdcall
#endif
    udp_thread_fn(void* arg) {
    udp_control* ctrl = (udp_control*)arg;
    int n;
    unsigned char buffer[5];
    struct sockaddr_in serv_addr;

    ctrl->sockfd = dsd_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ctrl->sockfd == DSD_INVALID_SOCKET) {
        perror("ERROR opening socket");
        DSD_THREAD_RETURN;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons((uint16_t)ctrl->port);

    if (dsd_socket_bind(ctrl->sockfd, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) != 0) {
        perror("ERROR on binding");
        dsd_socket_close(ctrl->sockfd);
        ctrl->sockfd = DSD_INVALID_SOCKET;
        DSD_THREAD_RETURN;
    }

    memset(buffer, 0, sizeof(buffer));
    LOG_INFO("Main socket started! :-) Tuning enabled on UDP/%d \n", ctrl->port);

    while (!ctrl->stop_flag && (n = dsd_socket_recv(ctrl->sockfd, buffer, 5, 0)) > 0) {
        if (n == 5 && buffer[0] == 0) {
            unsigned int new_freq = udp_chars_to_int(buffer);
            if (ctrl->cb) {
                ctrl->cb(new_freq, ctrl->user_data);
            }
            LOG_INFO("\nTuning to: %u [Hz] \n", new_freq);
        }
    }
    if (!ctrl->stop_flag && n < 0) {
        perror("ERROR on read");
    }

    if (ctrl->sockfd != DSD_INVALID_SOCKET) {
        dsd_socket_close(ctrl->sockfd);
        ctrl->sockfd = DSD_INVALID_SOCKET;
    }
    DSD_THREAD_RETURN;
}

/**
 * @brief Start UDP control thread.
 *
 * Starts a background UDP listener on the specified port. On valid messages,
 * invokes the provided retune callback with the parsed frequency.
 *
 * @param udp_port UDP port to bind and listen on (0 disables/start no-op).
 * @param cb Callback invoked upon receiving a valid retune command.
 * @param user_data Opaque pointer passed to the callback.
 * @return Opaque handle on success; NULL on failure.
 */
extern "C" udp_control*
udp_control_start(int udp_port, udp_control_retune_cb cb, void* user_data) {
    if (udp_port == 0) {
        return NULL;
    }
    udp_control* ctrl = (udp_control*)malloc(sizeof(udp_control));
    if (!ctrl) {
        return NULL;
    }
    ctrl->port = udp_port;
    ctrl->sockfd = DSD_INVALID_SOCKET;
    ctrl->cb = cb;
    ctrl->user_data = user_data;
    ctrl->stop_flag = 0;
    int rc = dsd_thread_create(&ctrl->thread, udp_thread_fn, ctrl);
    if (rc != 0) {
        free(ctrl);
        return NULL;
    }
    return ctrl;
}

/**
 * @brief Stop UDP control thread and free resources.
 *
 * Closes the socket, joins the worker thread, and releases the handle.
 *
 * @param ctrl Opaque handle returned by `udp_control_start` (safe to pass NULL).
 */
extern "C" void
udp_control_stop(udp_control* ctrl) {
    if (!ctrl) {
        return;
    }
    ctrl->stop_flag = 1;
    if (ctrl->sockfd != DSD_INVALID_SOCKET) {
        dsd_socket_shutdown(ctrl->sockfd, SHUT_RDWR);
    }
    dsd_thread_join(ctrl->thread);
    if (ctrl->sockfd != DSD_INVALID_SOCKET) {
        dsd_socket_close(ctrl->sockfd);
        ctrl->sockfd = DSD_INVALID_SOCKET;
    }
    free(ctrl);
}
