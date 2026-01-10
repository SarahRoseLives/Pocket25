// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/engine/frame_processing.h>
#include <dsd-neo/engine/protocol_dispatch.h>

#include <stddef.h>
#include <string.h>

extern int dsd_dispatch_matches_nxdn(int synctype);
extern void dsd_dispatch_handle_nxdn(dsd_opts* opts, dsd_state* state);

extern int dsd_dispatch_matches_dstar(int synctype);
extern void dsd_dispatch_handle_dstar(dsd_opts* opts, dsd_state* state);

extern int dsd_dispatch_matches_dmr(int synctype);
extern void dsd_dispatch_handle_dmr(dsd_opts* opts, dsd_state* state);

extern int dsd_dispatch_matches_x2tdma(int synctype);
extern void dsd_dispatch_handle_x2tdma(dsd_opts* opts, dsd_state* state);

extern int dsd_dispatch_matches_provoice(int synctype);
extern void dsd_dispatch_handle_provoice(dsd_opts* opts, dsd_state* state);

extern int dsd_dispatch_matches_edacs(int synctype);
extern void dsd_dispatch_handle_edacs(dsd_opts* opts, dsd_state* state);

extern int dsd_dispatch_matches_ysf(int synctype);
extern void dsd_dispatch_handle_ysf(dsd_opts* opts, dsd_state* state);

extern int dsd_dispatch_matches_m17(int synctype);
extern void dsd_dispatch_handle_m17(dsd_opts* opts, dsd_state* state);

extern int dsd_dispatch_matches_p25p2(int synctype);
extern void dsd_dispatch_handle_p25p2(dsd_opts* opts, dsd_state* state);

extern int dsd_dispatch_matches_dpmr(int synctype);
extern void dsd_dispatch_handle_dpmr(dsd_opts* opts, dsd_state* state);

extern int dsd_dispatch_matches_p25p1(int synctype);
extern void dsd_dispatch_handle_p25p1(dsd_opts* opts, dsd_state* state);

static const dsd_protocol_handler*
dsd_find_protocol_handler(int synctype) {
    const dsd_protocol_handler* handler = dsd_protocol_handlers;
    const dsd_protocol_handler* fallback = NULL;

    if (handler == NULL) {
        return NULL;
    }

    while (handler->name != NULL) {
        if (handler->matches_synctype != NULL && handler->matches_synctype(synctype)) {
            return handler;
        }
        if (fallback == NULL && strcmp(handler->name, "P25P1") == 0) {
            fallback = handler;
        }
        handler++;
    }

    return fallback;
}

void
processFrame(dsd_opts* opts, dsd_state* state) {

    if (state->rf_mod == 1) {
        state->maxref = state->max * 0.80F;
        state->minref = state->min * 0.80F;
    } else {
        state->maxref = state->max;
        state->minref = state->min;
    }

    const dsd_protocol_handler* handler = dsd_find_protocol_handler(state->synctype);
    if (handler != NULL && handler->handle_frame != NULL) {
        handler->handle_frame(opts, state);
    }
}

const dsd_protocol_handler dsd_protocol_handlers[] = {
    {"NXDN", dsd_dispatch_matches_nxdn, dsd_dispatch_handle_nxdn, NULL},
    {"D-STAR", dsd_dispatch_matches_dstar, dsd_dispatch_handle_dstar, NULL},
    {"DMR", dsd_dispatch_matches_dmr, dsd_dispatch_handle_dmr, NULL},
    {"X2-TDMA", dsd_dispatch_matches_x2tdma, dsd_dispatch_handle_x2tdma, NULL},
    {"ProVoice", dsd_dispatch_matches_provoice, dsd_dispatch_handle_provoice, NULL},
    {"EDACS", dsd_dispatch_matches_edacs, dsd_dispatch_handle_edacs, NULL},
    {"YSF", dsd_dispatch_matches_ysf, dsd_dispatch_handle_ysf, NULL},
    {"M17", dsd_dispatch_matches_m17, dsd_dispatch_handle_m17, NULL},
    {"P25P2", dsd_dispatch_matches_p25p2, dsd_dispatch_handle_p25p2, NULL},
    {"dPMR", dsd_dispatch_matches_dpmr, dsd_dispatch_handle_dpmr, NULL},
    {"P25P1", dsd_dispatch_matches_p25p1, dsd_dispatch_handle_p25p1, NULL},
    {0, 0, 0, 0},
};
