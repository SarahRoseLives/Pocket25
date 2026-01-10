// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct p25_sm_api {
    void (*init)(dsd_opts* opts, dsd_state* state);
    void (*on_group_grant)(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int tg, int src);
    void (*on_indiv_grant)(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int dst, int src);
    void (*on_release)(dsd_opts* opts, dsd_state* state);
    void (*on_neighbor_update)(dsd_opts* opts, dsd_state* state, const long* freqs, int count);
    int (*next_cc_candidate)(dsd_state* state, long* out_freq);
    void (*tick)(dsd_opts* opts, dsd_state* state);
} p25_sm_api;

void p25_sm_set_api(p25_sm_api api);
p25_sm_api p25_sm_get_api(void);
void p25_sm_reset_api(void);

#ifdef __cplusplus
}
#endif
