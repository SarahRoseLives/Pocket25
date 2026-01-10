// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/platform/timing.h>

#if DSD_PLATFORM_WIN_NATIVE

#include <windows.h>

/* Cached QPC frequency */
static LARGE_INTEGER s_qpc_freq = {0};
static int s_qpc_initialized = 0;

static void
ensure_qpc_initialized(void) {
    if (!s_qpc_initialized) {
        QueryPerformanceFrequency(&s_qpc_freq);
        s_qpc_initialized = 1;
    }
}

uint64_t
dsd_time_monotonic_ns(void) {
    ensure_qpc_initialized();

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    /* Convert to nanoseconds, avoiding overflow */
    uint64_t seconds = counter.QuadPart / s_qpc_freq.QuadPart;
    uint64_t remainder = counter.QuadPart % s_qpc_freq.QuadPart;

    return seconds * 1000000000ULL + (remainder * 1000000000ULL) / s_qpc_freq.QuadPart;
}

uint64_t
dsd_time_monotonic_ms(void) {
    ensure_qpc_initialized();

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    return (counter.QuadPart * 1000ULL) / s_qpc_freq.QuadPart;
}

uint64_t
dsd_time_realtime_ns(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    /* FILETIME is 100-nanosecond intervals since Jan 1, 1601 */
    /* Convert to Unix epoch (Jan 1, 1970) */
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    /* Subtract Windows-to-Unix epoch difference (in 100ns intervals) */
    /* 116444736000000000 = days between 1601 and 1970 in 100ns */
    const uint64_t EPOCH_DIFF = 116444736000000000ULL;
    uint64_t unix_100ns = uli.QuadPart - EPOCH_DIFF;

    return unix_100ns * 100ULL; /* Convert 100ns to ns */
}

void
dsd_sleep_ms(unsigned int ms) {
    Sleep(ms);
}

void
dsd_sleep_ns(uint64_t ns) {
    /* Windows Sleep has millisecond granularity */
    DWORD ms = (DWORD)((ns + 999999ULL) / 1000000ULL);
    if (ms == 0 && ns > 0) {
        ms = 1;
    }
    Sleep(ms);
}

void
dsd_sleep_us(uint64_t us) {
    /* Windows Sleep has millisecond granularity, round up */
    DWORD ms = (DWORD)((us + 999ULL) / 1000ULL);
    if (ms == 0 && us > 0) {
        ms = 1;
    }
    Sleep(ms);
}

uint64_t
dsd_time_deadline_ns(unsigned int timeout_ms) {
    return dsd_time_realtime_ns() + (uint64_t)timeout_ms * 1000000ULL;
}

#endif /* DSD_PLATFORM_WIN_NATIVE */
