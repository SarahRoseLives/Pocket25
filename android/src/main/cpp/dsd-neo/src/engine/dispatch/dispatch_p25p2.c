// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/p25/p25.h>

int
dsd_dispatch_matches_p25p2(int synctype) {
    return DSD_SYNC_IS_P25P2(synctype);
}

void
dsd_dispatch_handle_p25p2(dsd_opts* opts, dsd_state* state) {
    processP2(opts, state);
}
