// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

/**
 * @file
 * @brief Cross-platform high-resolution timing for DSD-neo.
 */

#include <dsd-neo/platform/platform.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get monotonic timestamp in nanoseconds.
 *
 * Returns a timestamp from a monotonic clock that is not affected by
 * system time changes. Suitable for measuring elapsed time.
 *
 * @return Nanoseconds since an arbitrary epoch.
 */
uint64_t dsd_time_monotonic_ns(void);

/**
 * @brief Get monotonic timestamp in milliseconds.
 *
 * @return Milliseconds since an arbitrary epoch.
 */
uint64_t dsd_time_monotonic_ms(void);

/**
 * @brief Get realtime (wall clock) timestamp in nanoseconds.
 *
 * Returns the current wall-clock time. May be affected by NTP adjustments.
 * Suitable for timeout calculations with condition variables.
 *
 * @return Nanoseconds since Unix epoch (1970-01-01 00:00:00 UTC).
 */
uint64_t dsd_time_realtime_ns(void);

/**
 * @brief Sleep for specified number of milliseconds.
 *
 * @param ms    Milliseconds to sleep.
 */
void dsd_sleep_ms(unsigned int ms);

/**
 * @brief Sleep for specified number of nanoseconds.
 *
 * Note: Actual resolution depends on platform. Windows typically has ~1ms
 * minimum granularity.
 *
 * @param ns    Nanoseconds to sleep.
 */
void dsd_sleep_ns(uint64_t ns);

/**
 * @brief Sleep for specified number of microseconds.
 *
 * Convenience function for microsecond sleep granularity.
 *
 * @param us    Microseconds to sleep.
 */
void dsd_sleep_us(uint64_t us);

/**
 * @brief Calculate deadline for timed wait (realtime clock).
 *
 * Returns an absolute timestamp suitable for use with condition variable
 * timed waits.
 *
 * @param timeout_ms    Relative timeout in milliseconds.
 * @return Absolute deadline in nanoseconds (realtime clock).
 */
uint64_t dsd_time_deadline_ns(unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif
