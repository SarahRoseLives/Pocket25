// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
* dsd_ncurses_menu.c
* DSD-FME ncurses terminal menu system
*
* LWVMOBILE
* 2025-05 DSD-FME Florida Man Edition
*-----------------------------------------------------------------------------*/

#include <dsd-neo/ui/menu_core.h>
#include <dsd-neo/ui/ncurses.h>

#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>
#endif

uint32_t temp_freq = -1;

//testing a few things, going to put this into ncursesMenu
#define WIDTH  36
#define HEIGHT 25

int startx = 0;
int starty = 0;

//ncursesMenu
void
ncursesMenu(dsd_opts* opts, dsd_state* state) {
    // Open the data-driven menu as a nonblocking overlay and return immediately.
    // Decode and the base UI continue to run underneath.
    ui_menu_open_async(opts, state);
}
