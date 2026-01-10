// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/platform/file_compat.h>

#if !DSD_PLATFORM_WIN_NATIVE

#include <sys/stat.h>
#include <unistd.h>

int
dsd_fileno(FILE* fp) {
    return fp ? fileno(fp) : -1;
}

int
dsd_isatty(int fd) {
    return isatty(fd);
}

int
dsd_dup(int oldfd) {
    return dup(oldfd);
}

int
dsd_dup2(int oldfd, int newfd) {
    return dup2(oldfd, newfd);
}

int
dsd_close(int fd) {
    return close(fd);
}

int
dsd_fsync(int fd) {
    return fsync(fd);
}

int
dsd_fstat(int fd, dsd_stat_t* st) {
    return fstat(fd, st);
}

int
dsd_fchmod(int fd, int mode) {
    return fchmod(fd, (mode_t)mode);
}

ssize_t
dsd_read(int fd, void* buf, size_t count) {
    return read(fd, buf, count);
}

ssize_t
dsd_write(int fd, const void* buf, size_t count) {
    return write(fd, buf, count);
}

const char*
dsd_null_device(void) {
    return "/dev/null";
}

#endif /* !DSD_PLATFORM_WIN_NATIVE */
