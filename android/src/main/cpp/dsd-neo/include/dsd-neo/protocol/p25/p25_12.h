// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief P25 Phase 1 1/2-rate trellis decoder interfaces.
 *
 * Declares the lightweight 1/2-rate trellis decoders implemented in
 * `src/protocol/p25/p25_12.c`.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int p25_12(uint8_t* input, uint8_t treturn[12]);
int p25_12_soft(uint8_t* input, const uint8_t* reliab98, uint8_t treturn[12]);

#ifdef __cplusplus
}
#endif
