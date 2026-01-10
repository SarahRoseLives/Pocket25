// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Input ring buffer implementation for interleaved I/Q float samples.
 *
 * Provides producer/consumer primitives to reserve, commit, write, and
 * blockingly read samples with wrap-around handling and wakeup signaling.
 */
#include <cstring>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/runtime/input_ring.h>
#include <errno.h>

extern "C" volatile uint8_t exitflag; // defined in src/runtime/exitflag.c
#ifdef USE_RTLSDR
extern "C" int dsd_rtl_stream_should_exit(void);
#endif

/**
 * @brief Reserve writable regions in the input ring buffer.
 *
 * @param r          Input ring buffer state.
 * @param min_needed Minimum number of samples needed.
 * @param p1         [out] First writable region pointer.
 * @param n1         [out] First writable region length.
 * @param p2         [out] Second writable region pointer or NULL.
 * @param n2         [out] Second writable region length.
 * @return Total writable samples granted across regions.
 */
int
input_ring_reserve(struct input_ring_state* r, size_t min_needed, float** p1, size_t* n1, float** p2, size_t* n2) {
    size_t free_sp = input_ring_free(r);
    /* Producer must never advance consumer tail; if full, grant nothing */
    /* Provide up to min(free_sp, min_needed) across at most two regions */
    size_t grant = (min_needed < free_sp) ? min_needed : free_sp;
    size_t h = r->head.load();

    *p1 = NULL;
    *n1 = 0;
    *p2 = NULL;
    *n2 = 0;

    if (grant == 0) {
        return 0;
    }

    /* First region: from head to end of buffer */
    *p1 = r->buffer + h;
    size_t to_end = r->capacity - h;
    if (to_end >= grant) {
        *n1 = grant;
        return (int)grant;
    }
    *n1 = to_end;

    /* Second region: wrap around to beginning */
    if (grant > to_end) {
        *p2 = r->buffer;
        *n2 = grant - to_end;
    }

    return (int)grant;
}

/**
 * @brief Commit previously reserved writable regions to the input ring.
 *
 * @param r         Input ring buffer state.
 * @param produced  Number of samples produced to commit.
 */
void
input_ring_commit(struct input_ring_state* r, size_t produced) {
    if (produced == 0) {
        return;
    }
    int need_signal = input_ring_is_empty(r);
    size_t h = r->head.load();
    h += produced;
    if (h >= r->capacity) {
        h -= r->capacity;
    }
    r->head.store(h);
    if (need_signal) {
        dsd_mutex_lock(&r->ready_m);
        dsd_cond_signal(&r->ready);
        dsd_mutex_unlock(&r->ready_m);
    }
}

/**
 * @brief Write samples to the input ring, blocking if necessary.
 *
 * Drops remaining samples if the ring is full to avoid racing the consumer.
 *
 * @param r     Input ring buffer state.
 * @param data  Source samples to write.
 * @param count Number of samples to write.
 */
void
input_ring_write(struct input_ring_state* r, const float* data, size_t count) {
    int need_signal = input_ring_is_empty(r);
    while (count > 0 && !exitflag) {
        size_t free_sp = input_ring_free(r);
        if (free_sp == 0) {
            /* Ring full: to avoid racing the consumer, drop remainder */
            r->producer_drops.fetch_add(count);
            break;
        }
        size_t write_now = (count < free_sp) ? count : free_sp;
        size_t h = r->head.load();

        /* First region: from head to end of buffer */
        size_t to_end = r->capacity - h;
        if (to_end >= write_now) {
            memcpy(r->buffer + h, data, write_now * sizeof(float));
            h += write_now;
            if (h >= r->capacity) {
                h = 0;
            }
            r->head.store(h);
            data += write_now;
            count -= write_now;
            continue;
        }

        /* Handle wrap-around case: split write_now across tail and head */
        if (to_end > 0) {
            memcpy(r->buffer + h, data, to_end * sizeof(float));
            data += to_end;
        }
        size_t remaining = write_now - to_end;
        if (remaining > 0) {
            memcpy(r->buffer, data, remaining * sizeof(float));
            h = remaining;
            data += remaining;
        } else {
            h = 0;
        }
        r->head.store(h);
        count -= write_now;
    }
    if (need_signal) {
        dsd_mutex_lock(&r->ready_m);
        dsd_cond_signal(&r->ready);
        dsd_mutex_unlock(&r->ready_m);
    }
}

/**
 * @brief Read up to max_count samples from the input ring, blocking until data is available.
 *
 * Returns -1 when an exit condition is observed while waiting for data.
 *
 * @param r         Input ring buffer state.
 * @param out       Destination buffer for samples.
 * @param max_count Maximum number of samples to read.
 * @return Number of samples read (>=1), 0 if max_count is 0, or -1 on exit.
 */
int
input_ring_read_block(struct input_ring_state* r, float* out, size_t max_count) {
    if (max_count == 0) {
        return 0;
    }
    while (input_ring_is_empty(r)) {
#ifdef USE_RTLSDR
        if (dsd_rtl_stream_should_exit()) {
            return -1;
        }
#endif
        dsd_mutex_lock(&r->ready_m);
        int ret = dsd_cond_timedwait(&r->ready, &r->ready_m, 10); /* 10ms */
        dsd_mutex_unlock(&r->ready_m);
        if (ret != 0) {
            if (exitflag) {
                return -1;
            }
#ifdef USE_RTLSDR
            if (dsd_rtl_stream_should_exit()) {
                return -1;
            }
#endif
            /* Metrics: consumer timed out waiting for input */
            r->read_timeouts.fetch_add(1);
            /* Timeout: check again */
            continue;
        }
    }

    size_t available = input_ring_used(r);
    size_t read_now = (max_count < available) ? max_count : available;
    size_t t = r->tail.load();
    size_t first = r->capacity - t;
    if (first >= read_now) {
        memcpy(out, r->buffer + t, read_now * sizeof(float));
        t += read_now;
        if (t >= r->capacity) {
            t = 0;
        }
    } else {
        memcpy(out, r->buffer + t, first * sizeof(float));
        memcpy(out + first, r->buffer, (read_now - first) * sizeof(float));
        t = read_now - first;
    }
    r->tail.store(t);

    return (int)read_now;
}
