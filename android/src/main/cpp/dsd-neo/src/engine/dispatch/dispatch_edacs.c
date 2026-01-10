// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/edacs/edacs.h>

int
dsd_dispatch_matches_edacs(int synctype) {
    return synctype == DSD_SYNC_EDACS_POS || synctype == DSD_SYNC_EDACS_NEG;
}

void
dsd_dispatch_handle_edacs(dsd_opts* opts, dsd_state* state) {
    if (opts->mbe_out_f != NULL) {
        closeMbeOutFile(opts, state);
    }
    edacs(opts, state);
}
