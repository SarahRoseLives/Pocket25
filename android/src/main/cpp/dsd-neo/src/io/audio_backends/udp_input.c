// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* UDP PCM16LE input backend */

#include <dsd-neo/platform/platform.h>
#include <dsd-neo/platform/sockets.h>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/platform/timing.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/io/udp_input.h>
#include <dsd-neo/runtime/exitflag.h>

/** @brief Simple single-producer/single-consumer ring for PCM16 samples. */
typedef struct udp_input_ring {
    int16_t* buf;
    size_t cap; // in samples
    size_t head;
    size_t tail;
    dsd_mutex_t m;
    dsd_cond_t cv;
} udp_input_ring;

/** @brief UDP input backend state shared across the reader thread and callers. */
typedef struct udp_input_ctx {
    dsd_socket_t sockfd;
    int running;
    udp_input_ring ring;
    dsd_thread_t th;
    int sample_rate;
} udp_input_ctx;

/**
 * @brief Initialize the sample ring with the requested capacity.
 * @param r Ring to initialize.
 * @param cap_samples Capacity in samples (allocates backing buffer).
 */
static void
ring_init(udp_input_ring* r, size_t cap_samples) {
    r->buf = (int16_t*)malloc(cap_samples * sizeof(int16_t));
    r->cap = (r->buf ? cap_samples : 0);
    r->head = r->tail = 0;
    dsd_mutex_init(&r->m);
    dsd_cond_init(&r->cv);
}

/**
 * @brief Destroy the ring buffer and free resources.
 * @param r Ring to destroy.
 */
static void
ring_destroy(udp_input_ring* r) {
    if (r->buf) {
        free(r->buf);
    }
    r->buf = NULL;
    r->cap = r->head = r->tail = 0;
    dsd_mutex_destroy(&r->m);
    dsd_cond_destroy(&r->cv);
}

/**
 * @brief Return number of samples currently buffered.
 * @param r Ring to query.
 */
static size_t
ring_used(const udp_input_ring* r) {
    return (r->head + r->cap - r->tail) % r->cap;
}

/**
 * @brief Return remaining free slots in the ring.
 * @param r Ring to query.
 */
static size_t
ring_free_space(const udp_input_ring* r) {
    return r->cap - 1 - ring_used(r);
}

/**
 * @brief Write as many samples as will fit into the ring.
 *
 * Drops trailing samples when the ring is full.
 *
 * @param r Ring to write into.
 * @param data Source samples.
 * @param count Number of samples to attempt to write.
 * @return Number of samples actually written.
 */
static size_t
ring_write(udp_input_ring* r, const int16_t* data, size_t count) {
    size_t w = 0;
    while (w < count && ring_free_space(r) > 0) {
        r->buf[r->head] = data[w++];
        r->head = (r->head + 1) % r->cap;
    }
    return w;
}

/** @brief Wake any thread blocked on the ring condition variable. */
static void
ring_signal(udp_input_ring* r) {
    dsd_mutex_lock(&r->m);
    dsd_cond_signal(&r->cv);
    dsd_mutex_unlock(&r->m);
}

/**
 * @brief Background UDP receive thread that widens PCM16LE datagrams.
 *
 * Receives UDP datagrams, converts to int16 samples, and pushes them into the
 * ring while tracking drop statistics on overflow.
 *
 * @param arg Pointer to owning `dsd_opts`.
 * @return NULL on exit.
 */
static DSD_THREAD_RETURN_TYPE
#if DSD_PLATFORM_WIN_NATIVE
    __stdcall
#endif
    udp_rx_thread(void* arg) {
    dsd_opts* opts = (dsd_opts*)arg;
    udp_input_ctx* ctx = (udp_input_ctx*)opts->udp_in_ctx;
    const size_t max_bytes = 65536;
    uint8_t* buf = (uint8_t*)malloc(max_bytes);
    if (!buf) {
        DSD_THREAD_RETURN;
    }

    while (ctx->running) {
        int n = dsd_socket_recv(ctx->sockfd, buf, max_bytes, 0);
        if (n < 0) {
            int err = dsd_socket_get_error();
#if DSD_PLATFORM_WIN_NATIVE
            if (err == WSAEINTR) {
                continue;
            }
            if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
                dsd_sleep_ms(1);
                continue;
            }
#else
            if (err == EINTR) {
                continue;
            }
            if (err == EAGAIN || err == EWOULDBLOCK) {
                dsd_sleep_ms(1);
                continue;
            }
#endif
            // fatal error -> stop
            break;
        }
        if (n == 0) {
            // no data
            continue;
        }
        opts->udp_in_packets++;
        opts->udp_in_bytes += (unsigned long long)n;
        // Must be even number of bytes for int16 samples
        size_t nsamp = (size_t)(n / 2);
        if (nsamp == 0) {
            continue;
        }

        // Write into ring, account for drops
        const int16_t* s = (const int16_t*)buf; // assumes little-endian input
        size_t wrote = 0;
        dsd_mutex_lock(&ctx->ring.m);
        wrote = ring_write(&ctx->ring, s, nsamp);
        if (wrote < nsamp) {
            opts->udp_in_drops += (unsigned long long)(nsamp - wrote);
        }
        dsd_mutex_unlock(&ctx->ring.m);
        ring_signal(&ctx->ring);
    }

    free(buf);
    DSD_THREAD_RETURN;
}

/**
 * @brief Start UDP input thread and ring.
 *
 * Creates the UDP socket, configures timeouts/buffers, spawns the reader
 * thread, and installs the context into `opts->udp_in_ctx`.
 *
 * @param opts Decoder options receiving backend context and counters.
 * @param bindaddr Address to bind (IPv4/IPv6 string).
 * @param port UDP port number.
 * @param samplerate Expected sample rate (Hz) used for ring sizing.
 * @return 0 on success; negative on error.
 */
int
udp_input_start(dsd_opts* opts, const char* bindaddr, int port, int samplerate) {
    if (!opts) {
        return -1;
    }
    if (opts->udp_in_ctx) {
        return 0; // already started
    }

    dsd_socket_t sockfd = dsd_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == DSD_INVALID_SOCKET) {
        fprintf(stderr, "Error creating UDP input socket\n");
        return -1;
    }

    // Increase OS receive buffer if possible
    int rcvbuf = 4 * 1024 * 1024;
    (void)dsd_socket_setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    // Set a short receive timeout so thread can notice stop requests
    (void)dsd_socket_set_recv_timeout(sockfd, 200); // 200ms

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (bindaddr && strlen(bindaddr) > 0) {
        if (strcmp(bindaddr, "0.0.0.0") == 0) {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        } else {
            /* Parse numeric address */
            if (dsd_socket_resolve(bindaddr, port, &addr) != 0) {
                fprintf(stderr, "Invalid UDP bind address: %s\n", bindaddr);
                dsd_socket_close(sockfd);
                return -1;
            }
        }
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    if (dsd_socket_bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Failed to bind UDP %s:%d\n", bindaddr ? bindaddr : "127.0.0.1", port);
        dsd_socket_close(sockfd);
        return -1;
    }

    udp_input_ctx* ctx = (udp_input_ctx*)calloc(1, sizeof(udp_input_ctx));
    if (!ctx) {
        dsd_socket_close(sockfd);
        return -1;
    }
    ctx->sockfd = sockfd;
    ctx->running = 1;
    ctx->sample_rate = samplerate;

    // Ring capacity: ~500ms at samplerate
    size_t cap = (size_t)(samplerate / 2);
    if (cap < 48000) {
        cap = 48000; // minimum
    }
    ring_init(&ctx->ring, cap);
    if (ctx->ring.cap == 0) {
        dsd_socket_close(sockfd);
        free(ctx);
        return -1;
    }

    opts->udp_in_ctx = ctx;
    opts->udp_in_sockfd = sockfd;
    int rc = dsd_thread_create(&ctx->th, (dsd_thread_fn)udp_rx_thread, opts);
    if (rc != 0) {
        ring_destroy(&ctx->ring);
        dsd_socket_close(sockfd);
        free(ctx);
        opts->udp_in_ctx = NULL;
        opts->udp_in_sockfd = DSD_INVALID_SOCKET;
        return -1;
    }
    return 0;
}

/**
 * @brief Stop UDP input backend and reclaim resources.
 * @param opts Decoder options containing the UDP context.
 */
void
udp_input_stop(dsd_opts* opts) {
    if (!opts || !opts->udp_in_ctx) {
        return;
    }
    udp_input_ctx* ctx = (udp_input_ctx*)opts->udp_in_ctx;
    ctx->running = 0;
    if (ctx->sockfd != DSD_INVALID_SOCKET) {
        dsd_socket_shutdown(ctx->sockfd, SHUT_RD);
        dsd_socket_close(ctx->sockfd);
    }
    ctx->sockfd = DSD_INVALID_SOCKET;
    // wake any blocked reader
    ring_signal(&ctx->ring);
    dsd_thread_join(ctx->th);
    ring_destroy(&ctx->ring);
    free(ctx);
    opts->udp_in_ctx = NULL;
    opts->udp_in_sockfd = DSD_INVALID_SOCKET;
}

/**
 * @brief Read a single sample from the UDP ring.
 *
 * Blocks (with a short timed wait) until a real sample is available or the
 * decoder is shutting down. This avoids inserting synthetic samples, which
 * skews symbol timing and breaks demod/framesync.
 *
 * @param opts Decoder options containing UDP context.
 * @param out [out] Receives one PCM16 sample.
 * @return 1 on success, 0 on shutdown.
 */
int
udp_input_read_sample(dsd_opts* opts, int16_t* out) {
    if (!opts || !opts->udp_in_ctx || !out) {
        return 0;
    }
    udp_input_ctx* ctx = (udp_input_ctx*)opts->udp_in_ctx;
    if (!ctx->running) {
        return 0;
    }
    // Block until we have a real sample (do not synthesize silence; it breaks symbol timing).
    dsd_mutex_lock(&ctx->ring.m);
    while (ring_used(&ctx->ring) == 0) {
        if (exitflag || !ctx->running) {
            dsd_mutex_unlock(&ctx->ring.m);
            return 0;
        }
        int ret = dsd_cond_timedwait(&ctx->ring.cv, &ctx->ring.m, 100); // 100ms timeout
        (void)ret;                                                      // tolerate spurious wakeups
    }
    *out = ctx->ring.buf[ctx->ring.tail];
    ctx->ring.tail = (ctx->ring.tail + 1) % ctx->ring.cap;
    dsd_mutex_unlock(&ctx->ring.m);
    return 1;
}
