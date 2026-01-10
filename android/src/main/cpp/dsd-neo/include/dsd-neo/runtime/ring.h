// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Output ring buffer API for demodulated audio samples.
 */

#pragma once

#include <atomic>
#include <stdint.h>
#include <stdlib.h>

#include <dsd-neo/platform/threading.h>

struct output_state {
    int rate;
    float* buffer;
    size_t capacity;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    dsd_cond_t ready;
    dsd_mutex_t ready_m;
    dsd_cond_t space;
    std::atomic<uint64_t> write_timeouts; /* producer waited for space */
    std::atomic<uint64_t> read_timeouts;  /* consumer waited for data */
};

/**
 * @brief Number of queued samples in the output ring.
 *
 * @param o Output ring state.
 * @return Number of queued samples.
 */
static inline size_t
ring_used(const struct output_state* o) {
    /* Atomics policy: head/tail are atomics. We use default sequential
       consistency for simplicity. In an SPSC ring, this could be relaxed
       to acquire/release without changing behavior. */
    size_t h = o->head.load();
    size_t t = o->tail.load();
    if (h >= t) {
        return h - t;
    }
    return o->capacity - t + h;
}

/**
 * @brief Number of writable samples before the ring becomes full.
 *
 * @param o Output ring state.
 * @return Number of writable samples before the ring becomes full.
 */
static inline size_t
ring_free(const struct output_state* o) {
    return (o->capacity - 1) - ring_used(o);
}

/**
 * @brief Check if the output ring is empty.
 *
 * @param o Output ring state.
 * @return Non-zero if empty, zero otherwise.
 */
static inline int
ring_is_empty(const struct output_state* o) {
    /* See atomics note in ring_used() for ordering considerations. */
    return o->head.load() == o->tail.load();
}

/**
 * @brief Clear the output ring head/tail indices.
 *
 * @param o Output ring state to clear.
 */
static inline void
ring_clear(struct output_state* o) {
    /* Clearing indices; with relaxed ordering this would be a release store. */
    o->tail.store(0);
    o->head.store(0);
}

/**
 * @brief Write up to count samples, blocking until space is available.
 *
 * Signals data availability only on an empty-to-non-empty transition.
 *
 * @param o     Output ring buffer state.
 * @param data  Source samples to write.
 * @param count Number of samples to write.
 */
void ring_write(struct output_state* o, const float* data, size_t count);

/**
 * @brief Write up to count samples, blocking until space is available.
 *
 * Does not signal; caller should decide when to signal.
 *
 * @param o     Output ring buffer state.
 * @param data  Source samples to write.
 * @param count Number of samples to write.
 */
void ring_write_no_signal(struct output_state* o, const float* data, size_t count);

/**
 * @brief Write samples with signal on empty-to-non-empty transition.
 *
 * @param o     Output ring buffer state.
 * @param data  Source samples to write.
 * @param count Number of samples to write.
 */
void ring_write_signal_on_empty_transition(struct output_state* o, const float* data, size_t count);

/**
 * @brief Read one sample from the output ring, blocking with timeout until available.
 *
 * @param o    Output ring buffer state.
 * @param out  Destination for one sample.
 * @return 0 on success, -1 on exit.
 */
int ring_read_one(struct output_state* o, float* out);

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
int ring_read_batch(struct output_state* o, float* out, size_t max_count);
