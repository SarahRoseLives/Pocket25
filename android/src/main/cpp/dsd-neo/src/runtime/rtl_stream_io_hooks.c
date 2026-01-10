// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/rtl_stream_io_hooks.h>

#include <dsd-neo/core/state.h>

static dsd_rtl_stream_io_hooks g_rtl_stream_io_hooks = {0};

void
dsd_rtl_stream_io_hooks_set(dsd_rtl_stream_io_hooks hooks) {
    g_rtl_stream_io_hooks = hooks;
}

int
dsd_rtl_stream_io_hook_read(dsd_state* state, float* out, size_t count, int* out_got) {
    int got_tmp = 0;
    int* out_got_ptr = out_got ? out_got : &got_tmp;
    *out_got_ptr = 0;

    if (!state || !state->rtl_ctx) {
        return 0;
    }
    if (!g_rtl_stream_io_hooks.read || !out || count == 0) {
        return 0;
    }

    return g_rtl_stream_io_hooks.read((void*)state->rtl_ctx, out, count, out_got_ptr);
}

double
dsd_rtl_stream_io_hook_return_pwr(const dsd_state* state) {
    if (!state || !state->rtl_ctx) {
        return 0.0;
    }
    if (!g_rtl_stream_io_hooks.return_pwr) {
        return 0.0;
    }

    return g_rtl_stream_io_hooks.return_pwr((const void*)state->rtl_ctx);
}
