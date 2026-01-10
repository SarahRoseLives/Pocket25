// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief M17 protocol lookup tables and constants.
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * M17 protocol tables and constants.
 *
 * Exposes shared lookup tables (scrambler sequence, base-40 alphabet,
 * puncture patterns) used by the M17 encoder/decoder.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Scrambler sequence used for M17 LSF/stream frames (length 369 bits). */
extern const uint8_t m17_scramble[369];

/* Base-40 alphabet used for CSD/CALLSIGN encoding/decoding. */
extern const char b40[41];

/* Puncture patterns used by LSF depuncturing logic. */
extern const uint8_t p1[62];
extern const uint8_t p3[62];

#ifdef __cplusplus
}
#endif
