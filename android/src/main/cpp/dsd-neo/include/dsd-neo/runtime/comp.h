// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief qsort() comparator helpers.
 *
 * Declares the float comparator implemented in `src/runtime/comp.c`.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int comp(const void* a, const void* b);

#ifdef __cplusplus
}
#endif
