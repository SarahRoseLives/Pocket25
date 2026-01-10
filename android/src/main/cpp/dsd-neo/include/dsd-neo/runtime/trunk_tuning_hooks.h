// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime hook table for trunking tune side effects.
 *
 * Protocol state machines may need to request retunes without depending on
 * IO/control headers or linking IO backends. The engine (or tests) installs
 * real hook functions at startup; the runtime provides safe wrappers and
 * fallback behavior when hooks are not installed.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*tune_to_freq)(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps);
    void (*tune_to_cc)(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps);
    void (*return_to_cc)(dsd_opts* opts, dsd_state* state);
} dsd_trunk_tuning_hooks;

void dsd_trunk_tuning_hooks_set(dsd_trunk_tuning_hooks hooks);

void dsd_trunk_tuning_hook_tune_to_freq(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps);
void dsd_trunk_tuning_hook_tune_to_cc(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps);
void dsd_trunk_tuning_hook_return_to_cc(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
