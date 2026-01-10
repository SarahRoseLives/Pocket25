// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

/**
 * @file
 * @brief Cross-platform threading abstraction for DSD-neo.
 *
 * Provides a unified API for threads, mutexes, and condition variables
 * that works on both POSIX (pthreads) and Windows (Win32 threads).
 */

#include <dsd-neo/platform/platform.h>

#if DSD_PLATFORM_WIN_NATIVE
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Type Definitions
 *============================================================================*/

#if DSD_PLATFORM_WIN_NATIVE

typedef HANDLE dsd_thread_t;
typedef CRITICAL_SECTION dsd_mutex_t;
typedef CONDITION_VARIABLE dsd_cond_t;

/* Thread function signature */
typedef unsigned int(__stdcall* dsd_thread_fn)(void*);
#define DSD_THREAD_RETURN_TYPE unsigned int
#define DSD_THREAD_RETURN      return 0

#else /* POSIX */

typedef pthread_t dsd_thread_t;
typedef pthread_mutex_t dsd_mutex_t;
typedef pthread_cond_t dsd_cond_t;

/* Thread function signature */
typedef void* (*dsd_thread_fn)(void*);
#define DSD_THREAD_RETURN_TYPE void*
#define DSD_THREAD_RETURN      return NULL

#endif

/*============================================================================
 * Thread Functions
 *============================================================================*/

/**
 * @brief Create and start a new thread.
 *
 * @param thread    Pointer to thread handle (output).
 * @param func      Thread entry function.
 * @param arg       Argument passed to thread function.
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_thread_create(dsd_thread_t* thread, dsd_thread_fn func, void* arg);

/**
 * @brief Wait for a thread to terminate.
 *
 * @param thread    Thread handle.
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_thread_join(dsd_thread_t thread);

/**
 * @brief Get handle/ID of the calling thread.
 *
 * @return Thread handle of caller.
 */
dsd_thread_t dsd_thread_self(void);

/*============================================================================
 * Mutex Functions
 *============================================================================*/

/**
 * @brief Initialize a mutex.
 *
 * @param mutex     Pointer to mutex (output).
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_mutex_init(dsd_mutex_t* mutex);

/**
 * @brief Destroy a mutex and release resources.
 *
 * @param mutex     Pointer to mutex.
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_mutex_destroy(dsd_mutex_t* mutex);

/**
 * @brief Lock a mutex (blocking).
 *
 * @param mutex     Pointer to mutex.
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_mutex_lock(dsd_mutex_t* mutex);

/**
 * @brief Unlock a mutex.
 *
 * @param mutex     Pointer to mutex.
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_mutex_unlock(dsd_mutex_t* mutex);

/*============================================================================
 * Condition Variable Functions
 *============================================================================*/

/**
 * @brief Initialize a condition variable.
 *
 * @param cond      Pointer to condition variable (output).
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_cond_init(dsd_cond_t* cond);

/**
 * @brief Destroy a condition variable.
 *
 * @param cond      Pointer to condition variable.
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_cond_destroy(dsd_cond_t* cond);

/**
 * @brief Wait on a condition variable.
 *
 * Atomically unlocks the mutex and waits for the condition to be signaled.
 * Mutex is re-locked before returning.
 *
 * @param cond      Pointer to condition variable.
 * @param mutex     Pointer to associated mutex (must be locked).
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_cond_wait(dsd_cond_t* cond, dsd_mutex_t* mutex);

/**
 * @brief Wait on a condition variable with timeout.
 *
 * @param cond          Pointer to condition variable.
 * @param mutex         Pointer to associated mutex (must be locked).
 * @param timeout_ms    Timeout in milliseconds.
 * @return 0 on success, ETIMEDOUT on timeout, other non-zero on error.
 */
int dsd_cond_timedwait(dsd_cond_t* cond, dsd_mutex_t* mutex, unsigned int timeout_ms);

/**
 * @brief Signal one thread waiting on a condition variable.
 *
 * @param cond      Pointer to condition variable.
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_cond_signal(dsd_cond_t* cond);

/**
 * @brief Signal all threads waiting on a condition variable.
 *
 * @param cond      Pointer to condition variable.
 * @return 0 on success, non-zero error code on failure.
 */
int dsd_cond_broadcast(dsd_cond_t* cond);

/*============================================================================
 * Thread Priority / Scheduling (Optional)
 *============================================================================*/

/**
 * @brief Attempt to set realtime priority for current thread.
 *
 * @param priority  Priority level (platform-specific interpretation).
 * @return 0 on success, non-zero on failure (may require elevated privileges).
 */
int dsd_thread_set_realtime_priority(int priority);

/**
 * @brief Set CPU affinity for current thread.
 *
 * @param cpu_index     CPU core index to pin to.
 * @return 0 on success, non-zero on failure or if unsupported.
 */
int dsd_thread_set_affinity(int cpu_index);

#ifdef __cplusplus
}
#endif
