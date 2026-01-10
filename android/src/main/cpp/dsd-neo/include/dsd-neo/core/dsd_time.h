// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Lightweight monotonic time helpers for state-machine timing.
 *
 * Provides monotonic time in seconds and helpers to stamp/clear CC/VC sync
 * times on `dsd_state`.
 */

#pragma once

#include <dsd-neo/core/state_fwd.h>
#include <dsd-neo/platform/timing.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return monotonic time in seconds when available (wall clock fallback).
 */
static inline double
dsd_time_now_monotonic_s(void) {
    return (double)dsd_time_monotonic_ns() / 1e9;
}

/** @brief Stamp current time as control-channel sync (monotonic + wall clock). */
void dsd_mark_cc_sync(dsd_state* state);

/** @brief Stamp current time as voice-channel sync (monotonic + wall clock). */
void dsd_mark_vc_sync(dsd_state* state);

/** @brief Clear control-channel sync timestamps. */
void dsd_clear_cc_sync(dsd_state* state);

/** @brief Clear voice-channel sync timestamps. */
void dsd_clear_vc_sync(dsd_state* state);

#ifdef __cplusplus
}
#endif
