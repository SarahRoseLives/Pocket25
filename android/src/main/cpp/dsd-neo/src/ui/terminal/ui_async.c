// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/platform/atomic_compat.h>
#include <dsd-neo/platform/curses_compat.h>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/platform/timing.h>
#include <dsd-neo/runtime/control_pump.h>
#include <dsd-neo/ui/ui_async.h>
#include <dsd-neo/ui/ui_prims.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/ui/menu_core.h>
#include <dsd-neo/ui/ncurses.h>
#include <dsd-neo/ui/ui_opts_snapshot.h>
#include <dsd-neo/ui/ui_snapshot.h>

#include "telemetry_hooks_impl.h"

// Minimal thread state.
static dsd_thread_t g_ui_thread;
static atomic_int g_ui_running = 0;
static atomic_int g_ui_stop = 0;
static atomic_int g_ui_dirty = 0; // notifier for redraw requests
static dsd_opts* g_ui_opts = NULL;
static dsd_state* g_ui_state = NULL;
static int g_ui_curses_cfg_done = 0;
static atomic_int g_ui_in_context = 0;

int
ui_is_thread_context(void) {
    return atomic_load(&g_ui_in_context);
}

static void
ui_control_pump(dsd_opts* opts, dsd_state* state) {
    (void)ui_drain_cmds(opts, state);
}

static DSD_THREAD_RETURN_TYPE
#if DSD_PLATFORM_WIN_NATIVE
    __stdcall
#endif
    ui_thread_main(void* arg) {
    (void)arg;

    const unsigned int sleep_ms = 15; // ~15 ms sleep cadence

    // Initialize ncurses lifecycle in UI thread
    if (g_ui_opts && g_ui_opts->use_ncurses_terminal == 1) {
        ncursesOpen(g_ui_opts, g_ui_state);
    }

    uint64_t last_draw_ns = dsd_time_monotonic_ns();
    const uint64_t frame_ns = 66ULL * 1000ULL * 1000ULL; // ~15 FPS cap

    while (!atomic_load(&g_ui_stop)) {
        // Input + overlays handled in the UI thread when curses is ready.
        const dsd_opts* osnap = ui_get_latest_opts_snapshot();
        if (!osnap) {
            osnap = g_ui_opts;
        }
        if (osnap && osnap->use_ncurses_terminal == 1 && stdscr != NULL) {
            // One-time input config in UI thread (ESC delay, keypad, nonblocking getch)
            if (!g_ui_curses_cfg_done) {
                dsd_curses_set_escdelay(25);
                keypad(stdscr, TRUE);
                timeout(0);
                g_ui_curses_cfg_done = 1;
            }

            int ch = ERR;
            if (osnap->audio_in_type != AUDIO_IN_STDIN) { // Avoid getch when stdin is input
                ch = getch();
            }

            if (ui_menu_is_open()) {
                if (ch != ERR) {
                    ui_menu_handle_key(ch, g_ui_opts, g_ui_state);
                }
                // Keep overlays updated each frame
                ui_menu_tick(g_ui_opts, g_ui_state);
                ui_commit_frame();
            } else {
                if (ch == KEY_RESIZE) {
#if DSD_CURSES_NEEDS_EXPLICIT_RESIZE
                    // PDCurses doesn't auto-update dimensions on resize;
                    // resize_term(0,0) queries actual console size.
                    resize_term(0, 0);
#endif
                    clearok(stdscr, TRUE);
                    ui_terminal_telemetry_request_redraw();
                } else if (ch != ERR) {
                    (void)ncurses_input_handler(g_ui_opts, g_ui_state, ch);
                }
            }
        }

        // Draw on dirty or FPS tick when curses is active
        if (osnap && osnap->use_ncurses_terminal == 1 && stdscr != NULL) {
            uint64_t now_ns = dsd_time_monotonic_ns();
            uint64_t dt_ns = now_ns - last_draw_ns;
            if (atomic_exchange(&g_ui_dirty, 0) || dt_ns >= frame_ns) {
                atomic_store(&g_ui_in_context, 1);
                /* Draw using a state snapshot when available */
                const dsd_state* snap = ui_get_latest_snapshot();
                if (snap) {
                    ncursesPrinter((dsd_opts*)osnap, (dsd_state*)snap);
                } else {
                    ncursesPrinter((dsd_opts*)osnap, g_ui_state);
                }
                atomic_store(&g_ui_in_context, 0);
                ui_commit_frame();
                last_draw_ns = now_ns;
            }
        }

        dsd_sleep_ms(sleep_ms);
    }

    if (g_ui_opts && g_ui_opts->use_ncurses_terminal == 1) {
        ncursesClose();
    }

    DSD_THREAD_RETURN;
}

int
ui_start(dsd_opts* opts, dsd_state* state) {
    if (atomic_load(&g_ui_running)) {
        return 0; // already running
    }

    ui_terminal_install_telemetry_hooks();
    g_ui_opts = opts;
    g_ui_state = state;
    atomic_store(&g_ui_stop, 0);

    if (dsd_thread_create(&g_ui_thread, ui_thread_main, NULL) != 0) {
        g_ui_opts = NULL;
        g_ui_state = NULL;
        return -1;
    }

    dsd_runtime_set_control_pump(ui_control_pump);
    atomic_store(&g_ui_running, 1);
    return 0;
}

void
ui_stop(void) {
    if (!atomic_load(&g_ui_running)) {
        return;
    }
    dsd_runtime_set_control_pump(NULL);
    atomic_store(&g_ui_stop, 1);
    dsd_thread_join(g_ui_thread);
    atomic_store(&g_ui_running, 0);
    g_ui_opts = NULL;
    g_ui_state = NULL;
}

void
ui_terminal_telemetry_request_redraw(void) {
    atomic_store(&g_ui_dirty, 1);
}
