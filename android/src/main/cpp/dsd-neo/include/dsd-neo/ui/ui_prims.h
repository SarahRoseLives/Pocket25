// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief UI primitives for the ncurses-based terminal frontend.
 *
 * Window helpers, transient status utilities, lightweight drawing helpers,
 * and a gamma LUT for density visualizations.
 */

#pragma once

#include <dsd-neo/platform/curses_compat.h>
#include <stddef.h>
#include <time.h>

// `curses_compat.h` provides the `WINDOW` type.

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Create a boxed ncurses window with keypad + nonblocking input. */
WINDOW* ui_make_window(int h, int w, int y, int x);
/** @brief Destroy a window and null the caller's pointer. */
void ui_destroy_window(WINDOW** win);

/** @brief Commit all pending window updates to the physical screen (double buffering). */
void ui_commit_frame(void);

/** @brief Set a transient status/footer message (printf-style). */
void ui_statusf(const char* fmt, ...);
/** @brief Copy active status into buf when not expired at `now`; returns 1 if copied. */
int ui_status_peek(char* buf, size_t n, time_t now);
/** @brief Clear status when expired at `now` (no-op if still active). */
void ui_status_clear_if_expired(time_t now);

/** @brief Draw a horizontal rule to end of line on stdscr. */
void ui_print_hr(void);
/** @brief Draw a section header prefixing the given title. */
void ui_print_header(const char* title);
/** @brief Draw a left border using the primary color. */
void ui_print_lborder(void);
/** @brief Draw a left border using the active/green color. */
void ui_print_lborder_green(void);
/** @brief Map IDEN nibble to color pair (21..28) for trunking displays. */
short ui_iden_color_pair(int iden);

/** @brief Map [0,1] -> [0,1] brightness using sqrt gamma LUT. */
double ui_gamma_map01(double f);

#ifdef __cplusplus
}
#endif
