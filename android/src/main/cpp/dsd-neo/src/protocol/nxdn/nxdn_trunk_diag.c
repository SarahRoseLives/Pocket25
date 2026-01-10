// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/protocol/nxdn/nxdn_trunk_diag.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/state_ext.h>
#include <dsd-neo/runtime/log.h>

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint8_t missing_seen[(0xFFFFu + 7u) / 8u];
    uint32_t missing_unique;
} nxdn_trunk_diag_t;

static nxdn_trunk_diag_t*
nxdn_trunk_diag_get_or_create(dsd_state* state) {
    if (!state) {
        return NULL;
    }

    nxdn_trunk_diag_t* diag = DSD_STATE_EXT_GET_AS(nxdn_trunk_diag_t, state, DSD_STATE_EXT_PROTO_NXDN_TRUNK_DIAG);
    if (diag) {
        return diag;
    }

    diag = (nxdn_trunk_diag_t*)calloc(1, sizeof(*diag));
    if (!diag) {
        return NULL;
    }
    if (dsd_state_ext_set(state, DSD_STATE_EXT_PROTO_NXDN_TRUNK_DIAG, diag, free) != 0) {
        free(diag);
        return NULL;
    }
    return diag;
}

int
nxdn_trunk_diag_note_missing_channel(dsd_state* state, uint16_t channel) {
    if (!state) {
        return 0;
    }
    if (channel == 0 || channel >= 0xFFFFu) {
        return 0;
    }

    nxdn_trunk_diag_t* diag = nxdn_trunk_diag_get_or_create(state);
    if (!diag) {
        return 0;
    }

    const size_t byte_idx = (size_t)channel / 8u;
    const uint8_t bit_mask = (uint8_t)(1u << ((unsigned)channel % 8u));
    if ((diag->missing_seen[byte_idx] & bit_mask) != 0) {
        return 0;
    }

    diag->missing_seen[byte_idx] |= bit_mask;
    diag->missing_unique++;
    return 1;
}

size_t
nxdn_trunk_diag_collect_unmapped_channels(const dsd_state* state, uint16_t* out, size_t out_cap) {
    if (!state) {
        return 0;
    }

    const nxdn_trunk_diag_t* diag =
        DSD_STATE_EXT_GET_AS(const nxdn_trunk_diag_t, (dsd_state*)state, DSD_STATE_EXT_PROTO_NXDN_TRUNK_DIAG);
    if (!diag || diag->missing_unique == 0) {
        return 0;
    }

    size_t total = 0;
    size_t wrote = 0;

    for (unsigned int ch = 1; ch < 0xFFFFu; ch++) {
        const size_t byte_idx = (size_t)ch / 8u;
        const uint8_t bit_mask = (uint8_t)(1u << (ch % 8u));
        if ((diag->missing_seen[byte_idx] & bit_mask) == 0) {
            continue;
        }
        if (state->trunk_chan_map[ch] != 0) {
            continue;
        }

        total++;
        if (out && wrote < out_cap) {
            out[wrote++] = (uint16_t)ch;
        }
    }

    return total;
}

void
nxdn_trunk_diag_log_missing_channel_once(const dsd_opts* opts, dsd_state* state, uint16_t channel,
                                         const char* context) {
    if (!opts || !state) {
        return;
    }
    if (opts->chan_in_file[0] == '\0') {
        return;
    }
    if (channel == 0 || channel >= 0xFFFFu) {
        return;
    }
    if (state->trunk_chan_map[channel] != 0) {
        return;
    }
    if (!nxdn_trunk_diag_note_missing_channel(state, channel)) {
        return;
    }

    if (context && context[0] != '\0') {
        LOG_NOTICE("NXDN trunking: %s: CH %u has no frequency mapping in chan_csv (%s)\n", context, channel,
                   opts->chan_in_file);
    } else {
        LOG_NOTICE("NXDN trunking: CH %u has no frequency mapping in chan_csv (%s)\n", channel, opts->chan_in_file);
    }
}

void
nxdn_trunk_diag_log_summary(const dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return;
    }
    if (opts->chan_in_file[0] == '\0') {
        return;
    }

    uint16_t missing[16];
    const size_t cap = sizeof(missing) / sizeof(missing[0]);
    const size_t total = nxdn_trunk_diag_collect_unmapped_channels(state, missing, cap);
    if (total == 0) {
        return;
    }

    char msg[512];
    int n = snprintf(msg, sizeof msg, "NXDN trunking: %zu channel%s missing frequency mapping in chan_csv (%s):", total,
                     (total == 1) ? " is" : "s are", opts->chan_in_file);
    if (n < 0) {
        return;
    }
    size_t used = (size_t)n;

    const size_t shown = (total < cap) ? total : cap;
    for (size_t i = 0; i < shown && used < sizeof msg; i++) {
        const char* sep = (i == 0) ? " CH " : ", CH ";
        int w = snprintf(msg + used, sizeof msg - used, "%s%u", sep, missing[i]);
        if (w < 0) {
            break;
        }
        used += (size_t)w;
    }
    if (total > shown && used < sizeof msg) {
        int w = snprintf(msg + used, sizeof msg - used, " (+%zu more)", total - shown);
        if (w > 0) {
            used += (size_t)w;
        }
    }
    if (used < sizeof msg) {
        (void)snprintf(msg + used, sizeof msg - used, "\n");
    } else {
        msg[sizeof msg - 1] = '\0';
    }

    LOG_NOTICE("%s", msg);
}
