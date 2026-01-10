// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief P25 channel/frequency mapping helpers.
 *
 * Declares small P25 channel mapping and IDEN trust utilities implemented in
 * `src/protocol/p25/p25_frequency.c`.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

long int process_channel_to_freq(dsd_opts* opts, dsd_state* state, int channel);
long int nxdn_channel_to_frequency(dsd_opts* opts, dsd_state* state, uint16_t channel);
long int nxdn_channel_to_frequency_quiet(dsd_state* state, uint16_t channel);

void p25_format_chan_suffix(const dsd_state* state, uint16_t chan, int slot_hint, char* out, size_t outsz);

void p25_reset_iden_tables(dsd_state* state);
void p25_confirm_idens_for_current_site(dsd_state* state);

#ifdef __cplusplus
}
#endif
