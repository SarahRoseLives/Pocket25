// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/dsp/symbol_levels.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>

static int
expect_float_eq(const char* label, float actual, float expected) {
    const float eps = 1e-6f;
    if (fabsf(actual - expected) > eps) {
        fprintf(stderr, "FAIL: %s: got %.6f expected %.6f\n", label, actual, expected);
        return 0;
    }
    return 1;
}

int
main(void) {
    int ok = 1;
    ok &= expect_float_eq("dibit 0 -> +1", dsd_symbol_level_from_dibit(0), 1.0f);
    ok &= expect_float_eq("dibit 1 -> +3", dsd_symbol_level_from_dibit(1), 3.0f);
    ok &= expect_float_eq("dibit 2 -> -1", dsd_symbol_level_from_dibit(2), -1.0f);
    ok &= expect_float_eq("dibit 3 -> -3", dsd_symbol_level_from_dibit(3), -3.0f);
    return ok ? 0 : 1;
}
