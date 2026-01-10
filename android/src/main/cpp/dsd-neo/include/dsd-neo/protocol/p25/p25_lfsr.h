// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief P25 encryption LFSR helper interfaces.
 *
 * Declares the P25-specific LFSR helpers implemented in
 * `src/protocol/p25/phase1/p25p1_ldu2.c`.
 */

#pragma once

#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

void LFSRP(dsd_state* state);
void LFSR128(dsd_state* state);

#ifdef __cplusplus
}
#endif
