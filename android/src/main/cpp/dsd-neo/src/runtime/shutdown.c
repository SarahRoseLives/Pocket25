// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/shutdown.h>

#include <dsd-neo/runtime/exitflag.h>

void
dsd_request_shutdown(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
    exitflag = 1;
}
