// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/platform/timing.h>

#if !DSD_PLATFORM_WIN_NATIVE

#include <errno.h>
#include <time.h>

uint64_t
dsd_time_monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t
dsd_time_monotonic_ms(void) {
    return dsd_time_monotonic_ns() / 1000000ULL;
}

uint64_t
dsd_time_realtime_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void
dsd_sleep_ms(unsigned int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        /* Retry if interrupted by signal */
    }
}

void
dsd_sleep_ns(uint64_t ns) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        /* Retry if interrupted */
    }
}

void
dsd_sleep_us(uint64_t us) {
    dsd_sleep_ns(us * 1000ULL);
}

uint64_t
dsd_time_deadline_ns(unsigned int timeout_ms) {
    return dsd_time_realtime_ns() + (uint64_t)timeout_ms * 1000000ULL;
}

#endif /* !DSD_PLATFORM_WIN_NATIVE */
