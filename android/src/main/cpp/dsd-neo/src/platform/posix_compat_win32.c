// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/platform/posix_compat.h>

#if DSD_PLATFORM_WIN_NATIVE

#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h>

#if DSD_COMPILER_MSVC
/* Minimal getopt(3) implementation for MSVC builds. MinGW provides
 * getopt via its POSIX layer, so we only enable this for MSVC. */
char* optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

int
getopt(int argc, char* const argv[], const char* optstring) {
    static int optpos = 1;

    if (argv == NULL || optstring == NULL) {
        return -1;
    }

    /* Allow callers to reset parsing by setting optind <= 1. */
    if (optind <= 1) {
        optind = 1;
        optpos = 1;
    }

    optarg = NULL;

    if (optind >= argc) {
        return -1;
    }

    char* arg = argv[optind];
    if (arg == NULL) {
        return -1;
    }

    /* Start of a new argv element: validate that it looks like an option. */
    if (optpos == 1) {
        if (arg[0] != '-' || arg[1] == '\0') {
            return -1;
        }
        /* End-of-options marker */
        if (arg[1] == '-' && arg[2] == '\0') {
            optind++;
            return -1;
        }
    }

    /* Consume next option character from this argv element. */
    char c = arg[optpos];
    if (c == '\0') {
        optind++;
        optpos = 1;
        return getopt(argc, argv, optstring);
    }

    optopt = (unsigned char)c;

    const char* spec = strchr(optstring, c);
    if (spec == NULL || c == ':') {
        /* Unknown option */
        optpos++;
        if (arg[optpos] == '\0') {
            optind++;
            optpos = 1;
        }
        return '?';
    }

    if (spec[1] == ':') {
        /* Option requires an argument. */
        if (arg[optpos + 1] != '\0') {
            optarg = &arg[optpos + 1];
            optind++;
            optpos = 1;
        } else if (optind + 1 < argc && argv[optind + 1] != NULL) {
            optarg = argv[optind + 1];
            optind += 2;
            optpos = 1;
        } else {
            /* Missing required argument */
            optpos++;
            if (arg[optpos] == '\0') {
                optind++;
                optpos = 1;
            }
            return (optstring[0] == ':') ? ':' : '?';
        }
    } else {
        /* Option does not take an argument. */
        optpos++;
        if (arg[optpos] == '\0') {
            optind++;
            optpos = 1;
        }
    }

    return (int)c;
}
#endif /* DSD_COMPILER_MSVC */

int
dsd_setenv(const char* name, const char* value, int overwrite) {
    if (name == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!overwrite) {
        /* Check if variable already exists */
        size_t required = 0;
        if (getenv_s(&required, NULL, 0, name) == 0 && required > 0) {
            return 0; /* Already exists, don't overwrite */
        }
    }
    if (_putenv_s(name, value) != 0) {
        return -1;
    }
    return 0;
}

int
dsd_unsetenv(const char* name) {
    if (name == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* Setting to empty string removes the variable */
    if (_putenv_s(name, "") != 0) {
        return -1;
    }
    return 0;
}

int
dsd_mkdir(const char* path, int mode) {
    (void)mode; /* Windows ignores mode */
    return _mkdir(path);
}

void*
dsd_aligned_alloc(size_t alignment, size_t size) {
    return _aligned_malloc(size, alignment);
}

void
dsd_aligned_free(void* ptr) {
    _aligned_free(ptr);
}

int
dsd_mkstemp(char* tmpl) {
    if (tmpl == NULL) {
        errno = EINVAL;
        return -1;
    }

    size_t len = strlen(tmpl);
    if (len < 6) {
        errno = EINVAL;
        return -1;
    }

    /* Verify template ends with XXXXXX */
    if (strcmp(tmpl + len - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }

    /* _mktemp_s modifies the template in place */
    if (_mktemp_s(tmpl, len + 1) != 0) {
        return -1;
    }

    /* Open the file with exclusive access */
    int fd = _open(tmpl, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
    return fd;
}

char*
dsd_mkdtemp(char* tmpl) {
    if (tmpl == NULL) {
        errno = EINVAL;
        return NULL;
    }

    size_t len = strlen(tmpl);
    if (len < 6) {
        errno = EINVAL;
        return NULL;
    }

    /* Verify template ends with XXXXXX */
    if (strcmp(tmpl + len - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return NULL;
    }

    /* _mktemp_s modifies the template in place */
    if (_mktemp_s(tmpl, len + 1) != 0) {
        return NULL;
    }

    /* Create the directory */
    if (_mkdir(tmpl) != 0) {
        return NULL;
    }

    return tmpl;
}

int
dsd_gettimeofday(struct dsd_timeval* tv, void* tz) {
    (void)tz;
    if (tv == NULL) {
        return -1;
    }

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    /* FILETIME is 100-nanosecond intervals since Jan 1, 1601 */
    /* Convert to Unix epoch (Jan 1, 1970) */
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    /* 116444736000000000 is the number of 100-ns intervals between
     * Jan 1, 1601 and Jan 1, 1970 */
    uli.QuadPart -= 116444736000000000ULL;

    tv->tv_sec = (long)(uli.QuadPart / 10000000ULL);
    tv->tv_usec = (long)((uli.QuadPart % 10000000ULL) / 10);

    return 0;
}

#endif /* DSD_PLATFORM_WIN_NATIVE */
