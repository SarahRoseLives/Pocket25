// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Core compile-time constants and lightweight helper macros.
 *
 * Provides common sample-rate constants and portable boolean/UNUSED helpers
 * as a shared lightweight header.
 */

#pragma once

// Base I/O sample rates
#define SAMPLE_RATE_IN  48000 // 48 kHz input
#define SAMPLE_RATE_OUT 8000  // 8 kHz output

// Provide portable boolean-style macros for legacy code paths that used
// ncurses-provided TRUE/FALSE. Keep these independent from ncurses to avoid
// leaking curses into non-UI translation units.
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// Lightweight UNUSED helpers used throughout the codebase
#define UNUSED(x)                   (void)(x)
#define UNUSED2(x1, x2)             (UNUSED(x1), UNUSED(x2))
#define UNUSED3(x1, x2, x3)         (UNUSED(x1), UNUSED(x2), UNUSED(x3))
#define UNUSED4(x1, x2, x3, x4)     (UNUSED(x1), UNUSED(x2), UNUSED(x3), UNUSED(x4))
#define UNUSED5(x1, x2, x3, x4, x5) (UNUSED(x1), UNUSED(x2), UNUSED(x3), UNUSED(x4), UNUSED(x5))
