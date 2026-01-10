// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

/**
 * @file
 * @brief Cross-platform file descriptor compatibility for DSD-neo.
 */

#include <dsd-neo/platform/platform.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#if DSD_PLATFORM_WIN_NATIVE
#include <sys/stat.h>
typedef struct _stat dsd_stat_t;
#else
#include <sys/stat.h>
typedef struct stat dsd_stat_t;
#endif

/* ssize_t definition for Windows */
#if DSD_PLATFORM_WIN_NATIVE && !defined(_SSIZE_T_DEFINED)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

/**
 * @brief Get file descriptor from FILE stream.
 *
 * @param fp    FILE pointer.
 * @return File descriptor, or -1 on error.
 */
int dsd_fileno(FILE* fp);

/**
 * @brief Check if file descriptor refers to a terminal.
 *
 * @param fd    File descriptor.
 * @return Non-zero if terminal, 0 otherwise.
 */
int dsd_isatty(int fd);

/**
 * @brief Duplicate a file descriptor.
 *
 * @param oldfd     File descriptor to duplicate.
 * @return New file descriptor, or -1 on error.
 */
int dsd_dup(int oldfd);

/**
 * @brief Duplicate a file descriptor to a specific number.
 *
 * @param oldfd     Source file descriptor.
 * @param newfd     Target file descriptor number.
 * @return newfd on success, -1 on error.
 */
int dsd_dup2(int oldfd, int newfd);

/**
 * @brief Close a file descriptor.
 *
 * @param fd    File descriptor to close.
 * @return 0 on success, -1 on error.
 */
int dsd_close(int fd);

/**
 * @brief Flush file data to disk.
 *
 * @param fd    File descriptor.
 * @return 0 on success, -1 on error.
 */
int dsd_fsync(int fd);

/**
 * @brief Get file status.
 *
 * @param fd    File descriptor.
 * @param st    Output stat struct.
 * @return 0 on success, -1 on error.
 */
int dsd_fstat(int fd, dsd_stat_t* st);

/**
 * @brief Set file permissions (best effort on Windows).
 *
 * @param fd    File descriptor.
 * @param mode  POSIX permission bits.
 * @return 0 on success, -1 on error.
 */
int dsd_fchmod(int fd, int mode);

/**
 * @brief Read from a file descriptor.
 *
 * @param fd        File descriptor.
 * @param buf       Output buffer.
 * @param count     Bytes to read.
 * @return Number of bytes read, 0 on EOF, or -1 on error.
 */
ssize_t dsd_read(int fd, void* buf, size_t count);

/**
 * @brief Write to a file descriptor.
 *
 * @param fd        File descriptor.
 * @param buf       Data buffer.
 * @param count     Bytes to write.
 * @return Number of bytes written, or -1 on error.
 */
ssize_t dsd_write(int fd, const void* buf, size_t count);

/**
 * @brief Get the null device path for this platform.
 *
 * @return "/dev/null" on POSIX, "NUL" on Windows.
 */
const char* dsd_null_device(void);

/**
 * @brief Standard file descriptor numbers.
 */
#if DSD_PLATFORM_WIN_NATIVE
#include <io.h>
#define DSD_STDIN_FILENO  0
#define DSD_STDOUT_FILENO 1
#define DSD_STDERR_FILENO 2
#else
#include <unistd.h>
#define DSD_STDIN_FILENO  STDIN_FILENO
#define DSD_STDOUT_FILENO STDOUT_FILENO
#define DSD_STDERR_FILENO STDERR_FILENO
#endif

#ifdef __cplusplus
}
#endif
