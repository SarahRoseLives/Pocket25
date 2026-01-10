// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief GPS/NMEA decode interfaces.
 *
 * Declares GPS helper entrypoints implemented in `src/core/gps/dsd_gps.c`.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dmr_embedded_gps(dsd_opts* opts, dsd_state* state, uint8_t lc_bits[]);
void apx_embedded_gps(dsd_opts* opts, dsd_state* state, uint8_t lc_bits[]);
void lip_protocol_decoder(dsd_opts* opts, dsd_state* state, uint8_t* input);
void nmea_iec_61162_1(dsd_opts* opts, dsd_state* state, uint8_t* input, uint32_t src, int type);
void nmea_harris(dsd_opts* opts, dsd_state* state, uint8_t* input, uint32_t src, int slot);
void harris_gps(dsd_opts* opts, dsd_state* state, int slot, uint8_t* input);

#ifdef __cplusplus
}
#endif
