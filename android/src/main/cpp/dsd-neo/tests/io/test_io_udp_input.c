// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Regression test: UDP PCM16LE input must be sample-accurate and must not
 * synthesize samples when idle (it should block until data arrives).
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/io/udp_input.h>
#include <dsd-neo/platform/sockets.h>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/platform/timing.h>
#include <dsd-neo/runtime/exitflag.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int
get_bound_port(dsd_socket_t sock) {
    struct sockaddr_in sa;
    socklen_t slen = (socklen_t)sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    if (getsockname(sock, (struct sockaddr*)&sa, &slen) != 0) {
        return -1;
    }
    if (sa.sin_family != AF_INET) {
        return -1;
    }
    return (int)ntohs(sa.sin_port);
}

static int
send_pcm16le(dsd_socket_t sock, const char* host, int port, const int16_t* samples, size_t nsamp) {
    uint8_t buf[2048];
    if (nsamp * 2 > sizeof(buf)) {
        return -1;
    }
    for (size_t i = 0; i < nsamp; i++) {
        uint16_t u = (uint16_t)samples[i];
        buf[i * 2 + 0] = (uint8_t)(u & 0xFFu);
        buf[i * 2 + 1] = (uint8_t)((u >> 8) & 0xFFu);
    }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    if (dsd_socket_resolve(host, port, &dst) != 0) {
        return -1;
    }
    int n = dsd_socket_sendto(sock, buf, nsamp * 2, 0, (struct sockaddr*)&dst, (int)sizeof(dst));
    return (n == (int)(nsamp * 2)) ? 0 : -1;
}

typedef struct reader_state {
    dsd_opts* opts;
    dsd_mutex_t mu;
    dsd_cond_t cv;
    int done;
    int ok;
    int16_t sample;
} reader_state;

static DSD_THREAD_RETURN_TYPE
#if DSD_PLATFORM_WIN_NATIVE
    __stdcall
#endif
    reader_thread(void* arg) {
    reader_state* rs = (reader_state*)arg;
    int16_t s = 0;
    int ok = udp_input_read_sample(rs->opts, &s);

    dsd_mutex_lock(&rs->mu);
    rs->done = 1;
    rs->ok = ok;
    rs->sample = s;
    dsd_cond_signal(&rs->cv);
    dsd_mutex_unlock(&rs->mu);

    DSD_THREAD_RETURN;
}

static int
wait_done(reader_state* rs, unsigned int timeout_ms) {
    int ret = 0;
    dsd_mutex_lock(&rs->mu);
    while (!rs->done && ret != ETIMEDOUT) {
        ret = dsd_cond_timedwait(&rs->cv, &rs->mu, timeout_ms);
    }
    int done = rs->done;
    dsd_mutex_unlock(&rs->mu);
    return done;
}

int
main(void) {
    exitflag = 0;
    if (dsd_socket_init() != 0) {
        fprintf(stderr, "dsd_socket_init failed\n");
        return 1;
    }

    int rc = 1;
    int started = 0;
    dsd_socket_t tx = DSD_INVALID_SOCKET;
    dsd_thread_t th = (dsd_thread_t)0;
    int th_started = 0;
    int rs_inited = 0;

    dsd_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.wav_sample_rate = 48000;

    if (udp_input_start(&opts, "127.0.0.1", 0, opts.wav_sample_rate) != 0) {
        fprintf(stderr, "udp_input_start failed\n");
        goto cleanup;
    }
    started = 1;

    int port = get_bound_port(opts.udp_in_sockfd);
    if (port <= 0) {
        fprintf(stderr, "failed to determine bound UDP port\n");
        goto cleanup;
    }

    tx = dsd_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tx == DSD_INVALID_SOCKET) {
        fprintf(stderr, "failed to create UDP sender socket\n");
        goto cleanup;
    }

    const int16_t v[] = {0, 1, -1, 32767, (int16_t)0x8000, 1234, -1234, 2222, -2222};
    if (send_pcm16le(tx, "127.0.0.1", port, v, sizeof(v) / sizeof(v[0])) != 0) {
        fprintf(stderr, "failed to send initial UDP PCM\n");
        goto cleanup;
    }

    for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
        int16_t out = 0;
        if (!udp_input_read_sample(&opts, &out)) {
            fprintf(stderr, "udp_input_read_sample returned shutdown unexpectedly\n");
            goto cleanup;
        }
        if (out != v[i]) {
            fprintf(stderr, "sample mismatch at %zu: got %d expected %d\n", i, (int)out, (int)v[i]);
            goto cleanup;
        }
    }

    // With no new packets, udp_input_read_sample should block (not synthesize silence).
    reader_state rs;
    memset(&rs, 0, sizeof(rs));
    rs.opts = &opts;
    dsd_mutex_init(&rs.mu);
    dsd_cond_init(&rs.cv);
    rs_inited = 1;

    if (dsd_thread_create(&th, reader_thread, &rs) != 0) {
        fprintf(stderr, "failed to create reader thread\n");
        goto cleanup;
    }
    th_started = 1;

    if (wait_done(&rs, 50)) {
        fprintf(stderr, "udp_input_read_sample returned without data (should block)\n");
        goto cleanup;
    }

    const int16_t last = (int16_t)0x1357;
    if (send_pcm16le(tx, "127.0.0.1", port, &last, 1) != 0) {
        fprintf(stderr, "failed to send unblock sample\n");
        goto cleanup;
    }

    if (!wait_done(&rs, 500)) {
        fprintf(stderr, "reader did not unblock after data arrival\n");
        goto cleanup;
    }

    if (!rs.ok || rs.sample != last) {
        fprintf(stderr, "unblock sample mismatch: ok=%d got=%d expected=%d\n", rs.ok, (int)rs.sample, (int)last);
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (th_started) {
        exitflag = 1;
        (void)dsd_thread_join(th);
    }
    if (rs_inited) {
        dsd_cond_destroy(&rs.cv);
        dsd_mutex_destroy(&rs.mu);
    }
    if (tx != DSD_INVALID_SOCKET) {
        dsd_socket_close(tx);
    }
    if (started) {
        udp_input_stop(&opts);
    }
    dsd_socket_cleanup();
    return rc;
}
