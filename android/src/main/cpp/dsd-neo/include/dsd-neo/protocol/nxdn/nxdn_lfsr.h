// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief NXDN LFSR helpers.
 *
 * Declares NXDN scrambler/descrambler helpers implemented in the NXDN protocol
 * module.
 */

#pragma once

#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

void LFSRN(char* BufferIn, char* BufferOut, dsd_state* state);
void LFSR128n(dsd_state* state);

#ifdef __cplusplus
}
#endif
