// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Output ring buffer for demodulated audio samples.
 *
 * Implements blocking producer/consumer operations with timed waits and
 * optional signaling semantics.
 */

#include <cstring>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/runtime/ring.h>
#include <errno.h>

extern "C" volatile uint8_t exitflag; // defined in src/runtime/exitflag.c

/**
 * @brief Write up to count samples, blocking until space is available.
 *
 * Signals data availability only on an empty-to-non-empty transition.
 *
 * @param o     Output ring buffer state.
 * @param data  Source samples to write.
 * @param count Number of samples to write.
 */
void
ring_write(struct output_state* o, const float* data, size_t count) {
    if (!o || !o->buffer) {
        return;
    }
    int need_signal = ring_is_empty(o);
    while (count > 0 && !exitflag) {
        size_t free_sp = ring_free(o);
        if (free_sp == 0) {
            /* Wait for space with timeout to avoid indefinite blocking */
            dsd_mutex_lock(&o->ready_m);
            int ret = dsd_cond_timedwait(&o->space, &o->ready_m, 50); /* 50ms */
            dsd_mutex_unlock(&o->ready_m);
            if (ret != 0) {
                if (exitflag) {
                    return;
                }
                /* Metrics: producer timed out waiting for space */
                o->write_timeouts.fetch_add(1);
                continue;
            }
        }
        free_sp = ring_free(o);
        if (free_sp == 0) {
            continue;
        }
        size_t write_now = (count < free_sp) ? count : free_sp;
        size_t h = o->head.load();

        /* First region: from head to end of buffer */
        size_t to_end = o->capacity - h;
        if (to_end >= write_now) {
            memcpy(o->buffer + h, data, write_now * sizeof(float));
            h += write_now;
            if (h >= o->capacity) {
                h = 0;
            }
            o->head.store(h);
            data += write_now;
            count -= write_now;
            continue;
        }

        /* Handle wrap-around case: split write_now across tail and head */
        if (to_end > 0) {
            memcpy(o->buffer + h, data, to_end * sizeof(float));
            data += to_end;
        }
        size_t remaining = write_now - to_end;
        if (remaining > 0) {
            memcpy(o->buffer, data, remaining * sizeof(float));
            h = remaining;
            data += remaining;
        } else {
            h = 0;
        }
        o->head.store(h);
        count -= write_now;
    }
    if (need_signal) {
        dsd_mutex_lock(&o->ready_m);
        dsd_cond_signal(&o->ready);
        dsd_mutex_unlock(&o->ready_m);
    }
}

/**
 * @brief Write up to count samples, blocking until space is available.
 *
 * Does not signal; caller should decide when to signal.
 *
 * @param o     Output ring buffer state.
 * @param data  Source samples to write.
 * @param count Number of samples to write.
 */
void
ring_write_no_signal(struct output_state* o, const float* data, size_t count) {
    if (!o || !o->buffer) {
        return;
    }
    while (count > 0 && !exitflag) {
        size_t free_sp = ring_free(o);
        if (free_sp == 0) {
            /* Wait for space with timeout to avoid indefinite blocking */
            dsd_mutex_lock(&o->ready_m);
            int ret = dsd_cond_timedwait(&o->space, &o->ready_m, 50); /* 50ms */
            dsd_mutex_unlock(&o->ready_m);
            if (ret != 0) {
                if (exitflag) {
                    return;
                }
                /* Metrics: producer timed out waiting for space */
                o->write_timeouts.fetch_add(1);
                continue;
            }
        }
        free_sp = ring_free(o);
        if (free_sp == 0) {
            continue;
        }
        size_t write_now = (count < free_sp) ? count : free_sp;
        size_t h = o->head.load();

        /* First region: from head to end of buffer */
        size_t to_end = o->capacity - h;
        if (to_end >= write_now) {
            memcpy(o->buffer + h, data, write_now * sizeof(float));
            h += write_now;
            if (h >= o->capacity) {
                h = 0;
            }
            o->head.store(h);
            data += write_now;
            count -= write_now;
            continue;
        }

        /* Handle wrap-around case: split write_now across tail and head */
        if (to_end > 0) {
            memcpy(o->buffer + h, data, to_end * sizeof(float));
            data += to_end;
        }
        size_t remaining = write_now - to_end;
        if (remaining > 0) {
            memcpy(o->buffer, data, remaining * sizeof(float));
            h = remaining;
            data += remaining;
        } else {
            h = 0;
        }
        o->head.store(h);
        count -= write_now;
    }
}

/**
 * @brief Write samples with signal on empty-to-non-empty transition.
 *
 * @param o     Output ring buffer state.
 * @param data  Source samples to write.
 * @param count Number of samples to write.
 */
void
ring_write_signal_on_empty_transition(struct output_state* o, const float* data, size_t count) {
    int need_signal = ring_is_empty(o);
    ring_write_no_signal(o, data, count);
    if (need_signal) {
        dsd_mutex_lock(&o->ready_m);
        dsd_cond_signal(&o->ready);
        dsd_mutex_unlock(&o->ready_m);
    }
}

/**
 * @brief Read one sample from the output ring, blocking with timeout until available.
 *
 * @param o    Output ring buffer state.
 * @param out  Destination for one sample.
 * @return 0 on success, -1 on exit.
 */
int
ring_read_one(struct output_state* o, float* out) {
    if (!o || !o->buffer) {
        return -1;
    }
    while (ring_is_empty(o)) {
        if (!o->buffer) {
            return -1; /* stream torn down */
        }
        dsd_mutex_lock(&o->ready_m);
        int ret = dsd_cond_timedwait(&o->ready, &o->ready_m, 10); /* 10ms */
        dsd_mutex_unlock(&o->ready_m);
        if (ret != 0) {
            if (exitflag) {
                return -1;
            }
            if (!o->buffer) {
                return -1; /* stream torn down */
            }
            /* Metrics: consumer timed out waiting for data */
            o->read_timeouts.fetch_add(1);
            /* Timeout: check again */
            continue;
        }
    }

    size_t t = o->tail.load();
    *out = o->buffer[t];
    t++;
    if (t == o->capacity) {
        t = 0;
    }
    o->tail.store(t);
    /* Signal space available for producer */
    dsd_mutex_lock(&o->ready_m);
    dsd_cond_signal(&o->space);
    dsd_mutex_unlock(&o->ready_m);
    return 0;
}

/**
 * @brief Read up to max_count samples into out.
 *
 * Blocks until at least one sample is available or exit.
 *
 * @param o         Output ring buffer state.
 * @param out       Destination buffer for samples.
 * @param max_count Maximum number of samples to read.
 * @return Number of samples read (>=1) or -1 on exit.
 */
int
ring_read_batch(struct output_state* o, float* out, size_t max_count) {
    if (max_count == 0) {
        return 0;
    }
    if (!o || !o->buffer) {
        return -1;
    }
    while (ring_is_empty(o)) {
        if (!o->buffer) {
            return -1; /* stream torn down */
        }
        dsd_mutex_lock(&o->ready_m);
        int ret = dsd_cond_timedwait(&o->ready, &o->ready_m, 10); /* 10ms */
        dsd_mutex_unlock(&o->ready_m);
        if (ret != 0) {
            if (exitflag) {
                return -1;
            }
            if (!o->buffer) {
                return -1; /* stream torn down */
            }
            /* Metrics: consumer timed out waiting for data */
            o->read_timeouts.fetch_add(1);
            /* Timeout: check again */
            continue;
        }
    }

    size_t available = ring_used(o);
    size_t read_now = (max_count < available) ? max_count : available;
    size_t t = o->tail.load();
    size_t first = o->capacity - t;
    if (first >= read_now) {
        memcpy(out, o->buffer + t, read_now * sizeof(float));
        t += read_now;
        if (t >= o->capacity) {
            t = 0;
        }
    } else {
        memcpy(out, o->buffer + t, first * sizeof(float));
        memcpy(out + first, o->buffer, (read_now - first) * sizeof(float));
        t = read_now - first;
    }
    o->tail.store(t);
    /* Signal space available once for the whole batch */
    dsd_mutex_lock(&o->ready_m);
    dsd_cond_signal(&o->space);
    dsd_mutex_unlock(&o->ready_m);
    return (int)read_now;
}
