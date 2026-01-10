// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Parse frequency strings into Hz (supports K/M/G suffixes).
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse a frequency string into Hz.
 *
 * Accepts an optional K/M/G suffix (case-insensitive). Returns 0 for invalid or empty input.
 * Clamps to UINT32_MAX on overflow and rounds to the nearest Hz.
 */
uint32_t dsd_parse_freq_hz(const char* s);

#ifdef __cplusplus
}
#endif
