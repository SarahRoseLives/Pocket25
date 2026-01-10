// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime hook table for optional rigctl queries.
 *
 * Protocol code should not depend on IO headers directly. The engine installs
 * real hook functions at startup; the runtime provides safe wrappers and
 * fallback behavior when hooks are not installed.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    long int (*get_current_freq_hz)(const dsd_opts* opts);
} dsd_rigctl_query_hooks;

void dsd_rigctl_query_hooks_set(dsd_rigctl_query_hooks hooks);

long int dsd_rigctl_query_hook_get_current_freq_hz(const dsd_opts* opts);

#ifdef __cplusplus
}
#endif
