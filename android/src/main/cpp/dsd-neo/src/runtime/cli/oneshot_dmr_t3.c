// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief DMR TIII LCN calculator one-shot utility - moved from apps/dsd-cli/main.c
 *        to make runtime CLI self-contained.
 */

#include <dsd-neo/runtime/cli.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/log.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Local helpers -----------------------------------------------------------

static int
cmp_long(const void* a, const void* b) {
    long aa = *(const long*)a;
    long bb = *(const long*)b;
    if (aa < bb) {
        return -1;
    }
    if (aa > bb) {
        return 1;
    }
    return 0;
}

static long
nearest_125(long hz) {
    // round to nearest 125 Hz
    long q = (hz + (hz >= 0 ? 62 : -62)) / 125;
    return q * 125;
}

static long
infer_step_125(const long* f, int n) {
    // Infer channel spacing, snapped to 125 Hz grid.
    // Use the smallest positive rounded diff as conservative step.
    long best = 0;
    for (int i = 1; i < n; i++) {
        long d = f[i] - f[i - 1];
        if (d <= 0) {
            continue;
        }
        long r = nearest_125(d);
        if (r <= 0) {
            continue;
        }
        if (best == 0 || r < best) {
            best = r;
        }
    }
    return best;
}

// --- Public API --------------------------------------------------------------

int
dsd_cli_calc_dmr_t3_lcn_from_csv(const char* path) {
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (!cfg) {
        dsd_neo_config_init(NULL);
        cfg = dsd_neo_get_config();
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        LOG_ERROR("LCN calc: unable to open '%s'\n", path);
        return 1;
    }
    // Parse first numeric field per line as frequency in Hz (accept raw or with separators)
    long freqs[4096];
    int nf = 0;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        // Skip empty/comment/header
        int has_digit = 0;
        for (char* p = line; *p; ++p) {
            if ((*p >= '0' && *p <= '9')) {
                has_digit = 1;
                break;
            }
        }
        if (!has_digit) {
            continue;
        }
        // Extract first field
        char* p = line;
        // Skip leading non-digit (allow +/-)
        while (*p && !((*p >= '0' && *p <= '9') || *p == '+' || *p == '-')) {
            p++;
        }
        if (!*p) {
            continue;
        }
        // Read as double to accept MHz; if < 1e6 assume MHz
        double val = 0.0;
        char* end = p;
        val = strtod(p, &end);
        if (end == p) {
            continue;
        }
        long hz = (long)llround(val);
        if (val < 1e5) {
            // probably MHz; convert
            hz = (long)llround(val * 1000000.0);
        }
        if (hz <= 0) {
            continue;
        }
        if (nf < (int)(sizeof(freqs) / sizeof(freqs[0]))) {
            freqs[nf++] = hz;
        }
    }
    fclose(fp);
    if (nf < 1) {
        LOG_ERROR("LCN calc: no frequencies parsed from '%s'\n", path);
        return 2;
    }
    qsort(freqs, nf, sizeof(long), cmp_long);
    // Unique
    int m = 0;
    for (int i = 0; i < nf; i++) {
        if (m == 0 || freqs[i] != freqs[m - 1]) {
            freqs[m++] = freqs[i];
        }
    }
    nf = m;
    if (nf == 1) {
        long start_lcn = (cfg && cfg->dmr_t3_start_lcn > 0) ? cfg->dmr_t3_start_lcn : 1;
        printf("lcn,freq\n%ld,%ld\n", start_lcn, freqs[0]);
        return 0;
    }
    // Infer step or take from env
    long step = 0;
    if (cfg && cfg->dmr_t3_step_hz_is_set && cfg->dmr_t3_step_hz > 0) {
        step = cfg->dmr_t3_step_hz;
    }
    if (step <= 0) {
        step = infer_step_125(freqs, nf);
    }
    if (step <= 0) {
        LOG_ERROR("LCN calc: could not infer channel step. Provide DSD_NEO_DMR_T3_STEP_HZ.\n");
        return 3;
    }
    // Anchors
    long cc_freq = (cfg && cfg->dmr_t3_cc_freq_is_set) ? cfg->dmr_t3_cc_freq_hz : 0;
    long cc_lcn = (cfg && cfg->dmr_t3_cc_lcn_is_set) ? cfg->dmr_t3_cc_lcn : 0;
    long start_lcn = (cfg && cfg->dmr_t3_start_lcn > 0) ? cfg->dmr_t3_start_lcn : 1;

    long base_freq = freqs[0];
    long base_lcn = start_lcn;
    if (cc_freq > 0 && cc_lcn > 0) {
        // Align base so that cc maps to cc_lcn
        // base_lcn = cc_lcn - round((cc_freq - base_freq)/step)
        long delta = cc_freq - base_freq;
        double denom = (double)step;
        if (denom == 0.0) {
            LOG_ERROR("LCN calc: invalid step (0) during anchor alignment.\n");
            return 3;
        }
        // NOLINTNEXTLINE(clang-analyzer-optin.taint.TaintedDiv)
        long idx = (long)llround((double)delta / denom);
        base_lcn = cc_lcn - idx;
    }

    // Emit mapping sorted by LCN
    printf("lcn,freq\n");
    for (int i = 0; i < nf; i++) {
        long delta = freqs[i] - base_freq;
        double denom = (double)step;
        if (denom == 0.0) {
            LOG_ERROR("LCN calc: invalid step (0) during mapping.\n");
            return 3;
        }
        // NOLINTNEXTLINE(clang-analyzer-optin.taint.TaintedDiv)
        long idx = (long)llround((double)delta / denom);
        long lcn = base_lcn + idx;
        printf("%ld,%ld\n", lcn, freqs[i]);
    }
    return 0;
}
