// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Prompt, chooser, and help overlay implementations for menu subsystem.
 *
 * This file owns the state for string/int/double prompts, the generic chooser overlay,
 * and the help overlay. It provides the public API declared in menu_prompts.h.
 */

#include "menu_prompts.h"
#include <dsd-neo/platform/curses_compat.h>
#include "menu_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/ui/keymap.h>
#include <dsd-neo/ui/ui_prims.h>

// ---- Prompt overlay state ----
typedef struct {
    int active;
    const char* title;
    WINDOW* win;
    // string mode fields
    char* buf;
    size_t cap;
    size_t len;
    void (*on_done_str)(void* user, const char* text); // NULL text indicates cancel/empty
    void* user;
} UiPrompt;

static UiPrompt g_prompt = {0};

// ---- Typed prompt context structures ----
typedef struct {
    void (*cb)(void*, int, int);
    void* user;
} PromptIntCtx;

typedef struct {
    void (*cb)(void*, int, double);
    void* user;
} PromptDblCtx;

// ---- Chooser overlay state ----
typedef struct {
    int active;
    const char* title;
    const char* const* items;
    int count;
    int sel;
    WINDOW* win;
    void (*on_done)(void* user, int sel);
    void* user;
} UiChooser;

static UiChooser g_chooser = {0};

// ---- Help overlay state ----
typedef struct {
    int active;
    const char* text;
    WINDOW* win;
} UiHelp;

static UiHelp g_help = {0};

// ---- Prompt implementations ----

void
ui_prompt_close_all(void) {
    // If an active prompt is being closed without an explicit completion,
    // signal a cancel to allow user context cleanup.
    if (g_prompt.active && g_prompt.on_done_str) {
        void (*cb)(void*, const char*) = g_prompt.on_done_str;
        void* up = g_prompt.user;
        g_prompt.on_done_str = NULL; // prevent double-callback
        cb(up, NULL);
    }

    if (g_prompt.win) {
        delwin(g_prompt.win);
        g_prompt.win = NULL;
    }
    if (g_prompt.buf) {
        free(g_prompt.buf);
        g_prompt.buf = NULL;
    }
    memset(&g_prompt, 0, sizeof(g_prompt));
}

void
ui_prompt_open_string_async(const char* title, const char* prefill, size_t cap,
                            void (*on_done)(void* user, const char* text), void* user) {
    ui_prompt_close_all();
    g_prompt.active = 1;
    g_prompt.title = title;
    g_prompt.on_done_str = on_done;
    g_prompt.user = user;
    if (cap < 2) {
        cap = 2;
    }
    g_prompt.buf = (char*)calloc(cap, 1);
    g_prompt.cap = cap;
    g_prompt.len = 0;
    if (!g_prompt.buf) {
        // Allocation failed: immediately signal cancel to ensure user context can be freed.
        if (on_done) {
            on_done(user, NULL);
        }
        g_prompt.active = 0;
        return;
    }
    if (prefill && *prefill) {
        strncpy(g_prompt.buf, prefill, cap - 1);
        g_prompt.buf[cap - 1] = '\0';
        g_prompt.len = strlen(g_prompt.buf);
    }
}

static void
ui_prompt_int_finish(void* u, const char* text) {
    PromptIntCtx* pic = (PromptIntCtx*)u;
    if (!pic) {
        return;
    }
    if (!text || !*text) {
        if (pic->cb) {
            pic->cb(pic->user, 0, 0);
        }
        free(pic);
        return;
    }
    char* end = NULL;
    long v = strtol(text, &end, 10);
    if (!end || *end != '\0') {
        if (pic->cb) {
            pic->cb(pic->user, 0, 0);
        }
    } else {
        if (pic->cb) {
            pic->cb(pic->user, 1, (int)v);
        }
    }
    free(pic);
}

static void
ui_prompt_double_finish(void* u, const char* text) {
    PromptDblCtx* pdc = (PromptDblCtx*)u;
    if (!pdc) {
        return;
    }
    if (!text || !*text) {
        if (pdc->cb) {
            pdc->cb(pdc->user, 0, 0.0);
        }
        free(pdc);
        return;
    }
    char* end = NULL;
    double v = strtod(text, &end);
    if (!end || *end != '\0') {
        if (pdc->cb) {
            pdc->cb(pdc->user, 0, 0.0);
        }
    } else {
        if (pdc->cb) {
            pdc->cb(pdc->user, 1, v);
        }
    }
    free(pdc);
}

void
ui_prompt_open_int_async(const char* title, int initial, void (*cb)(void* user, int ok, int value), void* user) {
    char pre[64];
    snprintf(pre, sizeof pre, "%d", initial);
    PromptIntCtx* pic = (PromptIntCtx*)calloc(1, sizeof(PromptIntCtx));
    if (!pic) {
        // Allocation failed: immediately signal cancel so caller can clean up.
        if (cb) {
            cb(user, 0, 0);
        }
        return;
    }
    pic->cb = cb;
    pic->user = user;
    ui_prompt_open_string_async(title, pre, 64, ui_prompt_int_finish, pic);
}

void
ui_prompt_open_double_async(const char* title, double initial, void (*cb)(void* user, int ok, double value),
                            void* user) {
    char pre[64];
    snprintf(pre, sizeof pre, "%.6f", initial);
    PromptDblCtx* pdc = (PromptDblCtx*)calloc(1, sizeof(PromptDblCtx));
    if (!pdc) {
        // Allocation failed: immediately signal cancel so caller can clean up.
        if (cb) {
            cb(user, 0, 0.0);
        }
        return;
    }
    pdc->cb = cb;
    pdc->user = user;
    ui_prompt_open_string_async(title, pre, 64, ui_prompt_double_finish, pdc);
}

// ---- Prompt active/handle_key/render for menu_core delegation ----

int
ui_prompt_active(void) {
    return g_prompt.active;
}

int
ui_prompt_handle_key(int ch) {
    if (!g_prompt.active) {
        return 0;
    }
    if (ch == KEY_RESIZE) {
#if DSD_CURSES_NEEDS_EXPLICIT_RESIZE
        resize_term(0, 0);
#endif
        if (g_prompt.win) {
            delwin(g_prompt.win);
            g_prompt.win = NULL;
        }
        return 1;
    }
    if (ch == ERR) {
        return 1;
    }
    if (ch == DSD_KEY_ESC || ch == 'q' || ch == 'Q') {
        if (g_prompt.on_done_str) {
            void (*cb)(void*, const char*) = g_prompt.on_done_str;
            void* up = g_prompt.user;
            g_prompt.on_done_str = NULL; // prevent close_all() from calling again
            cb(up, NULL);
        }
        ui_prompt_close_all();
        return 1;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        if (g_prompt.len > 0) {
            g_prompt.buf[--g_prompt.len] = '\0';
        }
        return 1;
    }
    if (ch == 10 || ch == KEY_ENTER || ch == '\r') {
        if (g_prompt.on_done_str) {
            void (*cb)(void*, const char*) = g_prompt.on_done_str;
            void* up = g_prompt.user;
            g_prompt.on_done_str = NULL; // prevent close_all() from calling again
            if (g_prompt.len == 0) {
                cb(up, NULL);
            } else {
                cb(up, g_prompt.buf);
            }
        }
        ui_prompt_close_all();
        return 1;
    }
    if (isprint(ch)) {
        if (g_prompt.len + 1 < g_prompt.cap) {
            g_prompt.buf[g_prompt.len++] = (char)ch;
            g_prompt.buf[g_prompt.len] = '\0';
        }
        return 1;
    }
    return 1;
}

void
ui_prompt_render(void) {
    if (!g_prompt.active) {
        return;
    }
    const char* title = g_prompt.title ? g_prompt.title : "Input";
    int h = 8;
    int w = (int)strlen(title) + 16;
    if (w < 54) {
        w = 54;
    }
    int scr_h = 0, scr_w = 0;
    getmaxyx(stdscr, scr_h, scr_w);
    int py = (scr_h - h) / 2;
    int px = (scr_w - w) / 2;
    if (py < 0) {
        py = 0;
    }
    if (px < 0) {
        px = 0;
    }
    if (!g_prompt.win) {
        g_prompt.win = ui_make_window(h, w, py, px);
        wtimeout(g_prompt.win, 0);
    }
    WINDOW* win = g_prompt.win;
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", title);
    mvwprintw(win, 3, 2, "> %s", g_prompt.buf ? g_prompt.buf : "");
    mvwprintw(win, h - 2, 2, "Enter=OK  Esc/q=Cancel");
    wnoutrefresh(win);
}

// ---- Help implementations ----

void
ui_help_open(const char* help) {
    if (!help || !*help) {
        return;
    }
    g_help.active = 1;
    g_help.text = help;
    if (g_help.win) {
        delwin(g_help.win);
        g_help.win = NULL;
    }
}

void
ui_help_close(void) {
    if (g_help.win) {
        delwin(g_help.win);
        g_help.win = NULL;
    }
    memset(&g_help, 0, sizeof(g_help));
}

int
ui_help_active(void) {
    return g_help.active;
}

int
ui_help_handle_key(int ch) {
    if (!g_help.active) {
        return 0;
    }
    if (ch != ERR) {
        ui_help_close();
    }
    return 1;
}

void
ui_help_render(void) {
    if (!g_help.active) {
        return;
    }
    const char* t = g_help.text ? g_help.text : "";
    int h = 8;
    int w = (int)strlen(t) + 6;
    if (w < 40) {
        w = 40;
    }
    int scr_h = 0, scr_w = 0;
    getmaxyx(stdscr, scr_h, scr_w);
    if (w > scr_w - 2) {
        w = scr_w - 2;
    }
    int hy = (scr_h - h) / 2;
    int hx = (scr_w - w) / 2;
    if (hy < 0) {
        hy = 0;
    }
    if (hx < 0) {
        hx = 0;
    }
    if (!g_help.win) {
        g_help.win = ui_make_window(h, w, hy, hx);
        wtimeout(g_help.win, 0);
    }
    WINDOW* hw = g_help.win;
    werase(hw);
    box(hw, 0, 0);
    mvwprintw(hw, 1, 2, "Help:");
    mvwprintw(hw, 3, 2, "%s", t);
    mvwprintw(hw, h - 2, 2, "Press any key to continue...");
    wnoutrefresh(hw);
}

// ---- Chooser implementations ----

void
ui_chooser_start(const char* title, const char* const* items, int count, void (*on_done)(void*, int), void* user) {
    g_chooser.active = 1;
    g_chooser.title = title;
    g_chooser.items = items;
    g_chooser.count = count;
    g_chooser.sel = 0;
    g_chooser.on_done = on_done;
    g_chooser.user = user;
    if (g_chooser.win) {
        delwin(g_chooser.win);
        g_chooser.win = NULL;
    }
}

void
ui_chooser_close(void) {
    if (g_chooser.win) {
        delwin(g_chooser.win);
        g_chooser.win = NULL;
    }
    memset(&g_chooser, 0, sizeof(g_chooser));
}

int
ui_chooser_active(void) {
    return g_chooser.active;
}

int
ui_chooser_handle_key(int ch) {
    if (!g_chooser.active) {
        return 0;
    }
    if (ch == ERR) {
        return 1;
    }
    if (ch == KEY_RESIZE) {
#if DSD_CURSES_NEEDS_EXPLICIT_RESIZE
        resize_term(0, 0);
#endif
        if (g_chooser.win) {
            delwin(g_chooser.win);
            g_chooser.win = NULL;
        }
        return 1;
    }
    if (ch == KEY_UP) {
        g_chooser.sel = (g_chooser.sel - 1 + g_chooser.count) % g_chooser.count;
        return 1;
    }
    if (ch == KEY_DOWN) {
        g_chooser.sel = (g_chooser.sel + 1) % g_chooser.count;
        return 1;
    }
    if (ch == 'q' || ch == 'Q' || ch == DSD_KEY_ESC) {
        ui_chooser_close();
        return 1;
    }
    if (ch == 10 || ch == KEY_ENTER || ch == '\r') {
        void (*cb)(void*, int) = g_chooser.on_done;
        void* userp = g_chooser.user;
        int sel = g_chooser.sel;
        ui_chooser_close();
        if (cb) {
            cb(userp, sel);
        }
        return 1;
    }
    return 1;
}

void
ui_chooser_render(void) {
    if (!g_chooser.active) {
        return;
    }
    const char* title = g_chooser.title ? g_chooser.title : "Select";
    int max_item = 0;
    for (int i = 0; i < g_chooser.count; i++) {
        int L = (int)strlen(g_chooser.items[i]);
        if (L > max_item) {
            max_item = L;
        }
    }
    const char* footer = "Arrows = Move   Enter = Select   Esc/q = Cancel";
    int w = 4 + (int)strlen(title);
    int need = 4 + max_item;
    if (need > w) {
        w = need;
    }
    need = 4 + (int)strlen(footer);
    if (need > w) {
        w = need;
    }
    w += 2;
    int h = g_chooser.count + 5;
    if (h < 7) {
        h = 7;
    }
    int scr_h = 0, scr_w = 0;
    getmaxyx(stdscr, scr_h, scr_w);
    if (w > scr_w - 2) {
        w = scr_w - 2;
    }
    if (h > scr_h - 2) {
        h = scr_h - 2;
    }
    int wy = (scr_h - h) / 2;
    int wx = (scr_w - w) / 2;
    if (wy < 0) {
        wy = 0;
    }
    if (wx < 0) {
        wx = 0;
    }
    if (!g_chooser.win) {
        g_chooser.win = ui_make_window(h, w, wy, wx);
        keypad(g_chooser.win, TRUE);
        wtimeout(g_chooser.win, 0);
    }
    WINDOW* win = g_chooser.win;
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", title);
    int y = 3;
    for (int i = 0; i < g_chooser.count; i++) {
        if (i == g_chooser.sel) {
            wattron(win, A_REVERSE);
        }
        mvwprintw(win, y++, 2, "%s", g_chooser.items[i]);
        if (i == g_chooser.sel) {
            wattroff(win, A_REVERSE);
        }
    }
    mvwprintw(win, h - 2, 2, "%s", footer);
    wnoutrefresh(win);
}
