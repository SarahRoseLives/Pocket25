// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/platform/file_compat.h>

#if DSD_PLATFORM_WIN_NATIVE

#include <io.h>
#include <sys/stat.h>
#include <windows.h>

int
dsd_fileno(FILE* fp) {
    return fp ? _fileno(fp) : -1;
}

int
dsd_isatty(int fd) {
    return _isatty(fd);
}

int
dsd_dup(int oldfd) {
    return _dup(oldfd);
}

int
dsd_dup2(int oldfd, int newfd) {
    return _dup2(oldfd, newfd);
}

int
dsd_close(int fd) {
    return _close(fd);
}

int
dsd_fsync(int fd) {
    /* _commit is Windows equivalent of fsync */
    return _commit(fd);
}

int
dsd_fstat(int fd, dsd_stat_t* st) {
    return _fstat(fd, st);
}

int
dsd_fchmod(int fd, int mode) {
    (void)fd;
    (void)mode;
    /* Windows lacks descriptor-based chmod; treat as no-op */
    return 0;
}

ssize_t
dsd_read(int fd, void* buf, size_t count) {
    return (ssize_t)_read(fd, buf, (unsigned int)count);
}

ssize_t
dsd_write(int fd, const void* buf, size_t count) {
    return (ssize_t)_write(fd, buf, (unsigned int)count);
}

const char*
dsd_null_device(void) {
    return "NUL";
}

#endif /* DSD_PLATFORM_WIN_NATIVE */
