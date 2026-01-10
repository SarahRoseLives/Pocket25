// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/dibit.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/m17/m17.h>

int
dsd_dispatch_matches_m17(int synctype) {
    return DSD_SYNC_IS_M17(synctype);
}

void
dsd_dispatch_handle_m17(dsd_opts* opts, dsd_state* state) {
    if (state->synctype == DSD_SYNC_M17_PRE_POS || state->synctype == DSD_SYNC_M17_PRE_NEG) {
        skipDibit(opts, state, 8);
        return;
    }

    if (state->synctype == DSD_SYNC_M17_LSF_POS || state->synctype == DSD_SYNC_M17_LSF_NEG) {
        processM17LSF(opts, state);
        return;
    }

    if (state->synctype == DSD_SYNC_M17_BRT_POS || state->synctype == DSD_SYNC_M17_BRT_NEG) {
        return;
    }

    if (state->synctype == DSD_SYNC_M17_PKT_POS || state->synctype == DSD_SYNC_M17_PKT_NEG) {
        processM17PKT(opts, state);
        return;
    }

    processM17STR(opts, state);
}
