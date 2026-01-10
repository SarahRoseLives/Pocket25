// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Realtime scheduling and CPU affinity API.
 *
 * Declares utilities to enable SCHED_FIFO priority scheduling and optional
 * CPU pinning for critical threads, controlled via environment variables.
 */

#ifndef DSD_NEO_RT_SCHED_H
#define DSD_NEO_RT_SCHED_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Optionally enable realtime scheduling and set CPU affinity for the current thread.
 *
 * Controlled by environment variables. When `DSD_NEO_RT_SCHED=1`, attempts to switch
 * the calling thread to SCHED_FIFO with a priority derived from `DSD_NEO_RT_PRIO_<ROLE>`
 * if present. If `DSD_NEO_CPU_<ROLE>` is set to a valid CPU index, pins the thread
 * to that CPU.
 *
 * @param role Optional role label (e.g. "DEMOD", "DONGLE", "USB").
 */
void maybe_set_thread_realtime_and_affinity(const char* role);

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_RT_SCHED_H */
