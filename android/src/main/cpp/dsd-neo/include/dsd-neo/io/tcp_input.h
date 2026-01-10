// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief TCP PCM16LE audio input backend.
 *
 * Provides a cross-platform abstraction for TCP audio input. On POSIX systems
 * (Linux, macOS), this uses libsndfile's sf_open_fd() to wrap the socket.
 * On native Windows (MSVC/MinGW), sockets are not file descriptors, so we read
 * directly from the socket using recv() and buffer samples manually.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/platform/sockets.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque TCP input context.
 *
 * On POSIX, wraps a SNDFILE* handle. On Windows native, manages a socket
 * read buffer for direct sample extraction.
 */
typedef struct tcp_input_ctx tcp_input_ctx;

/**
 * @brief Open TCP audio input on an already-connected socket.
 *
 * Takes ownership of the socket for reading. The socket should already be
 * connected to a TCP server streaming PCM16LE audio data.
 *
 * @param sockfd Connected TCP socket.
 * @param samplerate Expected sample rate in Hz (e.g., 48000).
 * @return Context pointer on success; NULL on error.
 */
tcp_input_ctx* tcp_input_open(dsd_socket_t sockfd, int samplerate);

/**
 * @brief Close TCP audio input and release resources.
 *
 * Does NOT close the underlying socket; caller retains socket ownership.
 *
 * @param ctx Context to close (may be NULL).
 */
void tcp_input_close(tcp_input_ctx* ctx);

/**
 * @brief Read one PCM16 sample from TCP input.
 *
 * Blocking read; returns when a sample is available or on error/EOF.
 *
 * @param ctx TCP input context.
 * @param out [out] Receives one sample.
 * @return 1 on success, 0 on EOF/error/disconnect.
 */
int tcp_input_read_sample(tcp_input_ctx* ctx, int16_t* out);

/**
 * @brief Check if TCP input context is valid and connected.
 *
 * @param ctx TCP input context (may be NULL).
 * @return 1 if valid and ready, 0 otherwise.
 */
int tcp_input_is_valid(tcp_input_ctx* ctx);

/**
 * @brief Get the underlying socket from a TCP input context.
 *
 * Useful for reconnection logic that needs to close/reopen the socket.
 *
 * @param ctx TCP input context.
 * @return Socket handle, or DSD_INVALID_SOCKET if ctx is NULL.
 */
dsd_socket_t tcp_input_get_socket(tcp_input_ctx* ctx);

#ifdef __cplusplus
}
#endif
