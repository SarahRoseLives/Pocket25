// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/platform/atomic_compat.h>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/platform/timing.h>
#include <dsd-neo/protocol/p25/p25_sm_watchdog.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/exitflag.h>

/* exitflag declared in runtime/exitflag.h, defined in src/runtime/exitflag.c */

static dsd_thread_t g_p25_sm_wd_thread;
static atomic_int g_p25_sm_wd_running = 0;
static atomic_int g_p25_sm_tick_lock = 0;
static atomic_int g_p25_sm_in_tick = 0;
static dsd_opts* g_opts = NULL;
static dsd_state* g_state = NULL;
static int g_p25_sm_wd_ms = 0; // 0 => unset (use defaults per UI mode)

void
p25_sm_try_tick(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return;
    }
    int expected = 0;
    if (atomic_compare_exchange_strong(&g_p25_sm_tick_lock, &expected, 1)) {
        /* Only one tick runs at a time across all callers. */
        atomic_store(&g_p25_sm_in_tick, 1);
        // Drive the high-level trunk SM tick
        p25_sm_tick(opts, state);
        atomic_store(&g_p25_sm_in_tick, 0);
        atomic_store(&g_p25_sm_tick_lock, 0);
    }
}

static DSD_THREAD_RETURN_TYPE
#if DSD_PLATFORM_WIN_NATIVE
    __stdcall
#endif
    p25_sm_watchdog_thread(void* arg) {
    (void)arg;
    while (atomic_load(&g_p25_sm_wd_running) && !exitflag) {
        if (g_opts && g_state && g_opts->p25_trunk == 1) {
            p25_sm_try_tick(g_opts, g_state);
        }
        // Compute watchdog cadence. Prefer env override when provided;
        // otherwise, tick faster under ncurses to reduce perceived wedges.
        int ms = g_p25_sm_wd_ms;
        if (ms <= 0) {
            ms = (g_opts && g_opts->use_ncurses_terminal == 1) ? 200 : 400; // 200ms UI, 400ms headless
        }
        if (ms < 20) {
            ms = 20; // clamp to sane bounds
        }
        if (ms > 2000) {
            ms = 2000; // 2s max
        }
        dsd_sleep_ms((unsigned int)ms);
    }
    DSD_THREAD_RETURN;
}

void
p25_sm_watchdog_start(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return;
    }
    g_opts = opts;
    g_state = state;
    // One-time env override for watchdog cadence (milliseconds)
    // DSD_NEO_P25_WD_MS=100 .. 2000
    if (g_p25_sm_wd_ms == 0) {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        if (cfg && cfg->p25_wd_ms_is_set) {
            g_p25_sm_wd_ms = cfg->p25_wd_ms;
        }
    }
    int expected = 0;
    if (atomic_compare_exchange_strong(&g_p25_sm_wd_running, &expected, 1)) {
        (void)dsd_thread_create(&g_p25_sm_wd_thread, p25_sm_watchdog_thread, NULL);
    }
}

void
p25_sm_watchdog_stop(void) {
    int was = atomic_exchange(&g_p25_sm_wd_running, 0);
    if (was != 0) {
        dsd_thread_join(g_p25_sm_wd_thread);
    }
}

int
p25_sm_in_tick(void) {
    return atomic_load(&g_p25_sm_in_tick) ? 1 : 0;
}
