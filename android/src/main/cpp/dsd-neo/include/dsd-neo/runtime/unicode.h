// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Unicode/ASCII fallback utilities for terminal output.
 */

#ifndef DSD_NEO_UNICODE_H
#define DSD_NEO_UNICODE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return 1 if UTF-8 output is likely supported, else 0 (cached). */
int dsd_unicode_supported(void);

/**
 * @brief Best-effort initialization to make UTF-8 output usable.
 *
 * Attempts to select a UTF-8 locale (LC_CTYPE) and, on native Windows, set the
 * console code page to UTF-8. Safe to call multiple times.
 */
void dsd_unicode_init_locale(void);

/** @brief Convenience helper to pick Unicode or ASCII string based on support. */
static inline const char*
dsd_unicode_or_ascii(const char* unicode_str, const char* ascii_str) {
    return dsd_unicode_supported() ? unicode_str : ascii_str;
}

/** @brief Degree glyph string with ASCII fallback ("\xC2\xB0" vs " deg"). */
const char* dsd_degrees_glyph(void);

/**
 * @brief Convert a UTF-8 string to ASCII-safe form in out buffer.
 * @return Pointer to out buffer.
 */
char* dsd_ascii_fallback(const char* in, char* out, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_UNICODE_H */
