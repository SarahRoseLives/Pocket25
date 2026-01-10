// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Environment variable helpers for menu subsystem.
 *
 * This header is internal to src/ui/terminal/ and should NOT be installed.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>

// Common helpers to get/set numeric env values with defaults.
int env_get_int(const char* name, int defv);
double env_get_double(const char* name, double defv);
void env_set_int(const char* name, int v);
void env_set_double(const char* name, double v);

// After changing env-backed runtime config, re-parse to apply immediately.
void env_reparse_runtime_cfg(dsd_opts* opts);

// Hex string parsing utility used by key entry callbacks
int parse_hex_u64(const char* s, unsigned long long* out);
