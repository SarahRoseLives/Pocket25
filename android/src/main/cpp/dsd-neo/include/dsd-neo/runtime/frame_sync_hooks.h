// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime hook table for frame-sync side effects.
 *
 * DSP frame-sync code may need to trigger protocol-specific actions without
 * depending directly on protocol headers. The engine installs the real hook
 * functions at startup; the runtime provides safe no-op wrappers until then.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*p25_sm_try_tick)(dsd_opts* opts, dsd_state* state);
    void (*p25_sm_on_release)(dsd_opts* opts, dsd_state* state);
    void (*eot_cc)(dsd_opts* opts, dsd_state* state);
    void (*no_carrier)(dsd_opts* opts, dsd_state* state);
} dsd_frame_sync_hooks;

void dsd_frame_sync_hooks_set(dsd_frame_sync_hooks hooks);

void dsd_frame_sync_hook_p25_sm_try_tick(dsd_opts* opts, dsd_state* state);
void dsd_frame_sync_hook_p25_sm_on_release(dsd_opts* opts, dsd_state* state);
void dsd_frame_sync_hook_eot_cc(dsd_opts* opts, dsd_state* state);
void dsd_frame_sync_hook_no_carrier(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
