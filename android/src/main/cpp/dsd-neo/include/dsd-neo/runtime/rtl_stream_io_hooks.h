// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime hook table for optional RTL stream I/O.
 *
 * Some protocol code wants to read RTL stream samples and query soft squelch
 * power without directly depending on IO backends. The engine installs real
 * hook functions at startup; the runtime provides safe wrappers and fallback
 * behavior when hooks are not installed.
 */
#pragma once

#include <dsd-neo/core/state_fwd.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*read)(void* rtl_ctx, float* out, size_t count, int* out_got);
    double (*return_pwr)(const void* rtl_ctx);
} dsd_rtl_stream_io_hooks;

void dsd_rtl_stream_io_hooks_set(dsd_rtl_stream_io_hooks hooks);

int dsd_rtl_stream_io_hook_read(dsd_state* state, float* out, size_t count, int* out_got);
double dsd_rtl_stream_io_hook_return_pwr(const dsd_state* state);

#ifdef __cplusplus
}
#endif
