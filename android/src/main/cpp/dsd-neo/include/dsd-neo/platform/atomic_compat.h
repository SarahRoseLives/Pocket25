// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

/**
 * @file
 * @brief Cross-platform atomics wrapper for C sources.
 *
 * MSVC's <stdatomic.h> may hard-error when C11 atomics are unavailable
 * (vcruntime_c11_stdatomic.h: "C atomic support is not enabled"). Provide a
 * small fallback implementation for the atomics used by dsd-neo.
 */

#include <dsd-neo/platform/platform.h>

#if DSD_COMPILER_MSVC && !DSD_COMPILER_CLANG && defined(__STDC_NO_ATOMICS__)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef volatile LONG atomic_int;

static inline int
atomic_load(const atomic_int* obj) {
    return (int)InterlockedCompareExchange((volatile LONG*)obj, 0, 0);
}

static inline void
atomic_store(atomic_int* obj, int desired) {
    (void)InterlockedExchange((volatile LONG*)obj, (LONG)desired);
}

static inline int
atomic_exchange(atomic_int* obj, int desired) {
    return (int)InterlockedExchange((volatile LONG*)obj, (LONG)desired);
}

static inline int
atomic_fetch_add(atomic_int* obj, int arg) {
    return (int)InterlockedExchangeAdd((volatile LONG*)obj, (LONG)arg);
}

static inline int
atomic_compare_exchange_strong(atomic_int* obj, int* expected, int desired) {
    LONG old = InterlockedCompareExchange((volatile LONG*)obj, (LONG)desired, (LONG)*expected);
    if (old == (LONG)*expected) {
        return 1;
    }
    *expected = (int)old;
    return 0;
}

#else

#include <stdatomic.h>

#endif
