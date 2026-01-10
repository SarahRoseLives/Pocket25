// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Public surface for data-driven ncurses menu trees.
 *
 * Exposes the root menu items and predicates/actions referenced by the
 * asynchronous overlay menu driver.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <dsd-neo/ui/menu_core.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque to callers outside menu subsystem
typedef struct UiCtx UiCtx;

/**
 * @brief Get the static top-level menu items for the overlay menu.
 *
 * The returned array is owned by the menu subsystem and remains valid for the
 * lifetime of the process.
 *
 * @param out_items [out] Receives pointer to the first menu item.
 * @param out_n [out] Receives the number of items in the array.
 * @param ctx Optional UI context used by predicate callbacks.
 */
void ui_menu_get_main_items(const NcMenuItem** out_items, size_t* out_n, UiCtx* ctx);

/**
 * @brief Predicate: returns true when RTL-SDR is the active input.
 */
bool io_rtl_active(void* ctx);
/**
 * @brief Action: signal the application to exit.
 */
void act_exit(void* v);

#ifdef __cplusplus
}
#endif
