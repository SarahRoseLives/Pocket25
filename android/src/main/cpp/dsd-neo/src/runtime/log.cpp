// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime logging implementation for environment-independent logging.
 *
 * Implements the low-level write routine used by logging macros to emit
 * messages. Currently forwards to `stderr`. Future enhancements may include
 * runtime level control, timestamps, and file sinks.
 */

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dsd-neo/runtime/log.h>
#include <dsd-neo/runtime/unicode.h>

#ifdef __ANDROID__
#include <android/log.h>
#define DSD_ANDROID_LOG_TAG "DSD-neo"
#endif

/**
 * @brief Write a formatted log message to the logging sink.
 *
 * Currently forwards to `stderr`. The `level` parameter is reserved for future
 * runtime gating and may be used to filter messages at runtime.
 *
 * @param level  Log severity level (currently not used for filtering).
 * @param format printf-style format string.
 * @param ...    Variadic arguments corresponding to `format`.
 */
void
dsd_neo_log_write(dsd_neo_log_level_t level, const char* format, ...) {
    if (format == nullptr) {
        return;
    }

    va_list args;
    va_start(args, format);
    /* Format into a temporary buffer first so we can apply ASCII fallback if needed. */
    char buf[4096];
    // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized)
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

#ifdef __ANDROID__
    // Map dsd-neo log levels to Android log priorities
    int android_prio;
    switch (level) {
        case LOG_LEVEL_ERROR:
            android_prio = ANDROID_LOG_ERROR;
            break;
        case LOG_LEVEL_WARN:
            android_prio = ANDROID_LOG_WARN;
            break;
        case LOG_LEVEL_INFO:
            android_prio = ANDROID_LOG_INFO;
            break;
        case LOG_LEVEL_DEBUG:
        default:
            android_prio = ANDROID_LOG_DEBUG;
            break;
    }
    __android_log_print(android_prio, DSD_ANDROID_LOG_TAG, "%s", buf);
#else
    (void)level; /* Currently unused, but available for future runtime gating */

    if (dsd_unicode_supported()) {
        fputs(buf, stderr);
    } else {
        char safe[4096];
        dsd_ascii_fallback(buf, safe, sizeof(safe));
        fputs(safe, stderr);
    }
#endif
}
