// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Menu item array declarations for each submenu.
 *
 * This header is internal to src/ui/terminal/ and should NOT be installed.
 */
#pragma once

#include <dsd-neo/ui/menu_core.h>
#include <stddef.h>

// ---- IO Menu ----
extern const NcMenuItem IO_MENU_ITEMS[];
extern const size_t IO_MENU_ITEMS_LEN;

// ---- Logging Menu ----
extern const NcMenuItem LOGGING_MENU_ITEMS[];
extern const size_t LOGGING_MENU_ITEMS_LEN;

// ---- Trunking Menu ----
extern const NcMenuItem TRUNK_MENU_ITEMS[];
extern const size_t TRUNK_MENU_ITEMS_LEN;

// ---- Keys Menu ----
extern const NcMenuItem KEYS_MENU_ITEMS[];
extern const size_t KEYS_MENU_ITEMS_LEN;

// ---- UI Display Menu ----
extern const NcMenuItem UI_DISPLAY_MENU_ITEMS[];
extern const size_t UI_DISPLAY_MENU_ITEMS_LEN;

// ---- LRRP Menu ----
extern const NcMenuItem LRRP_MENU_ITEMS[];
extern const size_t LRRP_MENU_ITEMS_LEN;

// ---- Config Menu ----
extern const NcMenuItem CONFIG_MENU_ITEMS[];
extern const size_t CONFIG_MENU_ITEMS_LEN;

// ---- Advanced Menu ----
extern const NcMenuItem ADV_MENU_ITEMS[];
extern const size_t ADV_MENU_ITEMS_LEN;

// ---- DSP Menu (USE_RTLSDR only) ----
#ifdef USE_RTLSDR
extern const NcMenuItem DSP_MENU_ITEMS[];
extern const size_t DSP_MENU_ITEMS_LEN;

// RTL-SDR menu (required for IO_INPUT_ITEMS submenu reference)
extern const NcMenuItem RTL_MENU_ITEMS[];

// DSP submenu arrays (needed by menu_labels.c predicates)
extern const NcMenuItem DSP_AGC_ITEMS[];
extern const size_t DSP_AGC_ITEMS_LEN;
extern const NcMenuItem DSP_TED_ITEMS[];
extern const size_t DSP_TED_ITEMS_LEN;
#endif
