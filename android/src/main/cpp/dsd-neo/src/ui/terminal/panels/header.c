// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * header.c
 * Header panel renderer for the ncurses terminal UI
 */

#include <dsd-neo/platform/curses_compat.h>
#include <time.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/runtime/git_ver.h>
#include <dsd-neo/ui/panels.h>
#include <dsd-neo/ui/ui_prims.h>

void
ui_panel_header_render(dsd_opts* opts, dsd_state* state) {
    (void)state;
    if (!opts) {
        return;
    }
    // header banner
    if (opts->ncurses_compact == 1) {
        ui_print_hr();
        printw("| Digital Speech Decoder: DSD-neo %s (%s)  | Enter=Menu  q=Quit\n", GIT_TAG, GIT_HASH);
        ui_print_hr();
    } else {
        attron(COLOR_PAIR(6));
        ui_print_hr();
        printw("| Digital Speech Decoder: DSD-neo %s (%s)  | Enter=Menu  q=Quit\n", GIT_TAG, GIT_HASH);
        ui_print_hr();
        attroff(COLOR_PAIR(6));
        attron(COLOR_PAIR(4));
    }
    // fix color/pair issue when compact and trunking enabled
    if (opts->ncurses_compact == 1 && opts->p25_trunk == 1) {
        attron(COLOR_PAIR(4));
    }
}
