// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief NXDN voice decode entrypoint.
 *
 * Declares the voice handler implemented in `src/protocol/nxdn/nxdn_voice.c`.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void nxdn_voice(dsd_opts* opts, dsd_state* state, int voice, uint8_t dbuf[182], const uint8_t* dbuf_reliab);

#ifdef __cplusplus
}
#endif
