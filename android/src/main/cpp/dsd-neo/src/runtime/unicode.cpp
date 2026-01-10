// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Unicode/ASCII fallback utilities implementation.
 */

#include <dsd-neo/runtime/unicode.h>

#include <dsd-neo/platform/posix_compat.h>

#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if DSD_PLATFORM_WIN_NATIVE
#include <windows.h>
#endif

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__ANDROID__)
#define HAVE_LANGINFO 1
#include <langinfo.h>
#endif

static int g_unicode_cached = 0;
static int g_unicode_supported = 0;
static int g_locale_inited = 0;

static char g_cached_locale[128] = {0};
#if DSD_PLATFORM_WIN_NATIVE
static unsigned int g_cached_console_cp = 0;
#endif

static int
env_truthy(const char* name) {
    const char* v = getenv(name);
    if (!v || !*v) {
        return 0;
    }
    return (*v == '1' || *v == 'y' || *v == 'Y' || *v == 't' || *v == 'T');
}

static int
str_icontains(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) {
        return 0;
    }
    size_t nlen = strlen(needle);
    for (size_t i = 0; haystack[i]; i++) {
        size_t j = 0;
        for (; j < nlen; j++) {
            unsigned char hc = (unsigned char)haystack[i + j];
            unsigned char nc = (unsigned char)needle[j];
            if (!hc) {
                break;
            }
            if (tolower(hc) != tolower(nc)) {
                break;
            }
        }
        if (j == nlen) {
            return 1;
        }
    }
    return 0;
}

static int
locale_is_utf8(void) {
#if HAVE_LANGINFO
    const char* codeset = nl_langinfo(CODESET);
    if (codeset && (dsd_strcasecmp(codeset, "UTF-8") == 0 || dsd_strcasecmp(codeset, "UTF8") == 0)) {
        return 1;
    }
#endif

    const char* loc = setlocale(LC_CTYPE, NULL);
    if (loc && (str_icontains(loc, "UTF-8") || str_icontains(loc, "UTF8") || str_icontains(loc, "65001"))) {
        return 1;
    }
    return 0;
}

void
dsd_unicode_init_locale(void) {
    if (g_locale_inited) {
        return;
    }
    g_locale_inited = 1;

    /* Initialize locale from the user's environment first. */
    setlocale(LC_CTYPE, "");

    if (env_truthy("DSD_FORCE_ASCII")) {
        return;
    }

#if DSD_PLATFORM_WIN_NATIVE
    /* Prefer UTF-8 console code pages on native Windows terminals (best effort). */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (locale_is_utf8()) {
        g_unicode_cached = 0;
        return;
    }

#if DSD_PLATFORM_WIN_NATIVE
    static const char* const candidates[] = {".UTF8", ".UTF-8", "C.UTF-8", "en_US.UTF-8", NULL};
#else
    static const char* const candidates[] = {"C.UTF-8", "C.UTF8", "en_US.UTF-8", "en_US.UTF8", NULL};
#endif
    for (int i = 0; candidates[i] != NULL; i++) {
        if (setlocale(LC_CTYPE, candidates[i]) != NULL && locale_is_utf8()) {
            break;
        }
    }

    /* Locale/codepage changes can affect support detection; recompute on next query. */
    g_unicode_cached = 0;
}

int
dsd_unicode_supported(void) {
    if (env_truthy("DSD_FORCE_ASCII")) {
        g_unicode_supported = 0;
        g_unicode_cached = 1;
        return 0;
    }
    if (env_truthy("DSD_FORCE_UTF8")) {
        g_unicode_supported = 1;
        g_unicode_cached = 1;
        return 1;
    }

    dsd_unicode_init_locale();

    const char* loc = setlocale(LC_CTYPE, NULL);
    if (!loc) {
        loc = "";
    }
#if DSD_PLATFORM_WIN_NATIVE
    unsigned int cp = GetConsoleOutputCP();
    if (g_unicode_cached && strcmp(loc, g_cached_locale) == 0 && cp == g_cached_console_cp) {
        return g_unicode_supported;
    }
    g_cached_console_cp = cp;
#else
    if (g_unicode_cached && strcmp(loc, g_cached_locale) == 0) {
        return g_unicode_supported;
    }
#endif
    strncpy(g_cached_locale, loc, sizeof(g_cached_locale) - 1);
    g_cached_locale[sizeof(g_cached_locale) - 1] = '\0';

    g_unicode_supported = locale_is_utf8() ? 1 : 0;
#if DSD_PLATFORM_WIN_NATIVE
    /* Even without a UTF-8 CRT locale, a UTF-8 console code page can render UTF-8 bytes. */
    if (g_unicode_supported == 0 && cp == CP_UTF8) {
        g_unicode_supported = 1;
    }
#endif
    g_unicode_cached = 1;
    return g_unicode_supported;
}

const char*
dsd_degrees_glyph(void) {
    return dsd_unicode_supported() ? "\xC2\xB0" : " deg";
}

char*
dsd_ascii_fallback(const char* in, char* out, size_t out_sz) {
    if (!in || !out || out_sz == 0) {
        return out;
    }
    if (dsd_unicode_supported()) {
        /* Copy with truncation */
        size_t n = strlen(in);
        if (n >= out_sz) {
            n = out_sz - 1;
        }
        memcpy(out, in, n);
        out[n] = '\0';
        return out;
    }
    /* Replace a small set of common glyphs with ASCII */
    size_t oi = 0;
    for (size_t i = 0; in[i] && oi + 1 < out_sz;) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) {
            out[oi++] = (char)c;
            i++;
            continue;
        }
        /* Multi-byte sequences we recognize */
        if ((unsigned char)in[i] == 0xE2) {
            /* E2 89 88: ≈  | E2 80 93: – | E2 80 94: — | E2 80 A6: … */
            if ((unsigned char)in[i + 1] == 0x89 && (unsigned char)in[i + 2] == 0x88) {
                out[oi++] = '~';
                i += 3;
                continue;
            }
            if ((unsigned char)in[i + 1] == 0x80 && (unsigned char)in[i + 2] == 0x93) {
                out[oi++] = '-';
                i += 3;
                continue;
            }
            if ((unsigned char)in[i + 1] == 0x80 && (unsigned char)in[i + 2] == 0x94) {
                out[oi++] = '-';
                i += 3;
                continue;
            }
            if ((unsigned char)in[i + 1] == 0x80 && (unsigned char)in[i + 2] == 0xA6) {
                if (oi + 3 < out_sz) {
                    out[oi++] = '.';
                    out[oi++] = '.';
                    out[oi++] = '.';
                }
                i += 3;
                continue;
            }
        } else if ((unsigned char)in[i] == 0xC2) {
            /* C2 B0: ° | C2 B5: µ */
            if ((unsigned char)in[i + 1] == 0xB0) {
                const char* rep = " deg";
                for (size_t k = 0; rep[k] && oi + 1 < out_sz; ++k) {
                    out[oi++] = rep[k];
                }
                i += 2;
                continue;
            }
            if ((unsigned char)in[i + 1] == 0xB5) {
                out[oi++] = 'u';
                i += 2;
                continue;
            }
        } else if ((unsigned char)in[i] == 0xC3) {
            /* C3 97: × */
            if ((unsigned char)in[i + 1] == 0x97) {
                out[oi++] = 'x';
                i += 2;
                continue;
            }
        }
        /* Unknown non-ASCII: replace with '?' */
        out[oi++] = '?';
        /* Skip continuation bytes */
        if ((c & 0xE0) == 0xC0) {
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            i += 4;
        } else {
            i++;
        }
    }
    out[oi] = '\0';
    return out;
}
