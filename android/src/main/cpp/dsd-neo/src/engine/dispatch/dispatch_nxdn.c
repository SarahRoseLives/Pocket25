// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/nxdn/nxdn.h>

int
dsd_dispatch_matches_nxdn(int synctype) {
    return DSD_SYNC_IS_NXDN(synctype);
}

void
dsd_dispatch_handle_nxdn(dsd_opts* opts, dsd_state* state) {
    nxdn_frame(opts, state);
}
