// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime hook for pumping UI/control commands.
 *
 * Protocol/DSP code may call dsd_runtime_pump_controls() during long-running
 * loops to keep user controls responsive without depending on UI headers.
 *
 * The default behavior is a safe no-op until a control pump is registered.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dsd_control_pump_fn)(dsd_opts* opts, dsd_state* state);

/**
 * @brief Register (or unregister) the global control pump.
 *
 * Passing NULL unregisters.
 */
void dsd_runtime_set_control_pump(dsd_control_pump_fn fn);

/**
 * @brief Pump pending UI/control commands if a pump is registered.
 *
 * Safe to call even when no pump is registered.
 */
void dsd_runtime_pump_controls(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
