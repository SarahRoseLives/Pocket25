// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <dsd-neo/runtime/freq_parse.h>

int
main(void) {
    assert(dsd_parse_freq_hz(NULL) == 0u);
    assert(dsd_parse_freq_hz("") == 0u);
    assert(dsd_parse_freq_hz("0") == 0u);
    assert(dsd_parse_freq_hz("-1") == 0u);
    assert(dsd_parse_freq_hz("abc") == 0u);

    assert(dsd_parse_freq_hz("1") == 1u);
    assert(dsd_parse_freq_hz("1.4") == 1u);
    assert(dsd_parse_freq_hz("1.6") == 2u);

    assert(dsd_parse_freq_hz("1k") == 1000u);
    assert(dsd_parse_freq_hz("1K") == 1000u);
    assert(dsd_parse_freq_hz("2m") == 2000000u);
    assert(dsd_parse_freq_hz("2M") == 2000000u);
    assert(dsd_parse_freq_hz("4g") == 4000000000u);
    assert(dsd_parse_freq_hz("4G") == 4000000000u);

    assert(dsd_parse_freq_hz("4294967295") == UINT32_MAX);
    assert(dsd_parse_freq_hz("4294967296") == UINT32_MAX);
    assert(dsd_parse_freq_hz("5g") == UINT32_MAX);
    return 0;
}
