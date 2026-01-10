// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Telemetry hooks for DSP/protocol to publish state.
 *
 * This header declares the telemetry hook functions that DSP and protocol
 * modules use to publish state snapshots. The runtime module provides a small
 * hook table and safe wrappers that default to no-ops when the UI is not
 * linked/running.
 *
 * DSP and protocol code should include this header rather than UI headers
 * to maintain proper dependency direction: DSP/protocol -> runtime (hooks).
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*publish_snapshot)(const dsd_state* state);
    void (*publish_opts_snapshot)(const dsd_opts* opts);
    void (*request_redraw)(void);
} dsd_telemetry_hooks;

void dsd_telemetry_hooks_set(dsd_telemetry_hooks hooks);

/**
 * @brief Publish a snapshot of the current demod state for the UI.
 *
 * Thread-safe. Called from decoder threads to update UI display.
 */
void ui_publish_snapshot(const dsd_state* state);

/**
 * @brief Publish a snapshot of options for the UI.
 *
 * Thread-safe. Called when options change and UI needs to reflect them.
 */
void ui_publish_opts_snapshot(const dsd_opts* opts);

/**
 * @brief Request a UI redraw from demod/decoder side.
 *
 * Marks the UI dirty so it redraws on the next refresh cycle.
 */
void ui_request_redraw(void);

/**
 * @brief Publish opts/state snapshots and request a redraw.
 *
 * Convenience function combining snapshot publishing with redraw request.
 * Thread-safe.
 */
void ui_publish_both_and_redraw(const dsd_opts* opts, const dsd_state* state);

#ifdef __cplusplus
}
#endif
