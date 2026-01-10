// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Snapshot API for publishing dsd_opts to the UI thread.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the latest published options snapshot.
 * @return Pointer to stable copy, or NULL if none published yet.
 */
const dsd_opts* ui_get_latest_opts_snapshot(void);

#ifdef __cplusplus
}
#endif
