// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/dstar/dstar.h>

#include <stdio.h>

int
dsd_dispatch_matches_dstar(int synctype) {
    return DSD_SYNC_IS_DSTAR(synctype);
}

void
dsd_dispatch_handle_dstar(dsd_opts* opts, dsd_state* state) {
    if ((opts->mbe_out_dir[0] != 0) && (opts->mbe_out_f == NULL)) {
        openMbeOutFile(opts, state);
    }

    if (state->synctype == DSD_SYNC_DSTAR_VOICE_POS || state->synctype == DSD_SYNC_DSTAR_VOICE_NEG) {
        sprintf(state->fsubtype, " VOICE        ");
        processDSTAR(opts, state);
        return;
    }

    sprintf(state->fsubtype, " DATA         ");
    processDSTAR_HD(opts, state);
}
