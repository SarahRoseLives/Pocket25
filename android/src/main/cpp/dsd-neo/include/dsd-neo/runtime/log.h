// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#ifndef DSD_NEO_LOG_H
#define DSD_NEO_LOG_H

/**
 * @file
 * @brief Runtime logging interface used across DSD-neo components.
 *
 * Declares log severity levels, the core logging write routine, and convenience
 * macros. The implementation currently forwards messages to `stderr`.
 */

/**
 * @brief Log severity levels for runtime logging.
 */
typedef enum { LOG_LEVEL_ERROR = 0, LOG_LEVEL_WARN = 1, LOG_LEVEL_INFO = 2, LOG_LEVEL_DEBUG = 3 } dsd_neo_log_level_t;

/* Compile-time log level control (default to INFO) */
#ifndef DSD_NEO_LOG_LEVEL
#define DSD_NEO_LOG_LEVEL LOG_LEVEL_INFO
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
#ifdef __cplusplus
extern "C" {
#endif
void dsd_neo_log_write(dsd_neo_log_level_t level, const char* format, ...);
#ifdef __cplusplus
}
#endif

/* Logging macros route through dsd_neo_log_write to allow environment handling */

/* Error messages - always shown */
#define LOG_ERROR(...)                                                                                                 \
    do {                                                                                                               \
        dsd_neo_log_write(LOG_LEVEL_ERROR, __VA_ARGS__);                                                               \
    } while (0)

/* Warning messages - always shown */
#define LOG_WARN(...)                                                                                                  \
    do {                                                                                                               \
        dsd_neo_log_write(LOG_LEVEL_WARN, __VA_ARGS__);                                                                \
    } while (0)

/* Info messages - always shown */
#define LOG_INFO(...)                                                                                                  \
    do {                                                                                                               \
        dsd_neo_log_write(LOG_LEVEL_INFO, __VA_ARGS__);                                                                \
    } while (0)

/* Debug messages - compile-time gated */
#if DSD_NEO_LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(...)                                                                                                 \
    do {                                                                                                               \
        dsd_neo_log_write(LOG_LEVEL_DEBUG, __VA_ARGS__);                                                               \
    } while (0)
#else
#define LOG_DEBUG(...)                                                                                                 \
    do {                                                                                                               \
        /* Debug logging disabled */                                                                                   \
    } while (0)
#endif

/* Convenience macros for specific message types */

/* For warnings with WARNING: prefix */
#define LOG_WARNING(...)  LOG_WARN("WARNING: " __VA_ARGS__)

/* For notices with NOTICE: prefix */
#define LOG_NOTICE(...)   LOG_INFO("NOTICE: " __VA_ARGS__)

/* For critical errors that may exit */
#define LOG_CRITICAL(...) LOG_ERROR(__VA_ARGS__)

#endif /* DSD_NEO_LOG_H */
