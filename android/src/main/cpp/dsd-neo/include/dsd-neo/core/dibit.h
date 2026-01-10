// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Dibit/symbol consumption helpers.
 *
 * Declares the dibit reader and soft-decision helpers implemented in
 * `src/core/frames/dsd_dibit.c`.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int getDibit(dsd_opts* opts, dsd_state* state);
int get_dibit_and_analog_signal(dsd_opts* opts, dsd_state* state, int* out_analog_signal);
int getDibitWithReliability(dsd_opts* opts, dsd_state* state, uint8_t* out_reliability);
int getDibitAndSoftSymbol(dsd_opts* opts, dsd_state* state, float* out_soft_symbol);
uint8_t dmr_compute_reliability(const dsd_state* st, float sym);
void soft_symbol_frame_begin(dsd_state* state);
uint16_t soft_symbol_to_viterbi_cost(float symbol, const dsd_state* state, int bit_position);
uint16_t gmsk_soft_symbol_to_viterbi_cost(float symbol, const dsd_state* state);
int digitize(dsd_opts* opts, dsd_state* state, float symbol);
void skipDibit(dsd_opts* opts, dsd_state* state, int count);

#ifdef __cplusplus
}
#endif
