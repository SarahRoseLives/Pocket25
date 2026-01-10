// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Trellis decoder helpers.
 *
 * Declares the trellis decoder implemented in `src/core/util/dsd_misc.c`.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void trellis_decode(uint8_t result[], const uint8_t source[], int result_len);

#ifdef __cplusplus
}
#endif
