// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Enable GNU extensions for pthread_setaffinity_np and CPU_* macros.
 * Must be defined before ANY system headers are included. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

/* Include sched.h early on Linux for CPU_* macros (needs _GNU_SOURCE) */
#if defined(__linux__)
#include <sched.h>
#endif

#include <dsd-neo/platform/threading.h>

#if !DSD_PLATFORM_WIN_NATIVE

#include <errno.h>
#include <string.h>
#include <time.h>

/*============================================================================
 * Thread Functions
 *============================================================================*/

int
dsd_thread_create(dsd_thread_t* thread, dsd_thread_fn func, void* arg) {
    if (!thread || !func) {
        return EINVAL;
    }
    return pthread_create(thread, NULL, func, arg);
}

int
dsd_thread_join(dsd_thread_t thread) {
    return pthread_join(thread, NULL);
}

dsd_thread_t
dsd_thread_self(void) {
    return pthread_self();
}

/*============================================================================
 * Mutex Functions
 *============================================================================*/

int
dsd_mutex_init(dsd_mutex_t* mutex) {
    if (!mutex) {
        return EINVAL;
    }
    return pthread_mutex_init(mutex, NULL);
}

int
dsd_mutex_destroy(dsd_mutex_t* mutex) {
    if (!mutex) {
        return EINVAL;
    }
    return pthread_mutex_destroy(mutex);
}

int
dsd_mutex_lock(dsd_mutex_t* mutex) {
    if (!mutex) {
        return EINVAL;
    }
    return pthread_mutex_lock(mutex);
}

int
dsd_mutex_unlock(dsd_mutex_t* mutex) {
    if (!mutex) {
        return EINVAL;
    }
    return pthread_mutex_unlock(mutex);
}

/*============================================================================
 * Condition Variable Functions
 *============================================================================*/

int
dsd_cond_init(dsd_cond_t* cond) {
    if (!cond) {
        return EINVAL;
    }
    return pthread_cond_init(cond, NULL);
}

int
dsd_cond_destroy(dsd_cond_t* cond) {
    if (!cond) {
        return EINVAL;
    }
    return pthread_cond_destroy(cond);
}

int
dsd_cond_wait(dsd_cond_t* cond, dsd_mutex_t* mutex) {
    if (!cond || !mutex) {
        return EINVAL;
    }
    return pthread_cond_wait(cond, mutex);
}

int
dsd_cond_timedwait(dsd_cond_t* cond, dsd_mutex_t* mutex, unsigned int timeout_ms) {
    if (!cond || !mutex) {
        return EINVAL;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;

    /* Normalize nanoseconds */
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    return pthread_cond_timedwait(cond, mutex, &ts);
}

int
dsd_cond_signal(dsd_cond_t* cond) {
    if (!cond) {
        return EINVAL;
    }
    return pthread_cond_signal(cond);
}

int
dsd_cond_broadcast(dsd_cond_t* cond) {
    if (!cond) {
        return EINVAL;
    }
    return pthread_cond_broadcast(cond);
}

/*============================================================================
 * Thread Priority / Scheduling
 *============================================================================*/

int
dsd_thread_set_realtime_priority(int priority) {
#if DSD_PLATFORM_LINUX || DSD_PLATFORM_MACOS
    struct sched_param sp;
    int policy = SCHED_FIFO;
    int pmax = sched_get_priority_max(policy);
    int pmin = sched_get_priority_min(policy);

    if (priority < pmin) {
        priority = pmin;
    }
    if (priority > pmax) {
        priority = pmax;
    }

    sp.sched_priority = priority;
    return pthread_setschedparam(pthread_self(), policy, &sp);
#else
    (void)priority;
    return ENOSYS;
#endif
}

int
dsd_thread_set_affinity(int cpu_index) {
#if DSD_PLATFORM_LINUX
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET((unsigned)cpu_index, &cpuset);
    #ifndef __ANDROID__
        return pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    #endif
#else
    (void)cpu_index;
    return ENOSYS; /* Not supported */
#endif
}

#endif /* !DSD_PLATFORM_WIN_NATIVE */
