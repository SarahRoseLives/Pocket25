// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Common lightweight math utilities used across DSP modules.
 */

#pragma once

#include <math.h>
#include <stdint.h>

static inline long long
dsd_neo_llrint_long_double(long double x) {
#if defined(_MSC_VER)
    return llrint((double)x);
#else
    return llrintl(x);
#endif
}

/**
 * @brief Saturate a 32-bit integer to the 16-bit signed range.
 *
 * Clamps the provided 32-bit value to the inclusive range [-32768, 32767]
 * and returns it as a 16-bit signed integer.
 *
 * @param x 32-bit integer input value.
 * @return Clamped 16-bit signed integer.
 */
static inline int16_t
sat16(int32_t x) {
    if (x > 32767) {
        return 32767;
    }
    if (x < -32768) {
        return -32768;
    }
    return (int16_t)x;
}

/**
 * @brief Compute the greatest common divisor using the Euclidean algorithm.
 *
 * Handles negative inputs by using their absolute values. If both inputs are
 * zero, returns 1.
 *
 * @param a First integer.
 * @param b Second integer.
 * @return Greatest common divisor of |a| and |b|, or 1 if both are zero.
 */
static inline int
gcd_int(int a, int b) {
    if (a < 0) {
        a = -a;
    }
    if (b < 0) {
        b = -b;
    }
    while (b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    return (a == 0) ? 1 : a;
}

/**
 * @brief Normalized sinc function.
 *
 * Computes sinc(x) = sin(pi*x)/(pi*x) with the special case sinc(0) = 1
 * implemented to avoid division by zero.
 *
 * @param x Input value.
 * @return Normalized sinc evaluated at x.
 */
static inline double
dsd_neo_sinc(double x) {
    if (x == 0.0) {
        return 1.0;
    }
    const double kPiLocal = 3.14159265358979323846;
    return sin(kPiLocal * x) / (kPiLocal * x);
}

/**
 * @brief Fast atan2 approximation for 64-bit inputs (Q14 output).
 *
 * Matches the Costas-loop detector implementation: uses a simple
 * piecewise-linear approximation expressed in long double to keep
 * precision and dynamic range. The returned angle is scaled such that
 * pi corresponds to 1<<14.
 *
 * @param y Imaginary component (numerator).
 * @param x Real component (denominator).
 * @return Approximate angle in Q14, where pi == 1<<14.
 */
static inline int
dsd_neo_fast_atan2(int64_t y, int64_t x) {
    const int pi4 = (1 << 12);
    const int pi34 = 3 * (1 << 12);
    if (x == 0 && y == 0) {
        return 0;
    }
    long double yabs = (y < 0) ? (long double)(-y) : (long double)y;
    if (x >= 0) {
        long double denom = (long double)x + yabs;
        if (denom == 0.0L) {
            return 0;
        }
        /* angle = pi/4 * (2*|y|)/(x+|y|) */
        long double t = ((2.0L * yabs) / denom) * (long double)pi4;
        int angle = (int)dsd_neo_llrint_long_double(t);
        return (y < 0) ? -angle : angle;
    } else {
        long double denom = yabs - (long double)x; /* strictly > 0 for x<0 */
        if (denom == 0.0L) {
            return (y < 0) ? -pi34 : pi34;
        }
        /* angle = 3pi/4 - pi/4 * (x+|y|)/( |y|-x ) */
        long double t = (long double)pi34 - ((long double)pi4 * (((long double)x + yabs) / denom));
        int angle = (int)dsd_neo_llrint_long_double(t);
        return (y < 0) ? -angle : angle;
    }
}
