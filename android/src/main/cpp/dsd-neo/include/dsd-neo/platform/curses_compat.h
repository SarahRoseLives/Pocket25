// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

/**
 * @file
 * @brief Cross-platform curses include wrapper.
 *
 * Provides a unified include for ncurses (POSIX) and PDCurses (Windows),
 * along with any compatibility macros needed to bridge API differences.
 */

#include <dsd-neo/platform/platform.h>

#if defined(DSD_USE_PDCURSES)
/* PDCurses backend */
#ifndef PDC_WIDE
#define PDC_WIDE
#endif
#include <curses.h>
#else
/* ncurses backend */
#include <curses.h>
#endif

/*
 * Compatibility macros for API differences between ncurses and PDCurses
 */

/* PDCurses mouse handling differs slightly - provide unified macros if needed */
#if defined(DSD_USE_PDCURSES) && defined(PDC_KEY_MODIFIER_SHIFT)
/* PDCurses-specific key modifier handling */
#endif

/* Ensure common constants are defined */
#ifndef A_NORMAL
#define A_NORMAL 0
#endif

/*
 * Color pair limits - PDCurses supports up to 256 color pairs,
 * ncurses typically supports more. Use common subset if needed.
 */
#ifndef DSD_MAX_COLOR_PAIRS
#if defined(DSD_USE_PDCURSES)
#define DSD_MAX_COLOR_PAIRS 256
#else
#define DSD_MAX_COLOR_PAIRS COLOR_PAIRS
#endif
#endif

/*
 * Terminal resize handling differs between ncurses and PDCurses.
 * PDCurses uses Windows Console API internally.
 */
#if defined(DSD_USE_PDCURSES)
/* PDCurses may need explicit resize handling via resize_term() */
#ifndef DSD_CURSES_NEEDS_EXPLICIT_RESIZE
#define DSD_CURSES_NEEDS_EXPLICIT_RESIZE 1
#endif
#else
#ifndef DSD_CURSES_NEEDS_EXPLICIT_RESIZE
#define DSD_CURSES_NEEDS_EXPLICIT_RESIZE 0
#endif
#endif

static inline void
dsd_curses_set_escdelay(int delay_ms) {
#if defined(DSD_USE_PDCURSES)
    /* PDCurses does not provide set_escdelay(); degrade gracefully. */
    (void)delay_ms;
#else
    set_escdelay(delay_ms);
#endif
}
