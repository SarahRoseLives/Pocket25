// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * footer.c
 * Footer panel renderer for the ncurses terminal UI
 */

#include <dsd-neo/platform/curses_compat.h>
#include <time.h>

#include <dsd-neo/core/state.h>
#include <dsd-neo/ui/panels.h>
#include <dsd-neo/ui/ui_async.h>
#include <dsd-neo/ui/ui_cmd.h>
#include <dsd-neo/ui/ui_prims.h>

void
ui_panel_footer_status_render(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return;
    }
    // Transient toast message (e.g., mute toggled)
    time_t now = time(NULL);
    if (state->ui_msg[0] != '\0' && state->ui_msg_expire > now) {
#ifdef PRETTY_COLORS
        // Preserve current color pair to avoid forcing default/white after toast
        attr_t saved_attrs = 0;
        short saved_pair = 0;
        attr_get(&saved_attrs, &saved_pair, NULL);
#endif
        attron(COLOR_PAIR(2));
        printw("| %s\n", state->ui_msg);
        attroff(COLOR_PAIR(2));
        ui_print_hr();
#ifdef PRETTY_COLORS
        // Restore whichever color/attrs were active before the toast
        attr_set(saved_attrs, saved_pair, NULL);
#endif
    } else if (state->ui_msg_expire <= now && state->ui_msg[0] != '\0') {
        // Clear stale message and sync canonical state via command queue
        ui_post_cmd(UI_CMD_UI_MSG_CLEAR, NULL, 0);
        state->ui_msg[0] = '\0';
    }
}
