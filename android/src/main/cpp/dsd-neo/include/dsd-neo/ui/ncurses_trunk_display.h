// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/**
 * @file ncurses_trunk_display.h
 * @brief Trunk system display API for ncurses UI.
 *
 * Provides functions for rendering trunking-related information
 * such as learned LCN/channel frequency mappings.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Print learned trunking LCNs and their mapped frequencies. */
void ui_print_learned_lcns(const dsd_opts* opts, const dsd_state* state);

#ifdef __cplusplus
}
#endif
