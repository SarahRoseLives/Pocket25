// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

/**
 * @file
 * @brief POSIX compatibility wrappers for Windows portability.
 *
 * This header provides cross-platform wrappers for POSIX APIs that are
 * not available on Windows/MSVC. Include this header instead of using
 * POSIX-specific functions directly.
 */

#include <dsd-neo/platform/platform.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Environment Variables (setenv/unsetenv)
 * ============================================================================ */

/**
 * @brief Set an environment variable.
 *
 * @param name      Variable name.
 * @param value     Variable value.
 * @param overwrite If non-zero, overwrite existing value.
 * @return 0 on success, -1 on error.
 */
int dsd_setenv(const char* name, const char* value, int overwrite);

/**
 * @brief Unset an environment variable.
 *
 * @param name  Variable name.
 * @return 0 on success, -1 on error.
 */
int dsd_unsetenv(const char* name);

/* ============================================================================
 * String Functions (strdup, strtok_r, strcasecmp, strncasecmp)
 * ============================================================================ */

#if DSD_PLATFORM_WIN_NATIVE
#include <string.h>
#define dsd_strdup                        _strdup
#define dsd_strtok_r(str, delim, saveptr) strtok_s(str, delim, saveptr)
#define dsd_strcasecmp                    _stricmp
#define dsd_strncasecmp                   _strnicmp
#else
#include <string.h>
#include <strings.h>
#define dsd_strdup      strdup
#define dsd_strtok_r    strtok_r
#define dsd_strcasecmp  strcasecmp
#define dsd_strncasecmp strncasecmp
#endif

/* ============================================================================
 * getopt (MSVC)
 * ============================================================================ */

/* MSVC does not ship getopt/optarg/optind. Provide declarations for our
 * compatibility implementation in src/platform/posix_compat_win32.c. */
#if DSD_COMPILER_MSVC
extern char* optarg;
extern int optind;
extern int opterr;
extern int optopt;
int getopt(int argc, char* const argv[], const char* optstring);
#endif

/* ============================================================================
 * Directory Creation (mkdir)
 * ============================================================================ */

/**
 * @brief Create a directory.
 *
 * @param path  Path to create.
 * @param mode  Permission bits (ignored on Windows).
 * @return 0 on success, -1 on error.
 */
int dsd_mkdir(const char* path, int mode);

/* ============================================================================
 * Aligned Memory Allocation
 * ============================================================================ */

/**
 * @brief Allocate aligned memory.
 *
 * @param alignment Required alignment (must be power of 2).
 * @param size      Size in bytes.
 * @return Pointer to aligned memory, or NULL on failure.
 */
void* dsd_aligned_alloc(size_t alignment, size_t size);

/**
 * @brief Free aligned memory.
 *
 * @param ptr   Pointer returned by dsd_aligned_alloc.
 */
void dsd_aligned_free(void* ptr);

/* ============================================================================
 * Temporary Files (mkstemp, mkdtemp)
 * ============================================================================ */

/**
 * @brief Create a unique temporary file.
 *
 * @param tmpl  Template ending in "XXXXXX" (modified in place).
 * @return File descriptor on success, -1 on error.
 */
int dsd_mkstemp(char* tmpl);

/**
 * @brief Create a unique temporary directory.
 *
 * @param tmpl  Template ending in "XXXXXX" (modified in place).
 * @return tmpl on success, NULL on error.
 */
char* dsd_mkdtemp(char* tmpl);

/* ============================================================================
 * GCC/Clang Attribute Compatibility
 * ============================================================================ */

#if DSD_COMPILER_MSVC
#define DSD_ATTR_UNUSED
#define DSD_ATTR_NORETURN __declspec(noreturn)
#define DSD_ATTR_PACKED
#define DSD_ATTR_WEAK
#define DSD_ATTR_FORMAT(archetype, string_index, first_to_check)
#else
#define DSD_ATTR_UNUSED   __attribute__((unused))
#define DSD_ATTR_NORETURN __attribute__((noreturn))
#define DSD_ATTR_PACKED   __attribute__((packed))
#define DSD_ATTR_WEAK     __attribute__((weak))
#define DSD_ATTR_FORMAT(archetype, string_index, first_to_check)                                                       \
    __attribute__((format(archetype, string_index, first_to_check)))
#endif

/* ============================================================================
 * GCC Builtin Compatibility (__builtin_popcountll)
 * ============================================================================ */

#if DSD_COMPILER_MSVC
#include <intrin.h>

static inline int
dsd_popcount64(uint64_t x) {
    return (int)__popcnt64(x);
}
#else
static inline int
dsd_popcount64(uint64_t x) {
    return __builtin_popcountll(x);
}
#endif

/* ============================================================================
 * File Status Macros (S_ISDIR, S_ISREG) and stat() compatibility
 * ============================================================================ */

#if DSD_PLATFORM_WIN_NATIVE
#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
/* MSVC uses _stat/_stat64 instead of POSIX stat; provide alias */
#if DSD_COMPILER_MSVC
#define stat _stat
#endif
#endif

/* ============================================================================
 * Time Helpers (gettimeofday)
 * ============================================================================ */

#if DSD_PLATFORM_WIN_NATIVE
struct dsd_timeval {
    long tv_sec;
    long tv_usec;
};

/**
 * @brief Get current time of day (Windows implementation).
 *
 * @param tv    Output timeval structure.
 * @param tz    Ignored (pass NULL).
 * @return 0 on success.
 */
int dsd_gettimeofday(struct dsd_timeval* tv, void* tz);
#else
#include <sys/time.h>
#define dsd_timeval timeval

static inline int
dsd_gettimeofday(struct timeval* tv, void* tz) {
    return gettimeofday(tv, (struct timezone*)tz);
}
#endif

#ifdef __cplusplus
}
#endif
