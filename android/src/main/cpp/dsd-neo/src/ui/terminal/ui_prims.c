// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * ui_prims.c
 * ncurses UI primitives shared by menu framework and screen panels
 */

#include <dsd-neo/platform/curses_compat.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dsd-neo/ui/ui_prims.h>

// ---------------- Window helpers ----------------

WINDOW*
ui_make_window(int h, int w, int y, int x) {
    WINDOW* win = newwin(h, w, y, x);
    if (win != NULL) {
        keypad(win, TRUE);
        wtimeout(win, 0); // non-blocking by default
        box(win, 0, 0);
        wnoutrefresh(win);
    }
    return win;
}

void
ui_destroy_window(WINDOW** win) {
    if (win && *win) {
        delwin(*win);
        *win = NULL;
    }
}

void
ui_commit_frame(void) {
    doupdate();
}

// ---------------- Status message ----------------

static char s_status_msg[256];
static time_t s_status_expire = 0;

void
ui_statusf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status_msg, sizeof s_status_msg, fmt, ap); // NOLINT
    va_end(ap);
    s_status_expire = time(NULL) + 3; // ~3 seconds visibility
}

int
ui_status_peek(char* buf, size_t n, time_t now) {
    if (!buf || n == 0) {
        return 0;
    }
    if (s_status_msg[0] == '\0') {
        return 0;
    }
    if (now >= s_status_expire) {
        return 0;
    }
    // copy but keep message for this frame; caller may clear after
    snprintf(buf, n, "%s", s_status_msg);
    return 1;
}

void
ui_status_clear_if_expired(time_t now) {
    if (s_status_msg[0] != '\0' && now >= s_status_expire) {
        s_status_msg[0] = '\0';
    }
}

// ---------------- Drawing helpers ----------------

void
ui_print_hr(void) {
    int rows = 0, cols = 80;
    getmaxyx(stdscr, rows, cols);
    (void)rows;
    if (cols < 1 || rows < 1) {
        cols = 80;
    }
    int y = 0, x = 0;
    getyx(stdscr, y, x);
    (void)x;
    mvhline(y, 0, '-', cols);
    if (y + 1 < rows) {
        move(y + 1, 0);
    } else {
        addch('\n');
    }
}

void
ui_print_header(const char* title) {
    int rows = 0, cols = 80;
    getmaxyx(stdscr, rows, cols);
    (void)rows;
    if (cols < 4) {
        cols = 80;
    }
    const char* t = (title && *title) ? title : "";
    int y = 0, x = 0;
    getyx(stdscr, y, x);
    (void)x;
    addstr("--");
    addstr(t);
    int used = 2 + (int)strlen(t);
    if (used < cols) {
        mvhline(y, used, '-', cols - used);
    }
    if (y + 1 < rows) {
        move(y + 1, 0);
    } else {
        addch('\n');
    }
}

void
ui_print_lborder(void) {
    attr_t saved_attrs = 0;
    short saved_pair = 0;
    attr_get(&saved_attrs, &saved_pair, NULL);
    attron(COLOR_PAIR(4));
    addch('|');
    attr_set(saved_attrs, saved_pair, NULL);
}

void
ui_print_lborder_green(void) {
    attr_t saved_attrs = 0;
    short saved_pair = 0;
    attr_get(&saved_attrs, &saved_pair, NULL);
    attron(COLOR_PAIR(3));
    addch('|');
    attr_set(saved_attrs, saved_pair, NULL);
}

short
ui_iden_color_pair(int iden) {
    if (iden < 0) {
        iden = 0;
    }
    int idx = iden & 7;
    return (short)(21 + idx);
}

// ---------------- Gamma LUT ----------------

static int s_gamma_ready = 0;
static float s_gamma_lut[256];

static inline void
gamma_init_once_local(void) {
    if (s_gamma_ready) {
        return;
    }
    for (int i = 0; i < 256; i++) {
        float x = (float)i / 255.0f;
        s_gamma_lut[i] = sqrtf(x); // gamma 0.5
    }
    s_gamma_ready = 1;
}

double
ui_gamma_map01(double f) {
    if (f <= 0.0) {
        return 0.0;
    }
    if (f >= 1.0) {
        return 1.0;
    }
    gamma_init_once_local();
    int idx = (int)lrint(f * 255.0);
    if (idx < 0) {
        idx = 0;
    }
    if (idx > 255) {
        idx = 255;
    }
    return (double)s_gamma_lut[idx];
}
