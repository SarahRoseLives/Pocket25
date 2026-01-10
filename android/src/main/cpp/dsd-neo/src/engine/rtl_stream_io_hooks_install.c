// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/rtl_stream_io_hooks.h>

#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>

static int
rtl_stream_io_read(void* rtl_ctx, float* out, size_t count, int* out_got) {
    return rtl_stream_read((RtlSdrContext*)rtl_ctx, out, count, out_got);
}

static double
rtl_stream_io_return_pwr(const void* rtl_ctx) {
    return rtl_stream_return_pwr((const RtlSdrContext*)rtl_ctx);
}
#endif

void
dsd_engine_rtl_stream_io_hooks_install(void) {
    dsd_rtl_stream_io_hooks hooks = {0};
#ifdef USE_RTLSDR
    hooks.read = rtl_stream_io_read;
    hooks.return_pwr = rtl_stream_io_return_pwr;
#endif
    dsd_rtl_stream_io_hooks_set(hooks);
}
