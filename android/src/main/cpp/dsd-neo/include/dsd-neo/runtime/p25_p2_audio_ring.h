// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief P25 Phase 2 audio jitter ring helpers.
 *
 * Declares helpers for managing the per-slot fixed-size audio jitter buffer
 * stored in `dsd_state`. Implementations live in the runtime module so both
 * core and protocol call sites can share the logic without introducing a
 * core↔protocol link cycle.
 */

#pragma once

#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset Phase 2 audio jitter ring for one or both slots.
 *
 * @param state Decoder state containing jitter rings.
 * @param slot  Slot index (0/1) or negative to reset both.
 */
void p25_p2_audio_ring_reset(dsd_state* state, int slot);

/**
 * @brief Push one 160-sample float frame into the Phase 2 jitter ring.
 *
 * Drops the oldest frame when the ring is full to keep latency bounded.
 *
 * @return 1 on success, 0 on invalid input.
 */
int p25_p2_audio_ring_push(dsd_state* state, int slot, const float* frame160);

/**
 * @brief Pop one 160-sample float frame from the Phase 2 jitter ring.
 *
 * When empty, fills out160 with zeros and returns 0.
 *
 * @return 1 when a frame was returned; 0 when empty/invalid.
 */
int p25_p2_audio_ring_pop(dsd_state* state, int slot, float* out160);

#ifdef __cplusplus
}
#endif
