// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * menu_defs.c
 * Concrete menu item arrays for the top-level menus
 */

#include <dsd-neo/ui/menu_defs.h>

// All NcMenuItem arrays are declared in menu_items.h
#include "menu_items.h"

void
ui_menu_get_main_items(const NcMenuItem** out_items, size_t* out_n, UiCtx* ctx) {
    (void)ctx; // context used by callbacks; arrays are static so safe to expose
    static int inited = 0;
    static NcMenuItem items[10];
    if (!inited) {
        items[0] = (NcMenuItem){.id = "main.io",
                                .label = "Devices & IO",
                                .help = "TCP, symbol replay, inversion.",
                                .submenu = IO_MENU_ITEMS,
                                .submenu_len = IO_MENU_ITEMS_LEN};
        items[1] = (NcMenuItem){.id = "main.logging",
                                .label = "Logging & Capture",
                                .help = "Symbols, WAV, payloads, alerts, history.",
                                .submenu = LOGGING_MENU_ITEMS,
                                .submenu_len = LOGGING_MENU_ITEMS_LEN};
        items[2] = (NcMenuItem){.id = "main.trunk",
                                .label = "Trunking & Control",
                                .help = "P25 CC prefs, Phase 2 params, rigctl.",
                                .submenu = TRUNK_MENU_ITEMS,
                                .submenu_len = TRUNK_MENU_ITEMS_LEN};
        items[3] = (NcMenuItem){.id = "main.keys",
                                .label = "Keys & Security",
                                .help = "Manage keys and encrypted audio muting.",
                                .submenu = KEYS_MENU_ITEMS,
                                .submenu_len = KEYS_MENU_ITEMS_LEN};
        items[4] = (NcMenuItem){.id = "main.dsp",
                                .label = "DSP Options",
                                .help = "RTL-SDR DSP toggles and tuning.",
                                .is_enabled = io_rtl_active,
#ifdef USE_RTLSDR
                                .submenu = DSP_MENU_ITEMS,
                                .submenu_len = DSP_MENU_ITEMS_LEN};
#else
                                .submenu = NULL,
                                .submenu_len = 0};
#endif
        items[5] = (NcMenuItem){.id = "main.ui",
                                .label = "UI Display",
                                .help = "Toggle on-screen sections.",
                                .submenu = UI_DISPLAY_MENU_ITEMS,
                                .submenu_len = UI_DISPLAY_MENU_ITEMS_LEN};
        items[6] = (NcMenuItem){.id = "lrrp",
                                .label = "LRRP",
                                .help = "Configure LRRP file output.",
                                .submenu = LRRP_MENU_ITEMS,
                                .submenu_len = LRRP_MENU_ITEMS_LEN};
        items[7] = (NcMenuItem){.id = "main.config",
                                .label = "Config",
                                .help = "Save current settings to a config file.",
                                .submenu = CONFIG_MENU_ITEMS,
                                .submenu_len = CONFIG_MENU_ITEMS_LEN};
        items[8] = (NcMenuItem){.id = "main.adv",
                                .label = "Advanced & Env",
                                .help = "P25 follower, DSP advanced, RTL/TCP, env editor.",
                                .submenu = ADV_MENU_ITEMS,
                                .submenu_len = ADV_MENU_ITEMS_LEN};
        items[9] =
            (NcMenuItem){.id = "exit", .label = "Exit DSD-neo", .help = "Quit the application.", .on_select = act_exit};
        inited = 1;
    }
    if (out_items) {
        *out_items = items;
    }
    if (out_n) {
        *out_n = sizeof items / sizeof items[0];
    }
}
