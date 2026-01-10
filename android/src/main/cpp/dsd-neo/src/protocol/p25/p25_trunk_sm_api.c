// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/protocol/p25/p25_trunk_sm_api.h>

static p25_sm_api g_api;

void
p25_sm_set_api(p25_sm_api api) {
    g_api = api;
}

p25_sm_api
p25_sm_get_api(void) {
    return g_api;
}

void
p25_sm_reset_api(void) {
    g_api = (p25_sm_api){0};
}
