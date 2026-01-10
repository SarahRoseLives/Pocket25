// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief P25 state-machine watchdog helpers.
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

/*
 * Background watchdog for the P25 trunking state machine tick.
 * Ensures hangtime/CC-return logic continues even if the main
 * DSP loop is temporarily stalled by upstream I/O.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Attempt to run one P25 state-machine tick when idle.
 *
 * Safe to call frequently; a tick is skipped when one is already in progress.
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 */
void p25_sm_try_tick(dsd_opts* opts, dsd_state* state);

/**
 * @brief Start the background 1 Hz watchdog thread.
 *
 * No-op if already started.
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 */
void p25_sm_watchdog_start(dsd_opts* opts, dsd_state* state);
/** @brief Stop the background watchdog thread (safe to call when stopped). */
void p25_sm_watchdog_stop(void);

/**
 * @brief Return 1 while a P25 SM tick is executing on the current thread.
 */
int p25_sm_in_tick(void);

#ifdef __cplusplus
}
#endif
