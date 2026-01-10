// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/freq_parse.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t
dsd_parse_freq_hz(const char* s) {
    if (!s || !*s) {
        return 0;
    }
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    buf[sizeof buf - 1] = '\0';
    size_t len = strlen(buf);
    if (len == 0) {
        return 0;
    }
    char last = buf[len - 1];
    double factor = 1.0;
    switch (last) {
        case 'g':
        case 'G':
            factor = 1e9;
            buf[len - 1] = '\0';
            break;
        case 'm':
        case 'M':
            factor = 1e6;
            buf[len - 1] = '\0';
            break;
        case 'k':
        case 'K':
            factor = 1e3;
            buf[len - 1] = '\0';
            break;
        default: break;
    }
    double val = atof(buf);
    if (val <= 0.0) {
        return 0;
    }
    double hz = val * factor;
    if (hz <= 0.0) {
        return 0;
    }
    if (hz > (double)UINT32_MAX) {
        hz = (double)UINT32_MAX;
    }
    return (uint32_t)(hz + 0.5);
}
