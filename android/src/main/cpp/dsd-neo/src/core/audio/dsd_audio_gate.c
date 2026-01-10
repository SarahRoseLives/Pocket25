// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Audio gating helpers used by mixers and tests.
 *
 * The helpers here centralize per-slot gating decisions so that
 * dsd_audio2.c only needs to invoke them rather than duplicate the
 * whitelist/TG-hold logic.
 */

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int
dsd_p25p2_mixer_gate(const dsd_state* state, int* encL, int* encR) {
    if (!state || !encL || !encR) {
        return -1;
    }
    *encL = state->p25_p2_audio_allowed[0] ? 0 : 1;
    *encR = state->p25_p2_audio_allowed[1] ? 0 : 1;
    return 0;
}

int
dsd_audio_group_gate_mono(const dsd_opts* opts, const dsd_state* state, unsigned long tg, int enc_in, int* enc_out) {
    if (!opts || !state || !enc_out) {
        return -1;
    }

    int enc = enc_in;

    char mode[8];
    snprintf(mode, sizeof mode, "%s", "");

    // If using allow/whitelist mode, default to block until a matching
    // group explicitly marks this TG as allowed.
    if (opts->trunk_use_allow_list == 1) {
        snprintf(mode, sizeof mode, "%s", "B");
    }

    for (unsigned int gi = 0; gi < state->group_tally; gi++) {
        if (state->group_array[gi].groupNumber == tg) {
            strncpy(mode, state->group_array[gi].groupMode, sizeof(mode) - 1);
            mode[sizeof(mode) - 1] = '\0';
            break;
        }
    }

    // Block if this TG is explicitly on the block list.
    if (strcmp(mode, "B") == 0) {
        enc = 1;
    }

    // TG Hold: when in effect, mute everything except the held TG. If the
    // held TG matches, force unmute to match existing behavior.
    if (state->tg_hold != 0 && state->tg_hold != (uint32_t)tg) {
        enc = 1;
    }
    if (state->tg_hold != 0 && state->tg_hold == (uint32_t)tg) {
        enc = 0;
    }

    *enc_out = enc;
    return 0;
}

int
dsd_audio_group_gate_dual(const dsd_opts* opts, const dsd_state* state, unsigned long tgL, unsigned long tgR,
                          int encL_in, int encR_in, int* encL_out, int* encR_out) {
    if (!encL_out || !encR_out) {
        return -1;
    }
    int rc = 0;
    rc |= dsd_audio_group_gate_mono(opts, state, tgL, encL_in, encL_out);
    rc |= dsd_audio_group_gate_mono(opts, state, tgR, encR_in, encR_out);
    return rc;
}
