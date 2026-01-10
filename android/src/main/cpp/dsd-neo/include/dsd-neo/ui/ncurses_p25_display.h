// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/**
 * @file ncurses_p25_display.h
 * @brief P25 protocol display API for ncurses UI.
 *
 * Provides functions for rendering P25-specific metrics, neighbor lists,
 * IDEN bandplans, and control channel candidates.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Print P25 P1/P2 decoder metrics. Returns number of lines printed. */
int ui_print_p25_metrics(const dsd_opts* opts, const dsd_state* state);

/** Print P25 control channel candidate list. */
void ui_print_p25_cc_candidates(const dsd_opts* opts, const dsd_state* state);

/** Print P25 neighbor site list. */
void ui_print_p25_neighbors(const dsd_opts* opts, const dsd_state* state);

/** Print P25 IDEN bandplan table. */
void ui_print_p25_iden_plan(const dsd_opts* opts, const dsd_state* state);

/** Classify whether a channel mapping matches the active P25 IDEN parameters. */
int ui_is_iden_channel(const dsd_state* state, int ch16, long int freq);

/** Match a channel index against P25 IDEN table entries. */
int ui_match_iden_channel(const dsd_state* state, int ch16, long int freq, int* out_iden);

/** Try to resolve an active VC frequency from current state. */
long int ui_guess_active_vc_freq(const dsd_state* state);

/** Compute average P25 P1 voice errors. */
int compute_p25p1_voice_avg_err(const dsd_state* s, double* out_avg);

/** Compute average P25 P2 voice errors for a slot. */
int compute_p25p2_voice_avg_err(const dsd_state* s, int slot, double* out_avg);

#ifdef __cplusplus
}
#endif
