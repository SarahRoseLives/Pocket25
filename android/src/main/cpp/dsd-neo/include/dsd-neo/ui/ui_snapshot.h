// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Demod → UI snapshot API for stable read-only state.
 *
 * Publishes deep-copied snapshots of `dsd_state` for the UI thread to read
 * without racing live decoder state.
 */

#pragma once

#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Obtain the latest snapshot for drawing.
 *
 * The returned pointer remains valid until the next call from the same
 * thread. Returns NULL if no snapshot has been published yet.
 */
const dsd_state* ui_get_latest_snapshot(void);

#ifdef __cplusplus
}
#endif
