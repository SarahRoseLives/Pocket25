// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Basic API smoke test for version reporting.
 */
#include <assert.h>
#include <string.h>
#include "mbelib-neo/mbelib.h"

/**
 * @brief Test entry: verifies mbe_printVersion matches MBELIB_VERSION.
 */
int
main(void) {
    char ver[32] = {0};
    mbe_printVersion(ver);
    assert(strcmp(ver, MBELIB_VERSION) == 0);
    return 0;
}
