// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Internal shared structures and helpers for menu subsystem.
 *
 * This header is internal to src/ui/terminal/ and should NOT be installed.
 */
#pragma once

#include <dsd-neo/platform/curses_compat.h>
#include <dsd-neo/ui/menu_core.h>

// Shared UI context passed to callbacks (full definition; forward-declared in menu_defs.h)
typedef struct UiCtx {
    dsd_opts* opts;
    dsd_state* state;
} UiCtx;

// Menu frame in overlay stack (owned/managed by menu_core.c)
typedef struct {
    const NcMenuItem* items;
    size_t n;
    int hi;
    WINDOW* win;
    int w, h;
    int y, x;
} UiMenuFrame;

// ---- Context structures shared between actions and callbacks ----
// These are allocated by act_* functions and consumed by cb_* functions

typedef struct {
    UiCtx* c;
    char host[256];
    int port;
} UdpOutCtx;

typedef struct {
    UiCtx* c;
    char host[256];
    int port;
} TcpLinkCtx;

typedef struct {
    UiCtx* c;
} TcpWavSymCtx;

typedef struct {
    UiCtx* c;
    char addr[128];
    int port;
} UdpInCtx;

typedef struct {
    UiCtx* c;
    char host[256];
    int port;
} RigCtx;

typedef struct {
    UiCtx* c;
    int step;
    unsigned long long w, s, n;
} P2Ctx;

typedef struct {
    UiCtx* c;
    const char* name;
} P25NumCtx;

typedef struct {
    UiCtx* c;
    int step;
    unsigned long long H, K1, K2, K3, K4;
} HyCtx;

typedef struct {
    UiCtx* c;
    int step;
    unsigned long long K1, K2, K3, K4;
} AesCtx;

typedef struct {
    UiCtx* c;
    char name[64];
} EnvEditCtx;

typedef struct {
    UiCtx* c;
} M17Ctx;

// Pulse device selection context
typedef struct {
    UiCtx* c;
    const char** labels;
    const char** names;
    char** bufs;
    int n;
} PulseSelCtx;

// ---- Visibility helpers (from menu_render.c) ----
int ui_is_enabled(const NcMenuItem* it, void* ctx);
int ui_submenu_has_visible(const NcMenuItem* items, size_t n, void* ctx);
int ui_next_enabled(const NcMenuItem* items, size_t n, void* ctx, int from, int dir);

// ---- Render helpers (from menu_render.c) ----
void ui_draw_menu(WINDOW* win, const NcMenuItem* items, size_t n, int hi, void* ctx);
void ui_overlay_layout(UiMenuFrame* f, void* ctx);
void ui_overlay_ensure_window(UiMenuFrame* f);
void ui_overlay_recreate_if_needed(UiMenuFrame* f);
int ui_visible_count_and_maxlab(const NcMenuItem* items, size_t n, void* ctx, int* out_maxlab);

// Chooser helpers are declared in menu_prompts.h
