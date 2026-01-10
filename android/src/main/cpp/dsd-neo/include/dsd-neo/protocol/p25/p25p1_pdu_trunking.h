// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief P25 Phase 1 trunking PDU decoder interfaces.
 *
 * Declares the P25p1 trunking PDU decoder entrypoint implemented in the P25
 * protocol library.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Decode a P25 trunking PDU (MPDU) and update decoder state. */
void p25_decode_pdu_trunking(dsd_opts* opts, dsd_state* state, uint8_t* mpdu_byte);

#ifdef __cplusplus
}
#endif
