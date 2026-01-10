// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Map a dibit value (0..3) to a nominal 4-level symbol amplitude.
 *
 * The project uses a consistent dibit ordering across protocols:
 * - 0 -> +1
 * - 1 -> +3
 * - 2 -> -1
 * - 3 -> -3
 *
 * This helper is used for symbol-capture playback where the input stream is
 * already digitized into dibits (e.g. `.bin` symbol capture files).
 */
float dsd_symbol_level_from_dibit(uint8_t dibit);

#ifdef __cplusplus
}
#endif
