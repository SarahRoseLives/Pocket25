// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Menu rendering and layout functions for menu subsystem.
 *
 * This file provides drawing, layout, and visibility helper functions
 * that are shared across the menu subsystem.
 */

#include <dsd-neo/platform/curses_compat.h>
#include "menu_internal.h"

#include <string.h>
#include <time.h>

#include <dsd-neo/ui/ui_prims.h>

// ---- Visibility helpers ----

int
ui_is_enabled(const NcMenuItem* it, void* ctx) {
    if (!it) {
        return 0;
    }
    if (it->is_enabled) {
        return it->is_enabled(ctx) ? 1 : 0;
    }
    // If no explicit predicate, but this item has a submenu, hide it when the submenu is empty
    if (it->submenu && it->submenu_len > 0) {
        return ui_submenu_has_visible(it->submenu, it->submenu_len, ctx);
    }
    return 1;
}

int
ui_submenu_has_visible(const NcMenuItem* items, size_t n, void* ctx) {
    if (!items || n == 0) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (ui_is_enabled(&items[i], ctx)) {
            return 1;
        }
    }
    return 0;
}

int
ui_next_enabled(const NcMenuItem* items, size_t n, void* ctx, int from, int dir) {
    if (!items || n == 0) {
        return 0;
    }
    int idx = from;
    for (size_t k = 0; k < n; k++) {
        idx = (idx + ((dir > 0) ? 1 : -1) + (int)n) % (int)n;
        if (ui_is_enabled(&items[idx], ctx)) {
            return idx;
        }
    }
    return from;
}

// ---- Render helpers ----

void
ui_draw_menu(WINDOW* menu_win, const NcMenuItem* items, size_t n, int hi, void* ctx) {
    int x = 2;
    int y = 1;
    werase(menu_win);
    box(menu_win, 0, 0);
    int mh = 0, mw = 0;
    getmaxyx(menu_win, mh, mw);
    for (size_t i = 0; i < n; i++) {
        if (!ui_is_enabled(&items[i], ctx)) {
            // Hide items that are not enabled for current context
            continue;
        }
        if ((int)i == hi) {
            wattron(menu_win, A_REVERSE);
        }
        char dyn[128];
        const char* lab = items[i].label ? items[i].label : items[i].id;
        if (items[i].label_fn) {
            const char* got = items[i].label_fn(ctx, dyn, sizeof dyn);
            if (got && *got) {
                lab = got;
            }
        }
        mvwprintw(menu_win, y++, x, "%s", lab);
        wattroff(menu_win, A_REVERSE);
    }
    // ensure a blank spacer line above footer to avoid looking like an item
    mvwhline(menu_win, mh - 5, 1, ' ', mw - 2);
    // footer help (split across two lines to avoid overflow)
    mvwprintw(menu_win, mh - 4, x, "Arrows: move  Enter: select");
    mvwprintw(menu_win, mh - 3, x, "h: help  Esc/q: back");
    // transient status
    time_t now = time(NULL);
    char sline[256];
    if (ui_status_peek(sline, sizeof sline, now)) {
        // clear line then print
        mvwhline(menu_win, mh - 2, 1, ' ', mw - 2);
        mvwprintw(menu_win, mh - 2, x, "Status: %s", sline);
    } else {
        ui_status_clear_if_expired(now);
    }
    wnoutrefresh(menu_win);
}

int
ui_visible_count_and_maxlab(const NcMenuItem* items, size_t n, void* ctx, int* out_maxlab) {
    int vis = 0;
    int maxlab = 0;
    for (size_t i = 0; i < n; i++) {
        if (!ui_is_enabled(&items[i], ctx)) {
            continue;
        }
        const char* lab = items[i].label ? items[i].label : items[i].id;
        char dyn[128];
        if (items[i].label_fn) {
            const char* got = items[i].label_fn(ctx, dyn, sizeof dyn);
            if (got && *got) {
                lab = got;
            }
        }
        int L = (int)strlen(lab);
        if (L > maxlab) {
            maxlab = L;
        }
        vis++;
    }
    if (out_maxlab) {
        *out_maxlab = maxlab;
    }
    return vis;
}

void
ui_overlay_layout(UiMenuFrame* f, void* ctx) {
    if (!f || !f->items || f->n == 0) {
        return;
    }
    const char* f1 = "Arrows: move  Enter: select";
    const char* f2 = "h: help  Esc/q: back";
    int pad_x = 2;
    int maxlab = 0;
    int vis = ui_visible_count_and_maxlab(f->items, f->n, ctx, &maxlab);
    int width = pad_x + ((maxlab > 0) ? maxlab : 1);
    int f1w = pad_x + (int)strlen(f1);
    int f2w = pad_x + (int)strlen(f2);
    if (f1w > width) {
        width = f1w;
    }
    if (f2w > width) {
        width = f2w;
    }
    width += 2; // borders
    int height = vis + 6;
    if (height < 8) {
        height = 8;
    }
    int term_h = 24, term_w = 80;
    getmaxyx(stdscr, term_h, term_w);
    if (width > term_w - 2) {
        width = term_w - 2;
        if (width < 10) {
            width = 10;
        }
    }
    if (height > term_h - 2) {
        height = term_h - 2;
        if (height < 7) {
            height = 7;
        }
    }
    int my = (term_h - height) / 2;
    int mx = (term_w - width) / 2;
    if (my < 0) {
        my = 0;
    }
    if (mx < 0) {
        mx = 0;
    }
    f->h = height;
    f->w = width;
    f->y = my;
    f->x = mx;
}

void
ui_overlay_ensure_window(UiMenuFrame* f) {
    if (!f) {
        return;
    }
    if (!f->win) {
        f->win = ui_make_window(f->h, f->w, f->y, f->x);
        keypad(f->win, TRUE);
        wtimeout(f->win, 0);
    }
}

void
ui_overlay_recreate_if_needed(UiMenuFrame* f) {
    if (!f || !f->win) {
        return;
    }
    int cur_h = 0, cur_w = 0;
    int cur_y = 0, cur_x = 0;
    getmaxyx(f->win, cur_h, cur_w);
    getbegyx(f->win, cur_y, cur_x);
    if (cur_h != f->h || cur_w != f->w || cur_y != f->y || cur_x != f->x) {
        delwin(f->win);
        f->win = NULL;
    }
}
