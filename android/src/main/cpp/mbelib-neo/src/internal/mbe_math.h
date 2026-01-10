// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Lightweight math helpers for performance-sensitive code paths.
 */

#ifndef MBELIB_NEO_INTERNAL_MBE_MATH_H
#define MBELIB_NEO_INTERNAL_MBE_MATH_H

#include <math.h>

/**
 * @brief Compute both sine and cosine of an angle.
 *
 * Uses a combined intrinsic when available for better performance and
 * accuracy; otherwise falls back to separate `sinf` and `cosf` calls.
 *
 * @param x Input angle in radians.
 * @param s Output pointer receiving `sinf(x)`.
 * @param c Output pointer receiving `cosf(x)`.
 */
static inline void
mbe_sincosf(float x, float* s, float* c) {
#if defined(__has_builtin)
#if __has_builtin(__builtin_sincosf)
    __builtin_sincosf(x, s, c);
    return;
#endif
#elif defined(__GNUC__) && !defined(__clang__)
    /* GCC provides __builtin_sincosf */
    __builtin_sincosf(x, s, c);
    return;
#endif
    *s = sinf(x);
    *c = cosf(x);
}

#endif /* MBELIB_NEO_INTERNAL_MBE_MATH_H */
