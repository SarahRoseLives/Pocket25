// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/**
 * @file ncurses_visualizers.h
 * @brief Visualizer panel API for ncurses UI.
 *
 * Provides functions for rendering RTL-SDR visualization panels including
 * constellation diagram, eye diagram, FSK histogram, and spectrum analyzer.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Render IQ constellation diagram (requires USE_RTLSDR). */
void print_constellation_view(dsd_opts* opts, dsd_state* state);

/** Render eye diagram for C4FM/FSK (requires USE_RTLSDR). */
void print_eye_view(dsd_opts* opts, dsd_state* state);

/** Render FSK 4-level histogram (requires USE_RTLSDR). */
void print_fsk_hist_view(void);

/** Render spectrum analyzer (requires USE_RTLSDR). */
void print_spectrum_view(dsd_opts* opts);

#ifdef __cplusplus
}
#endif
