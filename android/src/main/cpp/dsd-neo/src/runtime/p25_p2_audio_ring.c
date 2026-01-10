// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/p25_p2_audio_ring.h>

#include <dsd-neo/core/state.h>

#include <string.h>

void
p25_p2_audio_ring_reset(dsd_state* state, int slot) {
    if (!state) {
        return;
    }

    if (slot < 0 || slot > 1) {
        for (int s = 0; s < 2; s++) {
            state->p25_p2_audio_ring_head[s] = 0;
            state->p25_p2_audio_ring_tail[s] = 0;
            state->p25_p2_audio_ring_count[s] = 0;
        }
        memset(state->p25_p2_audio_ring, 0, sizeof state->p25_p2_audio_ring);
        return;
    }

    state->p25_p2_audio_ring_head[slot] = 0;
    state->p25_p2_audio_ring_tail[slot] = 0;
    state->p25_p2_audio_ring_count[slot] = 0;
    memset(state->p25_p2_audio_ring[slot], 0, sizeof state->p25_p2_audio_ring[slot]);
}

int
p25_p2_audio_ring_push(dsd_state* state, int slot, const float* frame160) {
    if (!state || !frame160 || slot < 0 || slot > 1) {
        return 0;
    }

    if (state->p25_p2_audio_ring_count[slot] >= 3) {
        state->p25_p2_audio_ring_head[slot] = (state->p25_p2_audio_ring_head[slot] + 1) % 3;
        state->p25_p2_audio_ring_count[slot]--;
    }

    int idx = state->p25_p2_audio_ring_tail[slot];
    memcpy(state->p25_p2_audio_ring[slot][idx], frame160, 160U * sizeof(*frame160));
    state->p25_p2_audio_ring_tail[slot] = (state->p25_p2_audio_ring_tail[slot] + 1) % 3;
    state->p25_p2_audio_ring_count[slot]++;
    return 1;
}

int
p25_p2_audio_ring_pop(dsd_state* state, int slot, float* out160) {
    if (!state || !out160 || slot < 0 || slot > 1) {
        return 0;
    }

    if (state->p25_p2_audio_ring_count[slot] <= 0) {
        memset(out160, 0, 160U * sizeof(*out160));
        return 0;
    }

    int idx = state->p25_p2_audio_ring_head[slot];
    memcpy(out160, state->p25_p2_audio_ring[slot][idx], 160U * sizeof(*out160));
    state->p25_p2_audio_ring_head[slot] = (state->p25_p2_audio_ring_head[slot] + 1) % 3;
    state->p25_p2_audio_ring_count[slot]--;
    return 1;
}
