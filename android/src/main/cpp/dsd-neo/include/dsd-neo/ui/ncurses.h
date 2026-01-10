// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Legacy ncurses UI entrypoints.
 *
 * Declares the ncurses UI lifecycle and input handlers implemented in
 * `src/ui/terminal/`.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ncursesOpen(dsd_opts* opts, dsd_state* state);
void ncursesPrinter(dsd_opts* opts, dsd_state* state);
void ncursesMenu(dsd_opts* opts, dsd_state* state);
void ncursesClose(void);

uint8_t ncurses_input_handler(dsd_opts* opts, dsd_state* state, int c);

#ifdef __cplusplus
}
#endif
