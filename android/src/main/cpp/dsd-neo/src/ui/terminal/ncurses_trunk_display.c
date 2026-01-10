// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*
 * ncurses_trunk_display.c
 * Trunk system display functions for ncurses UI
 */

#include <dsd-neo/ui/ncurses_trunk_display.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/ui/ncurses_p25_display.h>
#include <dsd-neo/ui/ui_prims.h>

#include <dsd-neo/platform/curses_compat.h>
#include <string.h>
#include <time.h>

// Print learned trunking LCNs and their mapped frequencies
void
ui_print_learned_lcns(const dsd_opts* opts, const dsd_state* state) {
    if (!opts || !state) {
        return;
    }
    if (opts->p25_trunk != 1) {
        return;
    }

    int have_lcn_freq = 0;
    for (int i = 0; i < 26; i++) {
        if (state->trunk_lcn_freq[i] != 0) {
            have_lcn_freq = 1;
            break;
        }
    }

    int have_chan_map = 0;
    // Presence check across the full range; needed because many systems use high channel indices
    for (int i = 1; i < 65535; i++) {
        if (state->trunk_chan_map[i] != 0) {
            have_chan_map = 1;
            break;
        }
    }

    if (!have_lcn_freq && !have_chan_map) {
        return;
    }

    ui_print_header("Channels");

    // Prefer a calm cyan unless a call is active
    if (state->carrier == 1) {
        attron(COLOR_PAIR(3));
    } else {
        attron(COLOR_PAIR(4));
    }

    // Track which freqs we've already shown to avoid duplicates across LCNs and CH map
    long int seen_freqs[256];
    int seen_count = 0;
    int cols_per_line = 3;
    int col_in_row = 0;

    // First: render known channel->frequency pairs as CH <hex>
    if (have_chan_map) {
        int printed = 0;
        int extra = 0;
        for (int i = 1; i < 65535; i++) {
            long int f = state->trunk_chan_map[i];
            if (f == 0) {
                continue;
            }
            int dup = 0;
            for (int k = 0; k < seen_count; k++) {
                if (seen_freqs[k] == f) {
                    dup = 1;
                    break;
                }
            }
            if (dup) {
                continue;
            }
            if (printed < 32) { // cap to avoid flooding (rows of 3)
                if (col_in_row == 0) {
                    ui_print_lborder_green();
                    addch(' ');
                }
                // Temporarily tint IDEN-derived channels
                attr_t saved_attrs = 0;
                short saved_pair = 0;
                attr_get(&saved_attrs, &saved_pair, NULL);
                int iden = -1;
                int is_iden = ui_match_iden_channel(state, i, f, &iden);
                if (is_iden) {
                    attron(COLOR_PAIR(ui_iden_color_pair(iden)));
                    printw("CH %04X[I%d]: %010.06lf MHz", i & 0xFFFF, iden & 0xF, (double)f / 1000000.0);
                    attr_set(saved_attrs, saved_pair, NULL);
                } else {
                    printw("CH %04X: %010.06lf MHz", i & 0xFFFF, (double)f / 1000000.0);
                }
                col_in_row++;
                printed++;
                if (col_in_row >= cols_per_line) {
                    addch('\n');
                    col_in_row = 0;
                } else {
                    addstr("   "); // spacing between columns
                }
            } else {
                extra++;
            }
            if (seen_count < (int)(sizeof(seen_freqs) / sizeof(seen_freqs[0]))) {
                seen_freqs[seen_count++] = f;
            }
        }
        if (col_in_row > 0) { // flush partial row before switching to LCN list
            addch('\n');
            col_in_row = 0; // reset so the next section starts with a fresh border
        }
        if (extra > 0) {
            ui_print_lborder_green();
            printw(" ... and %d more learned channels\n", extra);
        }
    }

    // Then: include any additional freqs learned via LCN list, labeling as CH as well.
    if (have_lcn_freq) {
        for (int i = 0; i < 26; i++) {
            long int f = state->trunk_lcn_freq[i];
            if (f == 0) {
                continue;
            }
            int dup = 0;
            for (int k = 0; k < seen_count; k++) {
                if (seen_freqs[k] == f) {
                    dup = 1;
                    break;
                }
            }
            if (dup) {
                continue;
            }
            // Try to find a matching channel id for this freq
            int found_ch = -1;
            for (int j = 1; j < 65535; j++) {
                if (state->trunk_chan_map[j] == f) {
                    found_ch = j;
                    break;
                }
            }
            if (found_ch >= 0) {
                if (col_in_row == 0) {
                    ui_print_lborder_green();
                    addch(' ');
                }
                // Tint if this CH aligns with IDEN params
                attr_t saved_attrs = 0;
                short saved_pair = 0;
                attr_get(&saved_attrs, &saved_pair, NULL);
                int iden = -1;
                int is_iden = ui_match_iden_channel(state, found_ch, f, &iden);
                if (is_iden) {
                    attron(COLOR_PAIR(ui_iden_color_pair(iden)));
                    printw("CH %04X[I%d]: %010.06lf MHz", found_ch & 0xFFFF, iden & 0xF, (double)f / 1000000.0);
                    attr_set(saved_attrs, saved_pair, NULL);
                } else {
                    printw("CH %04X: %010.06lf MHz", found_ch & 0xFFFF, (double)f / 1000000.0);
                }
            } else {
                if (col_in_row == 0) {
                    ui_print_lborder_green();
                    addch(' ');
                }
                printw("CH ----: %010.06lf MHz", (double)f / 1000000.0);
            }
            col_in_row++;
            if (col_in_row >= cols_per_line) {
                addch('\n');
                col_in_row = 0;
            } else {
                addstr("   ");
            }
            if (seen_count < (int)(sizeof(seen_freqs) / sizeof(seen_freqs[0]))) {
                seen_freqs[seen_count++] = f;
            }
        }
        if (col_in_row > 0) {
            addch('\n');
        }
    }

    // Legend for IDEN color/suffix (P25 systems only)
    {
        int lls = state ? state->synctype : DSD_SYNC_NONE;
        int is_p25p1 = DSD_SYNC_IS_P25P1(lls);
        int is_p25p2 = DSD_SYNC_IS_P25P2(lls);
        if (is_p25p1 || is_p25p2) {
            ui_print_lborder_green();
            printw(" Legend: IDEN colors ");
            for (int c = 0; c < 8; c++) {
                attron(COLOR_PAIR(ui_iden_color_pair(c)));
                printw("I%d", c);
                attroff(COLOR_PAIR(ui_iden_color_pair(c)));
                addch(' ');
            }
            addch('\n');
        }
    }

    // Restore to green if in-call, otherwise keep cyan; callers around will adjust as needed
    if (state->carrier == 1) {
        attron(COLOR_PAIR(3));
    } else {
        attron(COLOR_PAIR(4));
    }
}
