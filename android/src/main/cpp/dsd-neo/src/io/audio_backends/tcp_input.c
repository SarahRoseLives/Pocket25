// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief TCP PCM16LE audio input backend implementation.
 *
 * Platform-specific handling:
 * - POSIX (Linux, macOS): Uses libsndfile sf_open_fd() to wrap socket
 * - Windows native (MSVC/MinGW): Direct socket recv() with internal buffering
 */

#include <dsd-neo/io/tcp_input.h>
#include <dsd-neo/platform/platform.h>
#include <dsd-neo/platform/sockets.h>

#include <stdlib.h>
#include <string.h>

#if !DSD_PLATFORM_WIN_NATIVE
/* POSIX path: use libsndfile */
#include <sndfile.h>
#endif

/**
 * @brief Internal TCP input context structure.
 */
struct tcp_input_ctx {
    dsd_socket_t sockfd;
    int samplerate;
    int valid;

#if DSD_PLATFORM_WIN_NATIVE
    /* Windows native: manual buffering for socket reads */
    uint8_t* buf;   /**< Read buffer */
    size_t buf_cap; /**< Buffer capacity in bytes */
    size_t buf_pos; /**< Current read position in buffer */
    size_t buf_len; /**< Valid bytes in buffer */
#else
    /* POSIX: libsndfile handle */
    SNDFILE* sf;
    SF_INFO sf_info;
#endif
};

/* Buffer size for Windows native socket reads (~100ms at 48kHz stereo) */
#define TCP_INPUT_BUF_SIZE (48000 * 2 * sizeof(int16_t) / 10)

tcp_input_ctx*
tcp_input_open(dsd_socket_t sockfd, int samplerate) {
    if (sockfd == DSD_INVALID_SOCKET) {
        return NULL;
    }

    tcp_input_ctx* ctx = (tcp_input_ctx*)calloc(1, sizeof(tcp_input_ctx));
    if (!ctx) {
        return NULL;
    }

    ctx->sockfd = sockfd;
    ctx->samplerate = samplerate;

#if DSD_PLATFORM_WIN_NATIVE
    /* Windows native: allocate read buffer */
    ctx->buf_cap = TCP_INPUT_BUF_SIZE;
    ctx->buf = (uint8_t*)malloc(ctx->buf_cap);
    if (!ctx->buf) {
        free(ctx);
        return NULL;
    }
    ctx->buf_pos = 0;
    ctx->buf_len = 0;
    ctx->valid = 1;
#else
    /* POSIX: wrap socket with libsndfile */
    memset(&ctx->sf_info, 0, sizeof(ctx->sf_info));
    ctx->sf_info.samplerate = samplerate;
    ctx->sf_info.channels = 1;
    ctx->sf_info.format = SF_FORMAT_RAW | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;

    /* sf_open_fd takes ownership for reading but we pass 0 for close_desc
     * so sf_close won't close the socket - we manage that ourselves */
    ctx->sf = sf_open_fd((int)sockfd, SFM_READ, &ctx->sf_info, 0);
    if (!ctx->sf) {
        free(ctx);
        return NULL;
    }
    ctx->valid = 1;
#endif

    return ctx;
}

void
tcp_input_close(tcp_input_ctx* ctx) {
    if (!ctx) {
        return;
    }

#if DSD_PLATFORM_WIN_NATIVE
    if (ctx->buf) {
        free(ctx->buf);
        ctx->buf = NULL;
    }
#else
    if (ctx->sf) {
        sf_close(ctx->sf);
        ctx->sf = NULL;
    }
#endif

    ctx->valid = 0;
    free(ctx);
}

int
tcp_input_read_sample(tcp_input_ctx* ctx, int16_t* out) {
    if (!ctx || !ctx->valid || !out) {
        return 0;
    }

#if DSD_PLATFORM_WIN_NATIVE
    /* Windows native: read from buffer, refill from socket as needed */

    /* Need 2 bytes for one int16_t sample */
    while (ctx->buf_len - ctx->buf_pos < 2) {
        /* Not enough data in buffer, need to read more from socket */
        if (ctx->buf_pos > 0 && ctx->buf_len > ctx->buf_pos) {
            /* Move remaining partial byte(s) to start of buffer */
            memmove(ctx->buf, ctx->buf + ctx->buf_pos, ctx->buf_len - ctx->buf_pos);
            ctx->buf_len -= ctx->buf_pos;
            ctx->buf_pos = 0;
        } else {
            /* Buffer empty, reset positions */
            ctx->buf_pos = 0;
            ctx->buf_len = 0;
        }

        /* Read from socket */
        int n = dsd_socket_recv(ctx->sockfd, ctx->buf + ctx->buf_len, ctx->buf_cap - ctx->buf_len, 0);
        if (n <= 0) {
            /* Error or connection closed */
            ctx->valid = 0;
            return 0;
        }
        ctx->buf_len += (size_t)n;
    }

    /* Extract one little-endian int16_t sample */
    uint8_t lo = ctx->buf[ctx->buf_pos++];
    uint8_t hi = ctx->buf[ctx->buf_pos++];
    *out = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));

    return 1;

#else
    /* POSIX: use libsndfile */
    short s = 0;
    sf_count_t result = sf_read_short(ctx->sf, &s, 1);
    if (result != 1) {
        ctx->valid = 0;
        return 0;
    }
    *out = s;
    return 1;
#endif
}

int
tcp_input_is_valid(tcp_input_ctx* ctx) {
    if (!ctx) {
        return 0;
    }
    return ctx->valid;
}

dsd_socket_t
tcp_input_get_socket(tcp_input_ctx* ctx) {
    if (!ctx) {
        return DSD_INVALID_SOCKET;
    }
    return ctx->sockfd;
}
