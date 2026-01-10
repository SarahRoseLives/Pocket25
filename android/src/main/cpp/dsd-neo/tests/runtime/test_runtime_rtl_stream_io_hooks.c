// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdlib.h>

#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/rtl_stream_io_hooks.h>

static int g_read_calls = 0;
static int g_return_pwr_calls = 0;
static const void* g_last_rtl_ctx = NULL;

static int
fake_read(void* rtl_ctx, float* out, size_t count, int* out_got) {
    g_read_calls++;
    g_last_rtl_ctx = rtl_ctx;
    if (out && count > 0) {
        out[0] = 42.0f;
    }
    if (out_got) {
        *out_got = (count > 0) ? 1 : 0;
    }
    return 0;
}

static double
fake_return_pwr(const void* rtl_ctx) {
    g_return_pwr_calls++;
    g_last_rtl_ctx = rtl_ctx;
    return 123.45;
}

int
main(void) {
    dsd_rtl_stream_io_hooks_set((dsd_rtl_stream_io_hooks){0});

    dsd_state* state = calloc(1, sizeof(*state));
    assert(state != NULL);
    int got = 123;
    float sample = -1.0f;

    assert(dsd_rtl_stream_io_hook_read(state, &sample, 1, &got) == 0);
    assert(got == 0);
    assert(dsd_rtl_stream_io_hook_return_pwr(state) == 0.0);

    int dummy = 0;
    state->rtl_ctx = (struct RtlSdrContext*)&dummy;

    got = 123;
    sample = -1.0f;
    assert(dsd_rtl_stream_io_hook_read(state, &sample, 1, &got) == 0);
    assert(got == 0);
    assert(dsd_rtl_stream_io_hook_return_pwr(state) == 0.0);

    g_read_calls = 0;
    g_return_pwr_calls = 0;
    g_last_rtl_ctx = NULL;
    dsd_rtl_stream_io_hooks_set((dsd_rtl_stream_io_hooks){
        .read = fake_read,
        .return_pwr = fake_return_pwr,
    });

    got = 0;
    sample = 0.0f;
    assert(dsd_rtl_stream_io_hook_read(state, &sample, 1, &got) == 0);
    assert(g_read_calls == 1);
    assert(got == 1);
    assert(sample == 42.0f);
    assert(g_last_rtl_ctx == (const void*)state->rtl_ctx);

    assert(dsd_rtl_stream_io_hook_return_pwr(state) == 123.45);
    assert(g_return_pwr_calls == 1);
    assert(g_last_rtl_ctx == (const void*)state->rtl_ctx);

    dsd_state_ext_free_all(state);
    free(state);
    return 0;
}
