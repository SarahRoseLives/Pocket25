// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

/*
 * Small helpers for making unit tests portable across Linux/macOS/Windows.
 *
 * Keep this header dependency-light and usable from both C and C++ tests.
 */

#include <dsd-neo/platform/file_compat.h>
#include <dsd-neo/platform/platform.h>
#include <dsd-neo/platform/posix_compat.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !DSD_PLATFORM_WIN_NATIVE
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DSD_TEST_PATH_MAX
#define DSD_TEST_PATH_MAX 1024
#endif

static inline int
dsd_test_is_path_sep(char c) {
    return c == '/' || c == '\\';
}

static inline char
dsd_test_path_sep(void) {
#if DSD_PLATFORM_WIN_NATIVE
    return '\\';
#else
    return '/';
#endif
}

static inline int
dsd_test_path_join(char* out, size_t out_sz, const char* dir, const char* leaf) {
    if (!out || out_sz == 0 || !leaf) {
        errno = EINVAL;
        return -1;
    }

    if (!dir || dir[0] == '\0') {
        size_t leaf_len = strlen(leaf);
        if (leaf_len + 1 > out_sz) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(out, leaf, leaf_len + 1);
        return 0;
    }

    size_t dir_len = strlen(dir);
    size_t leaf_len = strlen(leaf);
    int need_sep = (dir_len > 0 && !dsd_test_is_path_sep(dir[dir_len - 1]));

    size_t total = dir_len + (need_sep ? 1u : 0u) + leaf_len + 1u;
    if (total > out_sz) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(out, dir, dir_len);
    size_t pos = dir_len;
    if (need_sep) {
        out[pos++] = dsd_test_path_sep();
    }
    memcpy(out + pos, leaf, leaf_len + 1);
    return 0;
}

static inline const char*
dsd_test_tmpdir(void) {
    const char* v = getenv("DSD_NEO_TEST_TMPDIR");
    if (v && v[0] != '\0') {
        return v;
    }

#if DSD_PLATFORM_WIN_NATIVE
    v = getenv("TEMP");
    if (v && v[0] != '\0') {
        return v;
    }
    v = getenv("TMP");
    if (v && v[0] != '\0') {
        return v;
    }
#else
    v = getenv("TMPDIR");
    if (v && v[0] != '\0') {
        return v;
    }
#endif

    return ".";
}

static inline int
dsd_test_make_temp_template(char* out, size_t out_sz, const char* prefix) {
    if (!out || out_sz == 0 || !prefix || prefix[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    char leaf[256];
    int n = snprintf(leaf, sizeof(leaf), "%s_XXXXXX", prefix);
    if (n < 0 || (size_t)n >= sizeof(leaf)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return dsd_test_path_join(out, out_sz, dsd_test_tmpdir(), leaf);
}

static inline int
dsd_test_mkstemp(char* out_path, size_t out_sz, const char* prefix) {
    if (dsd_test_make_temp_template(out_path, out_sz, prefix) != 0) {
        return -1;
    }
    return dsd_mkstemp(out_path);
}

static inline char*
dsd_test_mkdtemp(char* out_path, size_t out_sz, const char* prefix) {
    if (dsd_test_make_temp_template(out_path, out_sz, prefix) != 0) {
        return NULL;
    }
    return dsd_mkdtemp(out_path);
}

static inline int
dsd_test_setenv(const char* name, const char* value, int overwrite) {
    return dsd_setenv(name, value, overwrite);
}

static inline int
dsd_test_unsetenv(const char* name) {
    return dsd_unsetenv(name);
}

static inline const char*
dsd_test_home_dir(void) {
    const char* v = NULL;

#if DSD_PLATFORM_WIN_NATIVE
    v = getenv("USERPROFILE");
    if (v && v[0] != '\0') {
        return v;
    }
    const char* d = getenv("HOMEDRIVE");
    const char* p = getenv("HOMEPATH");
    if (d && d[0] != '\0' && p && p[0] != '\0') {
        static char buf[DSD_TEST_PATH_MAX];
        snprintf(buf, sizeof(buf), "%s%s", d, p);
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }
#else
    v = getenv("HOME");
    if (v && v[0] != '\0') {
        return v;
    }
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
        return pw->pw_dir;
    }
#endif

    return NULL;
}

typedef struct dsd_test_capture_stderr {
    int saved_fd;
    char path[DSD_TEST_PATH_MAX];
} dsd_test_capture_stderr;

static inline int
dsd_test_capture_stderr_begin(dsd_test_capture_stderr* cap, const char* prefix) {
    if (!cap) {
        errno = EINVAL;
        return -1;
    }
    cap->saved_fd = -1;
    cap->path[0] = '\0';

    int saved = dsd_dup(DSD_STDERR_FILENO);
    if (saved < 0) {
        return -1;
    }

    int fd = dsd_test_mkstemp(cap->path, sizeof(cap->path), prefix);
    if (fd < 0) {
        dsd_close(saved);
        return -1;
    }

    if (dsd_dup2(fd, DSD_STDERR_FILENO) < 0) {
        dsd_close(fd);
        dsd_close(saved);
        return -1;
    }
    dsd_close(fd);

    cap->saved_fd = saved;
    return 0;
}

static inline int
dsd_test_capture_stderr_end(dsd_test_capture_stderr* cap) {
    if (!cap) {
        errno = EINVAL;
        return -1;
    }

    (void)fflush(stderr);

    if (cap->saved_fd >= 0) {
        (void)dsd_dup2(cap->saved_fd, DSD_STDERR_FILENO);
        (void)dsd_close(cap->saved_fd);
        cap->saved_fd = -1;
    }
    return 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
