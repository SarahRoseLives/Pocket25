// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/telemetry.h>

#include "telemetry_hooks_impl.h"

void
ui_terminal_install_telemetry_hooks(void) {
    dsd_telemetry_hooks_set((dsd_telemetry_hooks){
        .publish_snapshot = ui_terminal_telemetry_publish_snapshot,
        .publish_opts_snapshot = ui_terminal_telemetry_publish_opts_snapshot,
        .request_redraw = ui_terminal_telemetry_request_redraw,
    });
}
