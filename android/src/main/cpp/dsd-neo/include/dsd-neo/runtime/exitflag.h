// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Global shutdown signaling flag for DSD-neo.
 *
 * Declares the shared `exitflag` variable used to signal graceful shutdown
 * across all modules. This flag is set by signal handlers (Ctrl+C) and checked
 * by processing loops throughout the codebase.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint8_t exitflag;

#ifdef __cplusplus
}
#endif
