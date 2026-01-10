// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/**
 * @file ncurses_snr.h
 * @brief SNR display API for ncurses UI.
 *
 * Provides SNR history tracking, sparkline visualization, and compact meter
 * rendering for different modulation types.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Push a new SNR sample to the history ring for the given modulation (0=C4FM, 1=QPSK, 2=GFSK). */
void snr_hist_push(int mod, double snr);

/** Render a sparkline showing recent SNR history. */
void print_snr_sparkline(const dsd_opts* opts, int mod);

/** Render a compact single-glyph SNR meter. */
void print_snr_meter(const dsd_opts* opts, double snr_db, int mod);

#ifdef __cplusplus
}
#endif
