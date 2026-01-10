// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/cli.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/log.h>

#include <stdlib.h>

#if defined(__SSE__) || defined(__SSE2__)
#include <xmmintrin.h>
#endif

void
dsd_bootstrap_enable_ftz_daz_if_enabled(void) {
#if defined(__SSE__) || defined(__SSE2__)
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (!cfg) {
        dsd_neo_config_init(NULL);
        cfg = dsd_neo_get_config();
    }
    if (!cfg || !cfg->ftz_daz_enable) {
        return;
    }
    unsigned int mxcsr = _mm_getcsr();
    mxcsr |= (1u << 15) | (1u << 6); // FTZ | DAZ
    _mm_setcsr(mxcsr);
    LOG_NOTICE("Enabled SSE FTZ/DAZ (env DSD_NEO_FTZ_DAZ)\n");
#endif
}
