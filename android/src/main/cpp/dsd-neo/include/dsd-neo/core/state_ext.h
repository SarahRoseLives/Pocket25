// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Extension slots for per-module `dsd_state` data.
 *
 * Provides a small mechanism for features/modules to attach per-state
 * allocations without continually expanding the core `dsd_state` struct.
 */

#pragma once

#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of extension slots.
 *
 * Keep this value stable: increasing it changes the size/layout of `dsd_state`.
 */
enum { DSD_STATE_EXT_MAX = 32 };

/**
 * @brief Stable IDs for extension slots.
 *
 * Values must be in the range [0, DSD_STATE_EXT_MAX).
 *
 * ID allocation policy (keep stable):
 * - 0-7: engine
 * - 8-15: io
 * - 16-23: ui
 * - 24-31: protocols
 *
 * When adding a new ID:
 * - Assign an explicit numeric value.
 * - Never renumber existing IDs.
 * - Keep within your module's reserved range and < DSD_STATE_EXT_MAX.
 */
typedef enum dsd_state_ext_id {
    DSD_STATE_EXT_ENGINE_START_MS = 0,
    DSD_STATE_EXT_ENGINE_TRUNK_CC_CANDIDATES = 1,
    DSD_STATE_EXT_PROTO_NXDN_TRUNK_DIAG = 24,
} dsd_state_ext_id;

typedef void (*dsd_state_ext_cleanup_fn)(void*);

#define DSD_STATE_EXT_GET_AS(type, state, id) ((type*)dsd_state_ext_get((state), (id)))

void* dsd_state_ext_get(dsd_state* state, dsd_state_ext_id id);

int dsd_state_ext_set(dsd_state* state, dsd_state_ext_id id, void* ptr, dsd_state_ext_cleanup_fn cleanup);

void dsd_state_ext_free_all(dsd_state* state);

#ifdef __cplusplus
} /* extern "C" */
#endif
