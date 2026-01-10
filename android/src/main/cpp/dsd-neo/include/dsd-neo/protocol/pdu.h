// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Shared protocol PDU decode helpers.
 *
 * Declares generic PDU decoders implemented in `src/protocol/dmr/dmr_pdu.c`.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void decode_ip_pdu(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* input);
void decode_cellocator(dsd_opts* opts, dsd_state* state, uint8_t* input, int len);
void decode_ars(dsd_opts* opts, dsd_state* state, uint8_t* input, int len);

#ifdef __cplusplus
}
#endif
