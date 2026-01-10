// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/control_pump.h>

#include <stddef.h>

static dsd_control_pump_fn g_control_pump_fn = NULL;

void
dsd_runtime_set_control_pump(dsd_control_pump_fn fn) {
    g_control_pump_fn = fn;
}

void
dsd_runtime_pump_controls(dsd_opts* opts, dsd_state* state) {
    dsd_control_pump_fn fn = g_control_pump_fn;
    if (!fn) {
        return;
    }
    fn(opts, state);
}
