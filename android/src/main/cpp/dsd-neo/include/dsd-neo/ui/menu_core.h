// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Minimal, reusable ncurses menu framework for the terminal UI.
 *
 * Declares the menu item structure and overlay driver helpers used by the
 * asynchronous menu system.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

typedef struct NcMenuItem NcMenuItem;

/** Predicate signature for enabling/disabling items. */
typedef bool (*nc_enabled_fn)(void* ctx);
/** Action to invoke on item selection. */
typedef void (*nc_action_fn)(void* ctx);
/** Dynamic label generator; writes into buf and returns it. */
typedef const char* (*nc_label_fn)(void* ctx, char* buf, size_t buf_len);

/**
 * @brief Declarative menu item description.
 */
struct NcMenuItem {
    const char* id;            // stable identifier for the item
    const char* label;         // static label text (fallback if label_fn is NULL)
    nc_label_fn label_fn;      // optional dynamic label generator (writes into buf and returns it)
    const char* help;          // optional help text shown with 'h'
    nc_enabled_fn is_enabled;  // optional predicate; NULL -> enabled
    nc_action_fn on_select;    // action to run when selected; may open submenus
    const NcMenuItem* submenu; // optional nested items
    size_t submenu_len;        // length of submenu
};

/**
 * @brief Set a transient status footer (shows briefly at bottom of menu window).
 */
void ui_statusf(const char* fmt, ...);

/**
 * @brief Open the main menu as a nonblocking overlay.
 *
 * Subsequent draws happen via `ui_menu_tick`, and keys are routed via
 * `ui_menu_handle_key`.
 */
void ui_menu_open_async(dsd_opts* opts, dsd_state* state);
/** @brief Return 1 when the overlay is currently open. */
int ui_menu_is_open(void);
/**
 * @brief Handle a key for the overlay; returns 1 if consumed.
 */
int ui_menu_handle_key(int ch, dsd_opts* opts, dsd_state* state);
/**
 * @brief Draw/update one frame of the overlay (no input read here).
 */
void ui_menu_tick(dsd_opts* opts, dsd_state* state);
