// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief DMR UTF-8 text decode helpers shared across modules.
 */

#pragma once

#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void utf8_to_text(dsd_state* state, uint8_t wr, uint16_t len, uint8_t* input);

#ifdef __cplusplus
}
#endif
