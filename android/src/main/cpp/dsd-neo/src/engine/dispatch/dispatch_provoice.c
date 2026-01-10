// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/provoice/provoice.h>

#include <stdio.h>

int
dsd_dispatch_matches_provoice(int synctype) {
    return synctype == DSD_SYNC_PROVOICE_POS || synctype == DSD_SYNC_PROVOICE_NEG;
}

void
dsd_dispatch_handle_provoice(dsd_opts* opts, dsd_state* state) {
    if ((opts->mbe_out_dir[0] != 0) && (opts->mbe_out_f == NULL)) {
        openMbeOutFile(opts, state);
    }
    sprintf(state->fsubtype, " VOICE        ");
    processProVoice(opts, state);
}
