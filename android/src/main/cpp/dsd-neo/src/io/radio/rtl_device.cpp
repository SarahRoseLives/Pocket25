// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief RTL-SDR device I/O implementation and USB ingestion pipeline.
 *
 * Implements the opaque `rtl_device` handle, device configuration helpers,
 * realtime threading hooks, and the asynchronous USB callback that widens
 * u8 I/Q samples into normalized float and feeds the `input_ring_state`.
 */

#include <atomic>
#include <dsd-neo/dsp/simd_widen.h>
#include <dsd-neo/io/rtl_device.h>
#include <dsd-neo/platform/sockets.h>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/platform/timing.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/input_ring.h>
#include <dsd-neo/runtime/rt_sched.h>
#include <errno.h>
#include <math.h>
#include <rtl-sdr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && DSD_PLATFORM_WIN_NATIVE
#include <excpt.h>
#endif

#if DSD_PLATFORM_POSIX
#include <strings.h>
#include <unistd.h>
#endif
/* Some platforms (e.g. non-glibc) may not define MSG_NOSIGNAL */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include <stdint.h>

// Shutdown signaling (defined in src/runtime/exitflag.c)
extern "C" volatile uint8_t exitflag;

// Internal RTL device structure
struct rtl_device {
    rtlsdr_dev_t* dev;
    int dev_index;
    uint32_t freq;
    uint32_t rate;
    int gain;
    uint32_t buf_len;
    int ppm_error;
    int offset_tuning;
    int direct_sampling;
    std::atomic<int> mute;
    dsd_thread_t thread;
    int thread_started;
    struct input_ring_state* input_ring;
    int combine_rotate_enabled;
    /* Backend selector: 0 = USB (librtlsdr), 1 = rtl_tcp */
    int backend;
    /* rtl_tcp connection */
    dsd_socket_t sockfd;
    char host[1024];
    int port;
    std::atomic<int> run;
    int agc_mode; /* cached for TCP backend */
    int bias_tee_on;
    int tcp_autotune; /* adaptive recv/buffering */
    /* TCP stats (optional) */
    uint64_t tcp_bytes_total;
    uint64_t tcp_bytes_window;
    uint64_t reserve_full_events;
    int stats_enabled;
    uint64_t stats_last_ns;
    /* TCP reassembly to uniform chunk size */
    unsigned char* tcp_pending;
    size_t tcp_pending_len;
    size_t tcp_pending_cap;
    /* Extra driver state for reconnect replay */
    int testmode_on;
    uint32_t rtl_xtal_hz;
    uint32_t tuner_xtal_hz;

    struct {
        int stage;
        int gain;
    } if_gains[16];

    int if_gain_count;
};

/**
 * Hint that a pointer is aligned to a compile-time boundary for vectorization.
 *
 * This is a lightweight wrapper over compiler intrinsics to improve
 * auto-vectorization by promising the compiler that the pointer meets the
 * specified alignment. Use with care and only when the alignment guarantee
 * is actually met.
 */
#ifndef DSD_NEO_RESTRICT
#if defined(_MSC_VER)
#define DSD_NEO_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define DSD_NEO_RESTRICT __restrict__
#else
#define DSD_NEO_RESTRICT
#endif
#endif

/**
 * @brief Optionally enable realtime scheduling and set CPU affinity for the current
 * thread based on environment variables.
 *
 * When `DSD_NEO_RT_SCHED=1`, attempts to switch the calling thread to
 * SCHED_FIFO with a priority derived from `DSD_NEO_RT_PRIO_<ROLE>` if present.
 * If `DSD_NEO_CPU_<ROLE>` is set to a valid CPU index, pins the thread to that
 * CPU.
 *
 * @param role Optional role label (e.g. "DEMOD", "DONGLE") used to look up
 *             per-role environment variables.
 */

/**
 * @brief Rotate IQ data by 90 degrees in-place.
 *
 * @param buf Interleaved IQ byte buffer.
 * @param len Buffer length in bytes (processed in blocks of 8).
 */
static void
rotate_90(unsigned char* buf, uint32_t len) {
    uint32_t i;
    unsigned char tmp;
    /* Process only full 8-byte blocks (4 IQ pairs) to avoid overrun */
    uint32_t full = len - (len % 8);
    for (i = 0; i < full; i += 8) {
        /* uint8_t negation = 255 - x */
        tmp = 255 - buf[i + 3];
        buf[i + 3] = buf[i + 2];
        buf[i + 2] = tmp;

        tmp = 255 - buf[i + 2];
        buf[i + 2] = buf[i + 3];
        buf[i + 3] = tmp;

        tmp = 255 - buf[i + 6];
        buf[i + 6] = buf[i + 7];
        buf[i + 7] = tmp;

        tmp = 255 - buf[i + 7];
        buf[i + 7] = buf[i + 6];
        buf[i + 6] = tmp;
    }
}

/**
 * @brief RTL-SDR asynchronous USB callback.
 * Converts incoming u8 I/Q to normalized float and enqueues into the input ring. If
 * `offset_tuning` is off and `DSD_NEO_COMBINE_ROT` is enabled (default), a
 * combined rotate+widen implementation is used. Otherwise it falls back to
 * legacy two-pass (rotate_90 u8, then widen subtracting 128) or a simple
 * widen subtracting 127. On overflow, drops oldest ring data to avoid stalls.
 *
 * @param buf USB I/Q byte buffer.
 * @param len Buffer length in bytes (I/Q interleaved).
 * @param ctx Opaque pointer to `rtl_device`.
 */
static void
rtlsdr_callback(unsigned char* buf, uint32_t len, void* ctx) {
    struct rtl_device* s = static_cast<rtl_device*>(ctx);

    /* One-time: ensure the USB callback thread gets RT scheduling/affinity if enabled */
    {
        static std::atomic<int> usb_sched_applied{0};
        int expected = 0;
        if (usb_sched_applied.compare_exchange_strong(expected, 1)) {
            maybe_set_thread_realtime_and_affinity("USB");
        }
    }

    if (exitflag) {
        return;
    }
    if (!ctx) {
        return;
    }
    /* Handle muting: skip (discard) muted samples entirely instead of zero-filling.
     *
     * Previously we set muted samples to 127 (midpoint), which after bias subtraction
     * becomes 0.0. These zero-magnitude samples corrupt the Costas loop and TED when
     * they're processed after the retune gate opens. By discarding them entirely,
     * the demod thread never sees transient samples.
     *
     * Advance buf pointer and reduce len to skip the muted portion. */
    if (s->mute.load(std::memory_order_relaxed) > 0) {
        int old = s->mute.load(std::memory_order_relaxed);
        if (old > 0) {
            uint32_t m = (uint32_t)old;
            if (m >= len) {
                /* Entire buffer is muted - discard all and update counter */
                s->mute.fetch_sub((int)len, std::memory_order_relaxed);
                return; /* Nothing to process */
            }
            /* Partial mute: skip first m bytes, process remainder */
            buf += m;
            len -= m;
            s->mute.fetch_sub((int)m, std::memory_order_relaxed);
        }
    }
    /* Convert incoming u8 I/Q and write directly into input ring without extra copy */
    size_t need = len;
    size_t done = 0;
    /* For legacy two-pass path, rotate the incoming byte buffer once up front */
    int use_two_pass = (!s->offset_tuning && !s->combine_rotate_enabled);
    if (use_two_pass) {
        rotate_90(buf, len);
    }
    while (need > 0) {
        float *p1 = NULL, *p2 = NULL;
        size_t n1 = 0, n2 = 0;
        input_ring_reserve(s->input_ring, need, &p1, &n1, &p2, &n2);
        if (n1 == 0 && n2 == 0) {
            /* Ring full: record drop and give up remaining bytes from this callback */
            if (s->input_ring) {
                s->input_ring->producer_drops.fetch_add((uint64_t)need);
            }
            break;
        }
        /* Ensure even counts to keep I/Q pairs aligned */
        if (n1 & 1) {
            n1--;
        }
        size_t w1 = (n1 < need) ? n1 : need;
        size_t rem_after_w1 = need - w1;
        if (n2 & 1) {
            n2--;
        }
        size_t w2 = (n2 < rem_after_w1) ? n2 : rem_after_w1;

        if (!s->offset_tuning && s->combine_rotate_enabled) {
            if (w1) {
                widen_rotate90_u8_to_f32_bias127(buf + done, p1, (uint32_t)w1);
            }
            if (w2) {
                widen_rotate90_u8_to_f32_bias127(buf + done + w1, p2, (uint32_t)w2);
            }
        } else if (use_two_pass) {
            /* bytes already rotated in-place; widen with 128 subtraction to avoid bias */
            if (w1) {
                widen_u8_to_f32_bias128_scalar(buf + done, p1, (uint32_t)w1);
            }
            if (w2) {
                widen_u8_to_f32_bias128_scalar(buf + done + w1, p2, (uint32_t)w2);
            }
        } else {
            if (w1) {
                widen_u8_to_f32_bias127(buf + done, p1, (uint32_t)w1);
            }
            if (w2) {
                widen_u8_to_f32_bias127(buf + done + w1, p2, (uint32_t)w2);
            }
        }
        input_ring_commit(s->input_ring, w1 + w2);
        done += w1 + w2;
        need -= w1 + w2;
    }
}

/**
 * @brief RTL-SDR USB thread entry: reads samples asynchronously into the input ring.
 * Applies optional realtime scheduling/affinity if configured.
 *
 * @param arg Pointer to `rtl_device`.
 * @return NULL on exit.
 */
static DSD_THREAD_RETURN_TYPE
#if DSD_PLATFORM_WIN_NATIVE
    __stdcall
#endif
    dongle_thread_fn(void* arg) {
    struct rtl_device* s = static_cast<rtl_device*>(arg);
    maybe_set_thread_realtime_and_affinity("DONGLE");
#if defined(_MSC_VER) && DSD_PLATFORM_WIN_NATIVE
    __try {
        rtlsdr_read_async(s->dev, rtlsdr_callback, s, 16, s->buf_len);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr,
                "ERROR: libusb exception in rtlsdr_read_async (MSVC/Windows). "
                "Check that the bundled libusb/librtlsdr DLLs match the build and the device driver is installed.\n");
        exitflag = 1;
    }
#else
    rtlsdr_read_async(s->dev, rtlsdr_callback, s, 16, s->buf_len);
#endif
    DSD_THREAD_RETURN;
}

/* ---- rtl_tcp backend helpers ---- */

/* Connect to rtl_tcp server */
static dsd_socket_t
tcp_connect_host(const char* host, int port) {
    dsd_socket_t sockfd = dsd_socket_create(AF_INET, SOCK_STREAM, 0);
    if (sockfd == DSD_INVALID_SOCKET) {
        fprintf(stderr, "rtl_tcp: ERROR opening socket\n");
        return DSD_INVALID_SOCKET;
    }
    /* Best-effort: enable TCP keepalive to detect half-open links */
    {
        int opt = 1;
        (void)dsd_socket_setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
#if defined(TCP_KEEPIDLE)
        int idle = 15; /* seconds before starting keepalive probes */
        (void)dsd_socket_setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif
#if defined(TCP_KEEPCNT)
        int cnt = 4; /* number of probes */
        (void)dsd_socket_setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
#if defined(TCP_KEEPINTVL)
        int intvl = 5; /* seconds between probes */
        (void)dsd_socket_setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#if defined(TCP_USER_TIMEOUT)
        /* Optional fail-fast if ACKs are not received within timeout (ms) */
        int uto = 20000; /* 20s */
        (void)dsd_socket_setsockopt(sockfd, IPPROTO_TCP, TCP_USER_TIMEOUT, &uto, sizeof(uto));
#endif
    }
    struct sockaddr_in serveraddr;
    if (dsd_socket_resolve(host, port, &serveraddr) != 0) {
        fprintf(stderr, "rtl_tcp: ERROR, no such host as %s\n", host);
        dsd_socket_close(sockfd);
        return DSD_INVALID_SOCKET;
    }
    if (dsd_socket_connect(sockfd, reinterpret_cast<const struct sockaddr*>(&serveraddr), sizeof(serveraddr)) != 0) {
        fprintf(stderr, "rtl_tcp: ERROR connecting to %s:%d\n", host, port);
        dsd_socket_close(sockfd);
        return DSD_INVALID_SOCKET;
    }
    return sockfd;
}

/* Send rtl_tcp command: 1 byte id + 4 byte big-endian value */
static int
rtl_tcp_send_cmd(dsd_socket_t sockfd, uint8_t cmd, uint32_t param) {
    uint8_t buf[5];
    buf[0] = cmd;
    buf[1] = (uint8_t)((param >> 24) & 0xFF);
    buf[2] = (uint8_t)((param >> 16) & 0xFF);
    buf[3] = (uint8_t)((param >> 8) & 0xFF);
    buf[4] = (uint8_t)(param & 0xFF);
    int n = dsd_socket_send(sockfd, buf, 5, MSG_NOSIGNAL);
    return (n == 5) ? 0 : -1;
}

static int
env_agc_want(void) {
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (!cfg) {
        return 1;
    }
    return cfg->rtl_agc_enable ? 1 : 0;
}

/* Read and discard rtl_tcp header: 'RTL0' + tuner(4) + ngains(4) + ngains*4 */
static void
rtl_tcp_skip_header(dsd_socket_t sockfd) {
    uint8_t hdr[12];
    int n = dsd_socket_recv(sockfd, hdr, sizeof(hdr), MSG_WAITALL);
    if (n != (int)sizeof(hdr)) {
        return;
    }
    if (!(hdr[0] == 'R' && hdr[1] == 'T' && hdr[2] == 'L' && hdr[3] == '0')) {
        return;
    }
    /* Parse ngains (last 4 bytes) as big-endian per rtl_tcp */
    uint32_t ngains =
        ((uint32_t)hdr[8] << 24) | ((uint32_t)hdr[9] << 16) | ((uint32_t)hdr[10] << 8) | (uint32_t)hdr[11];
    if (ngains > 0 && ngains < 4096) {
        size_t to_discard = (size_t)ngains * 4U;
        uint8_t buf[1024];
        while (to_discard > 0) {
            size_t chunk = to_discard > sizeof(buf) ? sizeof(buf) : to_discard;
            int r = dsd_socket_recv(sockfd, buf, chunk, MSG_WAITALL);
            if (r <= 0) {
                break;
            }
            to_discard -= (size_t)r;
        }
    }
}

/* TCP reader thread: read u8 IQ, widen to float, push to ring */
static DSD_THREAD_RETURN_TYPE
#if DSD_PLATFORM_WIN_NATIVE
    __stdcall
#endif
    tcp_thread_fn(void* arg) {
    struct rtl_device* s = static_cast<rtl_device*>(arg);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    maybe_set_thread_realtime_and_affinity("DONGLE");
    /* Default read size: for rtl_tcp prefer small (16 KiB) chunks for higher cadence.
       For USB, derive ~20 ms to reduce burstiness. */
    size_t BUFSZ = 0;
    if (s->backend == 1) {
        BUFSZ = 16384; /* ~5 ms @ 1.536 Msps */
    } else {
        if (s->rate > 0) {
            double bytes_per_sec = (double)s->rate * 2.0;   /* 2 bytes per complex sample (u8 I, u8 Q) */
            size_t target = (size_t)(bytes_per_sec * 0.02); /* ~20 ms */
            if (target < 16384) {
                target = 16384; /* lower bound */
            }
            if (target > 262144) {
                target = 262144; /* upper bound */
            }
            BUFSZ = target;
        } else {
            BUFSZ = 65536; /* safe fallback when rate isn't known yet */
        }
    }
    if (cfg && cfg->tcp_bufsz_is_set && cfg->tcp_bufsz_bytes > 0) {
        BUFSZ = (size_t)cfg->tcp_bufsz_bytes;
    }
    unsigned char* u8 = (unsigned char*)malloc(BUFSZ);
    if (!u8) {
        s->run.store(0);
        return 0;
    }
    /* Discard server capability header so following bytes are pure IQ */
    rtl_tcp_skip_header(s->sockfd);
    int waitall = (s->backend == 1) ? 0 : 1; /* rtl_tcp default off; USB default on */
    if (cfg && cfg->tcp_waitall_is_set) {
        waitall = cfg->tcp_waitall_enable ? 1 : 0;
    }
    /* Autotune can override BUFSZ/WAITALL adaptively. Initial state considers env, but
       each loop consults s->tcp_autotune so UI/runtime toggles take effect live. */
    if (!s->tcp_autotune) {
        if (cfg && cfg->tcp_autotune_enable) {
            s->tcp_autotune = 1; /* make it observable to loop */
        }
    }

    /* Track deltas for adaptive decisions */
    uint64_t prev_drops = s->input_ring ? s->input_ring->producer_drops.load() : 0ULL;
    uint64_t prev_rdto = s->input_ring ? s->input_ring->read_timeouts.load() : 0ULL;
    uint64_t prev_res_full = s->reserve_full_events;
    uint64_t auto_last_ns = dsd_time_monotonic_ns();
    /* Less aggressive reconnect: allow a few consecutive timeouts before
       declaring the connection lost. Default 3; override via
       DSD_NEO_TCP_MAX_TIMEOUTS. */
    int timeout_limit = cfg ? cfg->tcp_max_timeouts : 3;
    int consec_timeouts = 0;
    while (s->run.load() && exitflag == 0) {
        /* Light backpressure: if ring is nearly full, yield briefly */
        int autotune = s->tcp_autotune;
        if (autotune && s->input_ring) {
            const size_t SLICE = (s->buf_len > 0 ? (size_t)s->buf_len : 16384);
            size_t free_sp = input_ring_free(s->input_ring);
            if (free_sp < (SLICE * 2)) {
                dsd_sleep_us(500); /* 0.5 ms */
            }
        }
        int r = dsd_socket_recv(s->sockfd, u8, BUFSZ, waitall ? MSG_WAITALL : 0);
        if (r <= 0) {
            /* Timeout or connection closed. On timeout, tolerate up to
               timeout_limit consecutive occurrences before reconnecting. */
            if (!s->run.load() || exitflag) {
                break;
            }
            int e = dsd_socket_get_error();
#if DSD_PLATFORM_WIN_NATIVE
            int is_timeout = (r < 0) && (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT || e == WSAEINTR);
#else
            int is_timeout = (r < 0) && (e == EAGAIN || e == EWOULDBLOCK || e == EINTR);
#endif
            if (is_timeout) {
                consec_timeouts++;
                if (consec_timeouts < timeout_limit) {
                    /* Try again without reconnecting */
                    continue;
                }
            }
            /* Either closed, hard error, or too many consecutive timeouts. */
            fprintf(stderr, "rtl_tcp: input stalled; attempting to reconnect to %s:%d...\n", s->host, s->port);
            /* Preserve current device state to replay after reconnect */
            const uint32_t prev_freq = s->freq;
            const uint32_t prev_rate = s->rate;
            const int prev_gain = s->gain;
            const int prev_agc = s->agc_mode;
            const int prev_ppm = s->ppm_error;
            const int prev_direct = s->direct_sampling;
            const int prev_bias = s->bias_tee_on;
            const int prev_offset = s->offset_tuning;
            const int prev_testmode = s->testmode_on;
            const uint32_t prev_rtl_xtal = s->rtl_xtal_hz;
            const uint32_t prev_tuner_xtal = s->tuner_xtal_hz;

            if (s->sockfd != DSD_INVALID_SOCKET) {
                dsd_socket_shutdown(s->sockfd, SHUT_RDWR);
                dsd_socket_close(s->sockfd);
                s->sockfd = DSD_INVALID_SOCKET;
            }
            /* Backoff loop */
            int attempt = 0;
            while (s->run.load() && exitflag == 0) {
                attempt++;
                dsd_socket_t newsfd = tcp_connect_host(s->host, s->port);
                if (newsfd != DSD_INVALID_SOCKET) {
                    s->sockfd = newsfd;
                    fprintf(stderr, "rtl_tcp: reconnected on attempt %d.\n", attempt);
                    /* Reinitialize stream framing and pending state */
                    rtl_tcp_skip_header(s->sockfd);
                    s->tcp_pending_len = 0;
                    /* Reapply socket options: RCVBUF/NODELAY/RCVTIMEO */
                    {
                        const dsdneoRuntimeConfig* cfg2 = dsd_neo_get_config();
                        int rcvbuf = cfg2 ? cfg2->tcp_rcvbuf_bytes : (4 * 1024 * 1024);
                        (void)dsd_socket_setsockopt(s->sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
                        int nodelay = 1;
                        (void)dsd_socket_setsockopt(s->sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
                        int to_ms = cfg2 ? cfg2->tcp_rcvtimeo_ms : 2000;
                        (void)dsd_socket_set_recv_timeout(s->sockfd, (unsigned int)to_ms);
                    }
                    /* Replay essential device state to server */
                    if (prev_freq > 0) {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x01, prev_freq);
                    }
                    if (prev_rate > 0) {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x02, prev_rate);
                    }
                    if (prev_agc) {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x03, 0); /* tuner auto */
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x08, (uint32_t)env_agc_want());
                    } else {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x03, 1);
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x04, (uint32_t)prev_gain);
                    }
                    if (prev_ppm != 0) {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x05, (uint32_t)prev_ppm);
                    }
                    if (prev_direct) {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x09, (uint32_t)prev_direct);
                    }
                    if (prev_offset) {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x0A, 1);
                    }
                    if (prev_bias) {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x0E, 1);
                    }
                    if (prev_testmode) {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x07, (uint32_t)prev_testmode);
                    }
                    if (prev_rtl_xtal > 0) {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x0B, prev_rtl_xtal);
                    }
                    if (prev_tuner_xtal > 0) {
                        (void)rtl_tcp_send_cmd(s->sockfd, 0x0C, prev_tuner_xtal);
                    }
                    if (s->if_gain_count > 0) {
                        for (int i = 0; i < s->if_gain_count && i < 16; i++) {
                            uint32_t packed = ((uint32_t)(s->if_gains[i].stage & 0xFFFF) << 16)
                                              | ((uint16_t)(s->if_gains[i].gain & 0xFFFF));
                            (void)rtl_tcp_send_cmd(s->sockfd, 0x06, packed);
                        }
                    }
                    /* Resume recv loop */
                    r = dsd_socket_recv(s->sockfd, u8, BUFSZ, waitall ? MSG_WAITALL : 0);
                    if (r > 0) {
                        /* Continue with normal processing */
                        break;
                    }
                    /* Immediate failure: close and retry */
                    dsd_socket_shutdown(s->sockfd, SHUT_RDWR);
                    dsd_socket_close(s->sockfd);
                    s->sockfd = DSD_INVALID_SOCKET;
                }
                int backoff_ms = 200 * (attempt < 10 ? attempt : 10); /* up to ~2s */
                dsd_sleep_ms((unsigned int)backoff_ms);
            }
            if (s->sockfd == DSD_INVALID_SOCKET || r <= 0) {
                /* Could not reconnect or no data after reconnect; exit */
                break;
            }
        }
        /* Successful read: reset timeout counter */
        consec_timeouts = 0;
        uint32_t len = (uint32_t)r;
        /* Handle muting: discard muted samples for rtl_tcp backend.
         * Same logic as USB callback - skip samples entirely instead of processing. */
        if (s->mute.load(std::memory_order_relaxed) > 0) {
            int old = s->mute.load(std::memory_order_relaxed);
            if (old > 0) {
                uint32_t m = (uint32_t)old;
                if (m >= len) {
                    /* Entire buffer is muted - discard all */
                    s->mute.fetch_sub((int)len, std::memory_order_relaxed);
                    continue; /* Skip processing, get next recv */
                }
                /* Partial mute: skip first m bytes, process remainder */
                memmove(u8, u8 + m, len - m);
                len -= m;
                s->mute.fetch_sub((int)m, std::memory_order_relaxed);
            }
        }
        /* Stats: bytes in */
        if (s->stats_enabled) {
            s->tcp_bytes_total += (uint64_t)len;
            s->tcp_bytes_window += (uint64_t)len;
        }
        /* Reassemble into uniform slices matching device buf_len to stabilize cadence */
        int use_two_pass = (!s->offset_tuning && !s->combine_rotate_enabled);
        const size_t SLICE = (s->buf_len > 0 ? (size_t)s->buf_len : 16384);

        /* Fill pending if it exists to complete one slice */
        size_t consumed = 0;
        if (s->tcp_pending_len > 0) {
            size_t missing = (SLICE > s->tcp_pending_len) ? (SLICE - s->tcp_pending_len) : 0;
            size_t take = (missing < len) ? missing : len;
            if (take > 0) {
                if (!s->tcp_pending || s->tcp_pending_cap < SLICE) {
                    size_t cap = (SLICE + 4095) & ~((size_t)4095);
                    unsigned char* nb = (unsigned char*)realloc(s->tcp_pending, cap);
                    if (nb) {
                        s->tcp_pending = nb;
                        s->tcp_pending_cap = cap;
                    }
                }
                if (s->tcp_pending && s->tcp_pending_cap >= (s->tcp_pending_len + take)) {
                    memcpy(s->tcp_pending + s->tcp_pending_len, u8, take);
                    s->tcp_pending_len += take;
                    consumed += take;
                }
            }
            if (s->tcp_pending_len == SLICE) {
                unsigned char* src = s->tcp_pending;
                if (use_two_pass) {
                    rotate_90(src, (uint32_t)SLICE);
                }
                float *p1 = NULL, *p2 = NULL;
                size_t n1 = 0, n2 = 0;
                input_ring_reserve(s->input_ring, SLICE, &p1, &n1, &p2, &n2);
                if (n1 == 0 && n2 == 0) {
                    if (s->input_ring) {
                        s->input_ring->producer_drops.fetch_add((uint64_t)SLICE);
                    }
                    s->reserve_full_events++;
                } else {
                    if (n1 & 1) {
                        n1--;
                    }
                    size_t w1 = (n1 < SLICE) ? n1 : SLICE;
                    size_t rem_after_w1 = SLICE - w1;
                    if (n2 & 1) {
                        n2--;
                    }
                    size_t w2 = (n2 < rem_after_w1) ? n2 : rem_after_w1;
                    if (!s->offset_tuning && s->combine_rotate_enabled) {
                        if (w1) {
                            widen_rotate90_u8_to_f32_bias127(src, p1, (uint32_t)w1);
                        }
                        if (w2) {
                            widen_rotate90_u8_to_f32_bias127(src + w1, p2, (uint32_t)w2);
                        }
                    } else if (use_two_pass) {
                        if (w1) {
                            widen_u8_to_f32_bias128_scalar(src, p1, (uint32_t)w1);
                        }
                        if (w2) {
                            widen_u8_to_f32_bias128_scalar(src + w1, p2, (uint32_t)w2);
                        }
                    } else {
                        if (w1) {
                            widen_u8_to_f32_bias127(src, p1, (uint32_t)w1);
                        }
                        if (w2) {
                            widen_u8_to_f32_bias127(src + w1, p2, (uint32_t)w2);
                        }
                    }
                    input_ring_commit(s->input_ring, w1 + w2);
                }
                s->tcp_pending_len = 0;
            }
        }

        /* Process full slices directly from current buffer */
        while ((len - consumed) >= SLICE) {
            unsigned char* src = u8 + consumed;
            if (use_two_pass) {
                rotate_90(src, (uint32_t)SLICE);
            }
            float *p1 = NULL, *p2 = NULL;
            size_t n1 = 0, n2 = 0;
            input_ring_reserve(s->input_ring, SLICE, &p1, &n1, &p2, &n2);
            if (n1 == 0 && n2 == 0) {
                if (s->input_ring) {
                    s->input_ring->producer_drops.fetch_add((uint64_t)SLICE);
                }
                s->reserve_full_events++;
                break;
            }
            if (n1 & 1) {
                n1--;
            }
            size_t w1 = (n1 < SLICE) ? n1 : SLICE;
            size_t rem_after_w1 = SLICE - w1;
            if (n2 & 1) {
                n2--;
            }
            size_t w2 = (n2 < rem_after_w1) ? n2 : rem_after_w1;
            if (!s->offset_tuning && s->combine_rotate_enabled) {
                if (w1) {
                    widen_rotate90_u8_to_f32_bias127(src, p1, (uint32_t)w1);
                }
                if (w2) {
                    widen_rotate90_u8_to_f32_bias127(src + w1, p2, (uint32_t)w2);
                }
            } else if (use_two_pass) {
                if (w1) {
                    widen_u8_to_f32_bias128_scalar(src, p1, (uint32_t)w1);
                }
                if (w2) {
                    widen_u8_to_f32_bias128_scalar(src + w1, p2, (uint32_t)w2);
                }
            } else {
                if (w1) {
                    widen_u8_to_f32_bias127(src, p1, (uint32_t)w1);
                }
                if (w2) {
                    widen_u8_to_f32_bias127(src + w1, p2, (uint32_t)w2);
                }
            }
            input_ring_commit(s->input_ring, w1 + w2);
            consumed += SLICE;
        }

        /* Save remainder (<SLICE) into pending */
        size_t rem = len - consumed;
        if (rem > 0) {
            if (!s->tcp_pending || s->tcp_pending_cap < rem) {
                size_t cap = (rem + 4095) & ~((size_t)4095);
                unsigned char* nb = (unsigned char*)realloc(s->tcp_pending, cap);
                if (nb) {
                    s->tcp_pending = nb;
                    s->tcp_pending_cap = cap;
                } else {
                    rem = 0;
                }
            }
            if (rem > 0) {
                memcpy(s->tcp_pending, u8 + consumed, rem);
                s->tcp_pending_len = rem;
            }
        }

        /* Once per ~1s: optional stats print and adaptive tuning */
        {
            uint64_t now_ns = dsd_time_monotonic_ns();
            uint64_t stats_dt_ns = now_ns - s->stats_last_ns;
            if (s->stats_enabled && stats_dt_ns >= 1000000000ULL) {
                double dt = (double)stats_dt_ns / 1e9;
                double mbps = (double)s->tcp_bytes_window / dt / (1024.0 * 1024.0);
                double exp_bps = (s->rate > 0) ? (double)(s->rate * 2ULL) : 0.0;
                double exp_mbps = exp_bps / (1024.0 * 1024.0);
                uint64_t drops = s->input_ring ? s->input_ring->producer_drops.load() : 0ULL;
                uint64_t rdto = s->input_ring ? s->input_ring->read_timeouts.load() : 0ULL;
                fprintf(stderr, "rtl_tcp: %.2f MiB/s (exp %.2f), drops=%llu, res_full=%llu, read_timeouts=%llu\n", mbps,
                        exp_mbps, (unsigned long long)drops, (unsigned long long)s->reserve_full_events,
                        (unsigned long long)rdto);
                s->tcp_bytes_window = 0ULL;
                s->stats_last_ns = now_ns;
            }
            /* Adaptive block ~1s cadence */
            uint64_t auto_dt_ns = now_ns - auto_last_ns;
            autotune = s->tcp_autotune;
            if (autotune && auto_dt_ns >= 1000000000ULL) {
                uint64_t drops = s->input_ring ? s->input_ring->producer_drops.load() : 0ULL;
                uint64_t rdto = s->input_ring ? s->input_ring->read_timeouts.load() : 0ULL;
                uint64_t resf = s->reserve_full_events;
                uint64_t d_drops = (drops >= prev_drops) ? (drops - prev_drops) : 0ULL;
                uint64_t d_rdto = (rdto >= prev_rdto) ? (rdto - prev_rdto) : 0ULL;
                uint64_t d_resf = (resf >= prev_res_full) ? (resf - prev_res_full) : 0ULL;
                prev_drops = drops;
                prev_rdto = rdto;
                prev_res_full = resf;
                /* If we're overflowing frequently, shrink BUFSZ and ensure WAITALL=0 */
                if (d_drops > 0 || d_resf > 0) {
                    if (BUFSZ > 16384) {
                        BUFSZ = BUFSZ / 2;
                        if (BUFSZ < 16384) {
                            BUFSZ = 16384;
                        }
                        unsigned char* nb = (unsigned char*)realloc(u8, BUFSZ);
                        if (nb) {
                            u8 = nb;
                        }
                    }
                    waitall = 0;
                } else if (d_rdto > 5) {
                    /* Consumer is starved: shrink BUFSZ to deliver smaller, faster packets */
                    if (BUFSZ > 8192) {
                        BUFSZ = BUFSZ / 2;
                        if (BUFSZ < 8192) {
                            BUFSZ = 8192;
                        }
                        unsigned char* nb = (unsigned char*)realloc(u8, BUFSZ);
                        if (nb) {
                            u8 = nb;
                        }
                    }
                    waitall = 0;
                } else {
                    /* Quiet period: slowly grow BUFSZ up to 64 KiB for efficiency */
                    if (BUFSZ < 65536) {
                        size_t nsz = BUFSZ + (BUFSZ / 2);
                        if (nsz > 65536) {
                            nsz = 65536;
                        }
                        unsigned char* nb = (unsigned char*)realloc(u8, nsz);
                        if (nb) {
                            u8 = nb;
                            BUFSZ = nsz;
                        }
                    }
                }
                auto_last_ns = now_ns;
            }
        }
    }
    free(u8);
    s->run.store(0);
    DSD_THREAD_RETURN;
}

/**
 * @brief Find the nearest supported gain to the target gain.
 *
 * @param dev RTL-SDR device handle.
 * @param target_gain Target gain in tenths of dB.
 * @return Nearest supported gain in tenths of dB, or negative error code.
 */
static int
nearest_gain(rtlsdr_dev_t* dev, int target_gain) {
    int i, r, err1, err2, count, nearest;
    int* gains;
    r = rtlsdr_set_tuner_gain_mode(dev, 1);
    if (r < 0) {
        fprintf(stderr, "WARNING: Failed to enable manual gain.\n");
        return r;
    }
    count = rtlsdr_get_tuner_gains(dev, NULL);
    if (count <= 0) {
        return 0;
    }
    gains = static_cast<int*>(malloc(sizeof(int) * count));
    count = rtlsdr_get_tuner_gains(dev, gains);
    nearest = gains[0];
    for (i = 0; i < count; i++) {
        err1 = abs(target_gain - nearest);
        err2 = abs(target_gain - gains[i]);
        if (err2 < err1) {
            nearest = gains[i];
        }
    }
    free(gains);
    return nearest;
}

/**
 * @brief Set RTL-SDR center frequency with a brief status message.
 *
 * @param dev RTL-SDR device handle.
 * @param frequency Center frequency in Hz.
 * @return 0 on success or a negative error code.
 */
static int
verbose_set_frequency(rtlsdr_dev_t* dev, uint32_t frequency) {
    int r;
    r = rtlsdr_set_center_freq(dev, frequency);
    if (r < 0) {
        fprintf(stderr, " (WARNING: Failed to set Center Frequency). \n");
    } else {
        fprintf(stderr, " (Center Frequency: %u Hz.) \n", frequency);
    }
    return r;
}

/**
 * @brief Set RTL-SDR sampling rate with a brief status message.
 *
 * @param dev RTL-SDR device handle.
 * @param samp_rate Sampling rate in Hz.
 * @return 0 on success or a negative error code.
 */
static int
verbose_set_sample_rate(rtlsdr_dev_t* dev, uint32_t samp_rate) {
    int r;
    r = rtlsdr_set_sample_rate(dev, samp_rate);
    if (r < 0) {
        fprintf(stderr, "WARNING: Failed to set sample rate.\n");
    } else {
        fprintf(stderr, "Sampling at %u S/s.\n", samp_rate);
    }
    return r;
}

/**
 * @brief Enable or disable direct sampling mode.
 *
 * @param dev RTL-SDR device handle.
 * @param on Non-zero to enable, zero to disable.
 * @return 0 on success or a negative error code.
 */
static int
verbose_direct_sampling(rtlsdr_dev_t* dev, int on) {
    int r;
    r = rtlsdr_set_direct_sampling(dev, on);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set direct sampling mode.\n");
        return r;
    }
    if (on == 0) {
        fprintf(stderr, "Direct sampling mode disabled.\n");
    }
    if (on == 1) {
        fprintf(stderr, "Enabled direct sampling mode, input 1/I.\n");
    }
    if (on == 2) {
        fprintf(stderr, "Enabled direct sampling mode, input 2/Q.\n");
    }
    return r;
}

/**
 * @brief Print tuner type and expected hardware offset tuning support for this librtlsdr.
 *
 * Note: This is a heuristic based on tuner type. Upstream librtlsdr returns -2 for
 * R820T/R828D when enabling offset tuning. Forks may differ.
 */
void
rtl_device_print_offset_capability(struct rtl_device* dev) {
    if (!dev) {
        return;
    }
    if (dev->backend == 1) {
        fprintf(stderr,
                "rtl_tcp: offset tuning capability is determined by the server; defaulting to disabled to match USB "
                "fs/4/rotate path (override with DSD_NEO_RTL_OFFSET_TUNING=1).\n");
        return;
    }
    if (!dev->dev) {
        return;
    }
    int t = rtlsdr_get_tuner_type(dev->dev);
    const char* tt = "unknown";
    switch (t) {
        case RTLSDR_TUNER_E4000: tt = "E4000"; break;
        case RTLSDR_TUNER_FC0012: tt = "FC0012"; break;
        case RTLSDR_TUNER_FC0013: tt = "FC0013"; break;
        case RTLSDR_TUNER_FC2580: tt = "FC2580"; break;
        case RTLSDR_TUNER_R820T: tt = "R820T"; break;
        case RTLSDR_TUNER_R828D: tt = "R828D"; break;
        default: break;
    }
    int supported = 1;
    if (t == RTLSDR_TUNER_R820T || t == RTLSDR_TUNER_R828D) {
        supported = 0; /* per upstream librtlsdr */
    }
    fprintf(stderr, "RTL tuner: %s; hardware offset tuning supported by this librtlsdr: %s\n", tt,
            supported ? "yes (expected)" : "no (expected upstream)");
}

/**
 * @brief Set tuner IF bandwidth (if supported by the library/driver).
 */
static int
verbose_set_tuner_bandwidth(rtlsdr_dev_t* dev, uint32_t bw_hz) {
    /* Pass-through to librtlsdr; bw_hz == 0 lets driver choose an appropriate filter */
    int r = rtlsdr_set_tuner_bandwidth(dev, (int)bw_hz);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set tuner bandwidth to %u Hz.\n", bw_hz);
    } else {
        if (bw_hz == 0) {
            fprintf(stderr, "Tuner bandwidth set to auto (driver).\n");
        } else {
            fprintf(stderr, "Tuner bandwidth set to %u Hz.\n", bw_hz);
        }
    }
    return r;
}

/**
 * @brief Enable tuner automatic gain control.
 *
 * @param dev RTL-SDR device handle.
 * @return 0 on success or a negative error code.
 */
static int
verbose_auto_gain(rtlsdr_dev_t* dev) {
    int r;
    r = rtlsdr_set_tuner_gain_mode(dev, 0);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
    } else {
        fprintf(stderr, "Tuner gain set to automatic.\n");
    }
    /* Original plan: enable RTL digital AGC in auto mode by default.
       Allow override via env DSD_NEO_RTL_AGC=0 to disable. */
    int want = env_agc_want();
    int ra = rtlsdr_set_agc_mode(dev, want);
    if (ra != 0) {
        fprintf(stderr, "WARNING: Failed to %s RTL AGC.\n", want ? "enable" : "disable");
    } else {
        fprintf(stderr, "RTL AGC %s.\n", want ? "enabled" : "disabled");
    }
    return r;
}

/**
 * @brief Set a fixed tuner gain with a message indicating the result.
 *
 * @param dev RTL-SDR device handle.
 * @param gain Desired gain in tenths of dB.
 * @return 0 on success or a negative error code.
 */
static int
verbose_gain_set(rtlsdr_dev_t* dev, int gain) {
    int r;
    /* Disable RTL digital AGC when setting manual tuner gain */
    (void)rtlsdr_set_agc_mode(dev, 0);
    r = rtlsdr_set_tuner_gain_mode(dev, 1);
    if (r < 0) {
        fprintf(stderr, "WARNING: Failed to enable manual gain.\n");
        return r;
    }
    r = rtlsdr_set_tuner_gain(dev, gain);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
    } else {
        fprintf(stderr, "Tuner gain set to %0.2f dB.\n", gain / 10.0);
    }
    return r;
}

/**
 * @brief Set tuner PPM frequency error correction.
 *
 * @param dev RTL-SDR device handle.
 * @param ppm_error Error in parts-per-million.
 * @return 0 on success or a negative error code.
 */
static int
verbose_ppm_set(rtlsdr_dev_t* dev, int ppm_error) {
    int r;
    r = rtlsdr_set_freq_correction(dev, ppm_error);
    if (r < 0) {
        fprintf(stderr, "WARNING: Failed to set ppm error.\n");
    } else {
        fprintf(stderr, "Tuner error set to %i ppm.\n", ppm_error);
    }
    return r;
}

/**
 * @brief Reset RTL-SDR USB buffers.
 *
 * @param dev RTL-SDR device handle.
 * @return 0 on success or a negative error code.
 */
static int
verbose_reset_buffer(rtlsdr_dev_t* dev) {
    int r;
    r = rtlsdr_reset_buffer(dev);
    if (r < 0) {
        fprintf(stderr, "WARNING: Failed to reset buffers.\n");
    }
    return r;
}

// Public API Implementation

/**
 * @brief Create and initialize an RTL-SDR device.
 *
 * @param dev_index Device index to open.
 * @param input_ring Pointer to input ring for USB data.
 * @param combine_rotate_enabled_param Whether to use combined rotate+widen when offset tuning is disabled.
 * @return Pointer to rtl_device handle, or NULL on failure.
 */
struct rtl_device*
rtl_device_create(int dev_index, struct input_ring_state* input_ring, int combine_rotate_enabled_param) {
    if (!input_ring) {
        return NULL;
    }

    struct rtl_device* dev = static_cast<rtl_device*>(calloc(1, sizeof(struct rtl_device)));
    if (!dev) {
        return NULL;
    }

    dev->dev_index = dev_index;
    dev->input_ring = input_ring;
    dev->thread_started = 0;
    dev->mute = 0;
    dev->combine_rotate_enabled = combine_rotate_enabled_param;
    dev->backend = 0;
    dev->sockfd = DSD_INVALID_SOCKET;
    dev->host[0] = '\0';
    dev->port = 0;
    dev->run.store(0);
    dev->agc_mode = 1;
    dev->testmode_on = 0;
    dev->rtl_xtal_hz = 0;
    dev->tuner_xtal_hz = 0;
    dev->if_gain_count = 0;

    int r = 0;
#if defined(_MSC_VER) && DSD_PLATFORM_WIN_NATIVE
    __try {
        r = rtlsdr_open(&dev->dev, (uint32_t)dev_index);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr,
                "ERROR: libusb exception in rtlsdr_open (MSVC/Windows). "
                "Check that the bundled libusb/librtlsdr DLLs match the build and the device driver is installed.\n");
        r = -1;
    }
#else
    r = rtlsdr_open(&dev->dev, (uint32_t)dev_index);
#endif
    if (r < 0) {
        fprintf(stderr, "Failed to open rtlsdr device %d.\n", dev_index);
        free(dev);
        return NULL;
    }

    return dev;
}

struct rtl_device*
rtl_device_create_tcp(const char* host, int port, struct input_ring_state* input_ring, int combine_rotate_enabled_param,
                      int autotune_enabled) {
    if (!input_ring || !host || port <= 0) {
        return NULL;
    }
    struct rtl_device* dev = static_cast<rtl_device*>(calloc(1, sizeof(struct rtl_device)));
    if (!dev) {
        return NULL;
    }
    dev->dev = NULL;
    dev->dev_index = -1;
    dev->input_ring = input_ring;
    dev->thread_started = 0;
    dev->mute = 0;
    dev->combine_rotate_enabled = combine_rotate_enabled_param;
    dev->backend = 1;
    dev->sockfd = DSD_INVALID_SOCKET;
    snprintf(dev->host, sizeof(dev->host), "%s", host);
    dev->port = port;
    dev->run.store(0);
    dev->agc_mode = 1;
    dev->tcp_autotune = autotune_enabled ? 1 : 0;
    dev->offset_tuning = 0;
    dev->tcp_pending = NULL;
    dev->tcp_pending_len = 0;
    dev->tcp_pending_cap = 0;
    dev->testmode_on = 0;
    dev->rtl_xtal_hz = 0;
    dev->tuner_xtal_hz = 0;
    dev->if_gain_count = 0;

    dsd_socket_t sfd = tcp_connect_host(host, port);
    if (sfd == DSD_INVALID_SOCKET) {
        free(dev);
        return NULL;
    }
    /* Increase socket receive buffer to tolerate brief processing stalls */
    {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        int rcvbuf = cfg ? cfg->tcp_rcvbuf_bytes : (4 * 1024 * 1024);
        (void)dsd_socket_setsockopt(sfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
        int nodelay = 1;
        (void)dsd_socket_setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        /* Hard fix: apply a receive timeout so stalled connections don't appear
           as a P25 wedge. Default 2 seconds; override via DSD_NEO_TCP_RCVTIMEO (ms). */
        int to_ms = cfg ? cfg->tcp_rcvtimeo_ms : 2000;
        (void)dsd_socket_set_recv_timeout(sfd, (unsigned int)to_ms);
    }
    dev->sockfd = sfd;
    fprintf(stderr, "rtl_tcp: Connected to %s:%d\n", host, port);
    /* Optional TCP stats: enable with DSD_NEO_TCP_STATS=1 */
    {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        if (cfg && cfg->tcp_stats_enable) {
            dev->stats_enabled = 1;
            dev->stats_last_ns = dsd_time_monotonic_ns();
            fprintf(stderr, "rtl_tcp: stats enabled.\n");
        }
    }
    /* Initialize autotune from env if not already enabled by caller */
    if (!dev->tcp_autotune) {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        if (cfg && cfg->tcp_autotune_enable) {
            dev->tcp_autotune = 1;
        }
    }
    return dev;
}

/**
 * @brief Destroy an RTL-SDR device and free resources.
 *
 * @param dev Pointer to rtl_device handle.
 */
void
rtl_device_destroy(struct rtl_device* dev) {
    if (!dev) {
        return;
    }

    if (dev->thread_started) {
        /* Ensure async read is cancelled before joining to avoid blocking */
        if (dev->backend == 0) {
            if (dev->dev) {
                rtlsdr_cancel_async(dev->dev);
            }
        } else if (dev->backend == 1) {
            dev->run.store(0);
            if (dev->sockfd != DSD_INVALID_SOCKET) {
                dsd_socket_shutdown(dev->sockfd, SHUT_RDWR);
            }
        }
        dsd_thread_join(dev->thread);
        dev->thread_started = 0;
    }

    /* Best-effort device state cleanup before closing the USB handle. */
    if (dev->backend == 0 && dev->dev) {
        /* Disable bias tee so subsequent runs don't inherit stale 5V state. */
#ifdef USE_RTLSDR_BIAS_TEE
        (void)rtlsdr_set_bias_tee(dev->dev, 0);
#endif
        /* Reset buffers after cancel to leave device in a clean state. */
        (void)rtlsdr_reset_buffer(dev->dev);
    }

    if (dev->backend == 0 && dev->dev) {
        rtlsdr_close(dev->dev);
    }
    if (dev->backend == 1 && dev->sockfd != DSD_INVALID_SOCKET) {
        dsd_socket_close(dev->sockfd);
    }
    if (dev->tcp_pending) {
        free(dev->tcp_pending);
        dev->tcp_pending = NULL;
        dev->tcp_pending_len = 0;
        dev->tcp_pending_cap = 0;
    }

    free(dev);
}

/**
 * @brief Set device center frequency.
 *
 * @param dev RTL-SDR device handle.
 * @param frequency Frequency in Hz.
 * @return 0 on success, negative on failure.
 */
int
rtl_device_set_frequency(struct rtl_device* dev, uint32_t frequency) {
    if (!dev) {
        return -1;
    }
    dev->freq = frequency;
    if (dev->backend == 0) {
        if (!dev->dev) {
            return -1;
        }
        return verbose_set_frequency(dev->dev, frequency);
    } else {
        return rtl_tcp_send_cmd(dev->sockfd, 0x01, frequency);
    }
}

/**
 * @brief Set device sample rate.
 *
 * @param dev RTL-SDR device handle.
 * @param samp_rate Sample rate in Hz.
 * @return 0 on success, negative on failure.
 */
int
rtl_device_set_sample_rate(struct rtl_device* dev, uint32_t samp_rate) {
    if (!dev) {
        return -1;
    }
    dev->rate = samp_rate;
    if (dev->backend == 0) {
        if (!dev->dev) {
            return -1;
        }
        return verbose_set_sample_rate(dev->dev, samp_rate);
    } else {
        return rtl_tcp_send_cmd(dev->sockfd, 0x02, samp_rate);
    }
}

/**
 * @brief Get current device sample rate.
 *
 * For USB, queries librtlsdr for the actual rate applied (which may be
 * quantized). For rtl_tcp, returns the last programmed value.
 */
int
rtl_device_get_sample_rate(struct rtl_device* dev) {
    if (!dev) {
        return -1;
    }
    if (dev->backend == 0) {
        if (!dev->dev) {
            return -1;
        }
        return (int)rtlsdr_get_sample_rate(dev->dev);
    }
    return (int)dev->rate;
}

/**
 * @brief Set tuner gain mode and value.
 *
 * @param dev RTL-SDR device handle.
 * @param gain Gain in tenths of dB, or AUTO_GAIN for automatic.
 * @return 0 on success, negative on failure.
 */
int
rtl_device_set_gain(struct rtl_device* dev, int gain) {
    if (!dev) {
        return -1;
    }

#define AUTO_GAIN (-100)
    dev->gain = gain;
    if (dev->backend == 0) {
        if (!dev->dev) {
            return -1;
        }
        if (gain == AUTO_GAIN) {
            return verbose_auto_gain(dev->dev);
        } else {
            int nearest = nearest_gain(dev->dev, gain);
            return verbose_gain_set(dev->dev, nearest);
        }
    } else {
        if (gain == AUTO_GAIN) {
            dev->agc_mode = 1;
            int r = rtl_tcp_send_cmd(dev->sockfd, 0x03, 0); /* tuner auto */
            if (r < 0) {
                return r;
            }
            /* Mirror USB path: set RTL2832 digital AGC according to env */
            r = rtl_tcp_send_cmd(dev->sockfd, 0x08, (uint32_t)env_agc_want());
            return r;
        } else {
            dev->agc_mode = 0;
            int r = rtl_tcp_send_cmd(dev->sockfd, 0x03, 1);
            if (r < 0) {
                return r;
            }
            return rtl_tcp_send_cmd(dev->sockfd, 0x04, (uint32_t)gain);
        }
    }
}

int
rtl_device_set_gain_nearest(struct rtl_device* dev, int target_tenth_db) {
    if (!dev) {
        return -1;
    }
    if (dev->backend == 0) {
        /* USB: find nearest supported and set manual gain */
        if (!dev->dev) {
            return -1;
        }
        int g = nearest_gain(dev->dev, target_tenth_db);
        if (g < 0) {
            return g;
        }
        int r = rtlsdr_set_tuner_gain_mode(dev->dev, 1);
        if (r < 0) {
            fprintf(stderr, "WARNING: Failed to enable manual gain.\n");
            return r;
        }
        r = rtlsdr_set_tuner_gain(dev->dev, g);
        if (r < 0) {
            fprintf(stderr, "WARNING: Failed to set tuner gain (nearest).\n");
            return r;
        }
        dev->gain = g;
        fprintf(stderr, "Tuner manual gain (nearest): %0.1f dB.\n", (double)g / 10.0);
        return 0;
    }
    /* rtl_tcp: request manual mode and set target directly */
    int mode = 1;
    (void)rtl_tcp_send_cmd(dev->sockfd, 0x03, (uint32_t)mode);
    (void)rtl_tcp_send_cmd(dev->sockfd, 0x04, (uint32_t)target_tenth_db);
    dev->gain = target_tenth_db;
    return 0;
}

int
rtl_device_get_tuner_gain(struct rtl_device* dev) {
    if (!dev) {
        return -1;
    }
    if (dev->backend == 0) {
        if (!dev->dev) {
            return -1;
        }
        return rtlsdr_get_tuner_gain(dev->dev);
    }
    if (dev->agc_mode) {
        return 0;
    }
    return dev->gain;
}

int
rtl_device_is_auto_gain(struct rtl_device* dev) {
    if (!dev) {
        return -1;
    }
    if (dev->backend == 0) {
        /* We track AUTO vs manual in the requested field. */
        return (dev->gain == AUTO_GAIN) ? 1 : 0;
    } else {
        return dev->agc_mode ? 1 : 0;
    }
}

/**
 * @brief Set frequency correction (PPM error).
 *
 * @param dev RTL-SDR device handle.
 * @param ppm_error PPM correction value.
 * @return 0 on success, negative on failure.
 */
int
rtl_device_set_ppm(struct rtl_device* dev, int ppm_error) {
    if (!dev) {
        return -1;
    }
    /* Avoid redundant writes: if requested PPM equals current cached value,
       skip making a driver call. This prevents a spurious warning on startup
       when no PPM is provided (defaults to 0) or when re-applying zero. */
    if (ppm_error == dev->ppm_error) {
        return 0;
    }
    dev->ppm_error = ppm_error;
    if (dev->backend == 0) {
        if (!dev->dev) {
            return -1;
        }
        return verbose_ppm_set(dev->dev, ppm_error);
    } else {
        return rtl_tcp_send_cmd(dev->sockfd, 0x05, (uint32_t)ppm_error);
    }
}

/**
 * @brief Set direct sampling mode.
 *
 * @param dev RTL-SDR device handle.
 * @param on 1 to enable, 0 to disable.
 * @return 0 on success, negative on failure.
 */
int
rtl_device_set_direct_sampling(struct rtl_device* dev, int on) {
    if (!dev) {
        return -1;
    }
    dev->direct_sampling = on;
    if (dev->backend == 0) {
        if (!dev->dev) {
            return -1;
        }
        return verbose_direct_sampling(dev->dev, on);
    } else {
        return rtl_tcp_send_cmd(dev->sockfd, 0x09, (uint32_t)on);
    }
}

/**
 * @brief Enable or disable offset tuning mode.
 */
int
rtl_device_set_offset_tuning_enabled(struct rtl_device* dev, int on) {
    if (!dev) {
        return -1;
    }
    int r = 0;
    if (dev->backend == 0) {
        if (!dev->dev) {
            return -1;
        }
        r = rtlsdr_set_offset_tuning(dev->dev, on ? 1 : 0);
        if (r == 0) {
            fprintf(stderr, on ? "Offset tuning mode enabled.\n" : "Offset tuning mode disabled.\n");
        } else {
            int t = rtlsdr_get_tuner_type(dev->dev);
            const char* tt = "unknown";
            switch (t) {
                case RTLSDR_TUNER_E4000: tt = "E4000"; break;
                case RTLSDR_TUNER_FC0012: tt = "FC0012"; break;
                case RTLSDR_TUNER_FC0013: tt = "FC0013"; break;
                case RTLSDR_TUNER_FC2580: tt = "FC2580"; break;
                case RTLSDR_TUNER_R820T: tt = "R820T"; break;
                case RTLSDR_TUNER_R828D: tt = "R828D"; break;
                default: break;
            }
            fprintf(stderr, "WARNING: Failed to set offset tuning (%d) for tuner %s.\n", r, tt);
        }
    } else {
        r = rtl_tcp_send_cmd(dev->sockfd, 0x0A, (uint32_t)(on ? 1 : 0));
    }
    if (r == 0) {
        dev->offset_tuning = on ? 1 : 0;
    }
    return r;
}

int
rtl_device_set_tuner_bandwidth(struct rtl_device* dev, uint32_t bw_hz) {
    if (!dev) {
        return -1;
    }
    if (dev->backend == 0) {
        if (!dev->dev) {
            return -1;
        }
        return verbose_set_tuner_bandwidth(dev->dev, bw_hz);
    } else {
        /* Not universally supported by rtl_tcp; ignore */
        (void)bw_hz;
        return 0;
    }
}

/**
 * @brief Reset device buffer.
 *
 * @param dev RTL-SDR device handle.
 * @return 0 on success, negative on failure.
 */
int
rtl_device_reset_buffer(struct rtl_device* dev) {
    if (!dev) {
        return -1;
    }
    if (dev->backend == 0) {
        if (!dev->dev) {
            return -1;
        }
        return verbose_reset_buffer(dev->dev);
    } else {
        /* No explicit reset; treat as success */
        return 0;
    }
}

/**
 * @brief Start asynchronous reading from the device.
 *
 * @param dev RTL-SDR device handle.
 * @param buf_len Buffer length for async read.
 * @return 0 on success, negative on failure.
 */
int
rtl_device_start_async(struct rtl_device* dev, uint32_t buf_len) {
    if (!dev || dev->thread_started) {
        return -1;
    }
    dev->buf_len = buf_len;
    dev->thread_started = 1;
    int r = 0;
    if (dev->backend == 0) {
        if (!dev->dev) {
            dev->thread_started = 0;
            return -1;
        }
        r = dsd_thread_create(&dev->thread, (dsd_thread_fn)dongle_thread_fn, dev);
    } else {
        dev->run.store(1);
        r = dsd_thread_create(&dev->thread, (dsd_thread_fn)tcp_thread_fn, dev);
    }
    if (r != 0) {
        dev->thread_started = 0;
        return -1;
    }
    return 0;
}

/**
 * @brief Stop asynchronous reading and join the device thread.
 *
 * @param dev RTL-SDR device handle.
 * @return 0 on success, negative on failure.
 */
int
rtl_device_stop_async(struct rtl_device* dev) {
    if (!dev || !dev->thread_started) {
        return -1;
    }
    if (dev->backend == 0) {
        if (dev->dev) {
            rtlsdr_cancel_async(dev->dev);
        }
    } else {
        dev->run.store(0);
        if (dev->sockfd != DSD_INVALID_SOCKET) {
            dsd_socket_shutdown(dev->sockfd, SHUT_RDWR);
        }
    }
    dsd_thread_join(dev->thread);
    dev->thread_started = 0;
    return 0;
}

/**
 * @brief Mute the incoming raw input stream for a specified number of bytes.
 *
 * @param dev RTL-SDR device handle.
 * @param bytes Number of input bytes to replace with 0x7F.
 */
void
rtl_device_mute(struct rtl_device* dev, int bytes) {
    if (!dev) {
        return;
    }
    dev->mute.store(bytes);
}

int
rtl_device_set_bias_tee(struct rtl_device* dev, int on) {
    if (!dev) {
        return -1;
    }
    dev->bias_tee_on = on ? 1 : 0;
    if (dev->backend == 1) {
        /* rtl_tcp protocol command 0x0E toggles bias tee */
        return rtl_tcp_send_cmd(dev->sockfd, 0x0E, (uint32_t)dev->bias_tee_on);
    }
#ifdef USE_RTLSDR_BIAS_TEE
    if (!dev->dev) {
        return -1;
    }
    int r = rtlsdr_set_bias_tee(dev->dev, dev->bias_tee_on);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to %sable RTL-SDR bias tee.\n", dev->bias_tee_on ? "en" : "dis");
        return -1;
    }
    fprintf(stderr, "RTL-SDR bias tee %s.\n", dev->bias_tee_on ? "enabled" : "disabled");
    return 0;
#else
    (void)on;
    fprintf(stderr, "NOTE: librtlsdr built without bias tee API; ignoring bias setting on USB.\n");
    return 0;
#endif
}

int
rtl_device_set_tcp_autotune(struct rtl_device* dev, int onoff) {
    if (!dev) {
        return -1;
    }
    if (dev->backend != 1) {
        return 0; /* not applicable for USB */
    }
    dev->tcp_autotune = onoff ? 1 : 0;
    return 0;
}

int
rtl_device_get_tcp_autotune(struct rtl_device* dev) {
    if (!dev) {
        return 0;
    }
    if (dev->backend != 1) {
        return 0;
    }
    return dev->tcp_autotune ? 1 : 0;
}

int
rtl_device_set_xtal_freq(struct rtl_device* dev, uint32_t rtl_xtal_hz, uint32_t tuner_xtal_hz) {
    if (!dev) {
        return -1;
    }
    dev->rtl_xtal_hz = rtl_xtal_hz;
    dev->tuner_xtal_hz = tuner_xtal_hz;
    if (dev->backend == 1) {
        if (dev->sockfd == DSD_INVALID_SOCKET) {
            return -1;
        }
        if (rtl_xtal_hz > 0) {
            (void)rtl_tcp_send_cmd(dev->sockfd, 0x0B, rtl_xtal_hz);
        }
        if (tuner_xtal_hz > 0) {
            (void)rtl_tcp_send_cmd(dev->sockfd, 0x0C, tuner_xtal_hz);
        }
        return 0;
    }
    if (!dev->dev) {
        return -1;
    }
    int r = rtlsdr_set_xtal_freq(dev->dev, rtl_xtal_hz, tuner_xtal_hz);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set xtal freq (rtl=%u, tuner=%u).\n", rtl_xtal_hz, tuner_xtal_hz);
        return -1;
    }
    fprintf(stderr, "Set xtal freq: rtl=%u Hz%s, tuner=%u Hz%s.\n", rtl_xtal_hz, rtl_xtal_hz ? "" : " (unchanged)",
            tuner_xtal_hz, tuner_xtal_hz ? "" : " (unchanged)");
    return 0;
}

int
rtl_device_set_testmode(struct rtl_device* dev, int on) {
    if (!dev) {
        return -1;
    }
    dev->testmode_on = on ? 1 : 0;
    if (dev->backend == 1) {
        if (dev->sockfd == DSD_INVALID_SOCKET) {
            return -1;
        }
        return rtl_tcp_send_cmd(dev->sockfd, 0x07, (uint32_t)(on ? 1 : 0));
    }
    if (!dev->dev) {
        return -1;
    }
    int r = rtlsdr_set_testmode(dev->dev, on ? 1 : 0);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to %s RTL-SDR test mode.\n", on ? "enable" : "disable");
        return -1;
    }
    fprintf(stderr, "RTL-SDR test mode %s.\n", on ? "enabled" : "disabled");
    return 0;
}

int
rtl_device_set_if_gain(struct rtl_device* dev, int stage, int gain_tenth_db) {
    if (!dev) {
        return -1;
    }
    if (stage < 0) {
        return -1;
    }
    int replaced = 0;
    for (int i = 0; i < dev->if_gain_count && i < 16; i++) {
        if (dev->if_gains[i].stage == stage) {
            dev->if_gains[i].gain = gain_tenth_db;
            replaced = 1;
            break;
        }
    }
    if (!replaced && dev->if_gain_count < 16) {
        dev->if_gains[dev->if_gain_count].stage = stage;
        dev->if_gains[dev->if_gain_count].gain = gain_tenth_db;
        dev->if_gain_count++;
    }
    if (dev->backend == 1) {
        if (dev->sockfd == DSD_INVALID_SOCKET) {
            return -1;
        }
        uint32_t packed = ((uint32_t)(stage & 0xFFFF) << 16) | ((uint16_t)(gain_tenth_db & 0xFFFF));
        return rtl_tcp_send_cmd(dev->sockfd, 0x06, packed);
    }
    if (!dev->dev) {
        return -1;
    }
    int r = rtlsdr_set_tuner_if_gain(dev->dev, stage, (int16_t)gain_tenth_db);
    if (r != 0) {
        fprintf(stderr, "WARNING: Failed to set IF gain: stage=%d, gain=%d (0.1 dB).\n", stage, gain_tenth_db);
        return -1;
    }
    fprintf(stderr, "IF gain set: stage=%d, gain=%0.1f dB.\n", stage, gain_tenth_db / 10.0);
    return 0;
}
