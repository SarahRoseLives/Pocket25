// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file exitflag.c
 * @brief Global shutdown signaling flag for DSD-neo.
 *
 * Defines the shared exitflag variable used to signal graceful shutdown
 * across all modules. This flag is set by signal handlers (Ctrl+C) and
 * checked by processing loops throughout the codebase.
 *
 * Previously defined in apps/dsd-cli/main.c, moved here so libraries
 * don't depend on the app target for this symbol.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

volatile uint8_t exitflag = 0;

#ifdef __cplusplus
}
#endif
