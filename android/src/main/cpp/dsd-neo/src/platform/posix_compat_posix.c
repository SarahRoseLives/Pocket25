#ifdef __ANDROID__
#include <malloc.h>
#endif
// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/platform/posix_compat.h>

#if !DSD_PLATFORM_WIN_NATIVE

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int
dsd_setenv(const char* name, const char* value, int overwrite) {
    return setenv(name, value, overwrite);
}

int
dsd_unsetenv(const char* name) {
    return unsetenv(name);
}

int
dsd_mkdir(const char* path, int mode) {
    return mkdir(path, (mode_t)mode);
}

void*
dsd_aligned_alloc(size_t alignment, size_t size) {
    void* ptr = NULL;
#ifdef __ANDROID__
    /* Android API < 28 doesn't have aligned_alloc, use memalign */
    ptr = memalign(alignment, size);
#elif defined(_ISOC11_SOURCE) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L)
    /* C11 aligned_alloc requires size to be multiple of alignment */
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    ptr = aligned_alloc(alignment, aligned_size);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = NULL;
    }
#endif
    return ptr;
}

void
dsd_aligned_free(void* ptr) {
    free(ptr);
}

int
dsd_mkstemp(char* tmpl) {
    return mkstemp(tmpl);
}

char*
dsd_mkdtemp(char* tmpl) {
    return mkdtemp(tmpl);
}

#endif /* !DSD_PLATFORM_WIN_NATIVE */
