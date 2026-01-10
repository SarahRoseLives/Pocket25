// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/control_pump.h>

#include <stddef.h>

static int s_calls = 0;

static void
test_pump(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
    s_calls++;
}

int
main(void) {
    // Default behavior is a safe no-op until a pump is registered.
    dsd_runtime_pump_controls(NULL, NULL);
    if (s_calls != 0) {
        return 1;
    }

    dsd_runtime_set_control_pump(test_pump);
    dsd_runtime_pump_controls(NULL, NULL);
    dsd_runtime_pump_controls(NULL, NULL);
    if (s_calls != 2) {
        return 2;
    }

    dsd_runtime_set_control_pump(NULL);
    dsd_runtime_pump_controls(NULL, NULL);
    if (s_calls != 2) {
        return 3;
    }

    // Re-register should work.
    dsd_runtime_set_control_pump(test_pump);
    dsd_runtime_pump_controls(NULL, NULL);
    if (s_calls != 3) {
        return 4;
    }

    dsd_runtime_set_control_pump(NULL);
    return 0;
}
