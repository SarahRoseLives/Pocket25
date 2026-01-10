// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/platform/threading.h>

#if DSD_PLATFORM_WIN_NATIVE

#include <errno.h>
#include <process.h>

/*============================================================================
 * Thread Functions
 *============================================================================*/

int
dsd_thread_create(dsd_thread_t* thread, dsd_thread_fn func, void* arg) {
    if (!thread || !func) {
        return EINVAL;
    }

    /* _beginthreadex returns 0 on failure, handle otherwise */
    uintptr_t h = _beginthreadex(NULL, 0, func, arg, 0, NULL);
    if (h == 0) {
        return errno ? errno : EAGAIN;
    }
    *thread = (HANDLE)h;
    return 0;
}

int
dsd_thread_join(dsd_thread_t thread) {
    if (thread == NULL || thread == INVALID_HANDLE_VALUE) {
        return EINVAL;
    }
    DWORD result = WaitForSingleObject(thread, INFINITE);
    if (result == WAIT_OBJECT_0) {
        CloseHandle(thread);
        return 0;
    }
    return EINVAL;
}

dsd_thread_t
dsd_thread_self(void) {
    return GetCurrentThread();
}

/*============================================================================
 * Mutex Functions (using CRITICAL_SECTION for performance)
 *============================================================================*/

int
dsd_mutex_init(dsd_mutex_t* mutex) {
    if (!mutex) {
        return EINVAL;
    }
    InitializeCriticalSection(mutex);
    return 0;
}

int
dsd_mutex_destroy(dsd_mutex_t* mutex) {
    if (!mutex) {
        return EINVAL;
    }
    DeleteCriticalSection(mutex);
    return 0;
}

int
dsd_mutex_lock(dsd_mutex_t* mutex) {
    if (!mutex) {
        return EINVAL;
    }
    EnterCriticalSection(mutex);
    return 0;
}

int
dsd_mutex_unlock(dsd_mutex_t* mutex) {
    if (!mutex) {
        return EINVAL;
    }
    LeaveCriticalSection(mutex);
    return 0;
}

/*============================================================================
 * Condition Variable Functions (Vista+ CONDITION_VARIABLE)
 *============================================================================*/

int
dsd_cond_init(dsd_cond_t* cond) {
    if (!cond) {
        return EINVAL;
    }
    InitializeConditionVariable(cond);
    return 0;
}

int
dsd_cond_destroy(dsd_cond_t* cond) {
    /* Windows condition variables don't need explicit destruction */
    (void)cond;
    return 0;
}

int
dsd_cond_wait(dsd_cond_t* cond, dsd_mutex_t* mutex) {
    if (!cond || !mutex) {
        return EINVAL;
    }
    if (!SleepConditionVariableCS(cond, mutex, INFINITE)) {
        return GetLastError();
    }
    return 0;
}

int
dsd_cond_timedwait(dsd_cond_t* cond, dsd_mutex_t* mutex, unsigned int timeout_ms) {
    if (!cond || !mutex) {
        return EINVAL;
    }
    if (!SleepConditionVariableCS(cond, mutex, timeout_ms)) {
        DWORD err = GetLastError();
        if (err == ERROR_TIMEOUT) {
            return ETIMEDOUT;
        }
        return err;
    }
    return 0;
}

int
dsd_cond_signal(dsd_cond_t* cond) {
    if (!cond) {
        return EINVAL;
    }
    WakeConditionVariable(cond);
    return 0;
}

int
dsd_cond_broadcast(dsd_cond_t* cond) {
    if (!cond) {
        return EINVAL;
    }
    WakeAllConditionVariable(cond);
    return 0;
}

/*============================================================================
 * Thread Priority / Scheduling
 *============================================================================*/

int
dsd_thread_set_realtime_priority(int priority) {
    /*
     * Windows priority mapping:
     * priority < 0  -> THREAD_PRIORITY_BELOW_NORMAL
     * priority == 0 -> THREAD_PRIORITY_NORMAL
     * priority > 0  -> THREAD_PRIORITY_ABOVE_NORMAL to TIME_CRITICAL
     */
    int win_priority;

    if (priority <= -2) {
        win_priority = THREAD_PRIORITY_LOWEST;
    } else if (priority == -1) {
        win_priority = THREAD_PRIORITY_BELOW_NORMAL;
    } else if (priority == 0) {
        win_priority = THREAD_PRIORITY_NORMAL;
    } else if (priority == 1) {
        win_priority = THREAD_PRIORITY_ABOVE_NORMAL;
    } else if (priority == 2) {
        win_priority = THREAD_PRIORITY_HIGHEST;
    } else {
        win_priority = THREAD_PRIORITY_TIME_CRITICAL;
    }

    if (!SetThreadPriority(GetCurrentThread(), win_priority)) {
        return GetLastError();
    }
    return 0;
}

int
dsd_thread_set_affinity(int cpu_index) {
    if (cpu_index < 0) {
        return EINVAL;
    }

    DWORD_PTR mask = (DWORD_PTR)1 << cpu_index;
    if (SetThreadAffinityMask(GetCurrentThread(), mask) == 0) {
        return GetLastError();
    }
    return 0;
}

#endif /* DSD_PLATFORM_WIN_NATIVE */
