// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/**
 * @file ncurses_dsp_display.h
 * @brief DSP status panel display API.
 *
 * Provides functions for rendering the DSP status panel showing RTL-SDR
 * pipeline state.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Render compact DSP status panel (RTL-SDR pipeline state). */
void print_dsp_status(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
