// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* UI command actions — radio domain */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/ui/ui_cmd_dispatch.h>

#include <string.h>
#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>
#endif

static int
ui_handle_ppm_delta(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    int32_t d = 0;
    if (c->n >= (int)sizeof(int32_t)) {
        memcpy(&d, c->data, sizeof(int32_t));
    }
    opts->rtlsdr_ppm_error += d;
    return 1;
}

static int
ui_handle_invert_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    (void)c;
    int inv = opts->inverted_dmr ? 0 : 1;
    opts->inverted_dmr = inv;
    opts->inverted_dpmr = inv;
    opts->inverted_x2tdma = inv;
    opts->inverted_ysf = inv;
    opts->inverted_m17 = inv;
    return 1;
}

static int
ui_handle_mod_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)c;
    if (state->rf_mod == 0) {
        opts->mod_c4fm = 0;
        opts->mod_qpsk = 1;
        opts->mod_gfsk = 0;
        state->rf_mod = 1;
        // P25P1 QPSK: 4800 sym/s - compute SPS from actual demod rate
#ifdef USE_RTLSDR
        int demod_rate = 0;
        if (state->rtl_ctx) {
            demod_rate = (int)rtl_stream_output_rate(state->rtl_ctx);
        }
#else
        int demod_rate = 0;
#endif
        state->samplesPerSymbol = dsd_opts_compute_sps_rate(opts, 4800, demod_rate);
        state->symbolCenter = dsd_opts_symbol_center(state->samplesPerSymbol);
    } else {
        opts->mod_c4fm = 1;
        opts->mod_qpsk = 0;
        opts->mod_gfsk = 0;
        state->rf_mod = 0;
        // Keep current symbol timing unless other code adjusts it
    }
    return 1;
}

static int
ui_handle_mod_p2_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)c;
    // P25P2 TDMA: 6000 sym/s - compute SPS from actual demod rate
#ifdef USE_RTLSDR
    int demod_rate = 0;
    if (state->rtl_ctx) {
        demod_rate = (int)rtl_stream_output_rate(state->rtl_ctx);
    }
#else
    int demod_rate = 0;
#endif
    int sps = dsd_opts_compute_sps_rate(opts, 6000, demod_rate);
    int center = dsd_opts_symbol_center(sps);
    if (state->rf_mod == 0) {
        opts->mod_c4fm = 0;
        opts->mod_qpsk = 1;
        opts->mod_gfsk = 0;
        state->rf_mod = 1;
        state->samplesPerSymbol = sps;
        state->symbolCenter = center;
    } else {
        opts->mod_c4fm = 1;
        opts->mod_qpsk = 0;
        opts->mod_gfsk = 0;
        state->rf_mod = 0;
        state->samplesPerSymbol = sps;
        state->symbolCenter = center;
    }
    return 1;
}

const struct UiCmdReg ui_actions_radio[] = {
    {UI_CMD_PPM_DELTA, ui_handle_ppm_delta},
    {UI_CMD_INVERT_TOGGLE, ui_handle_invert_toggle},
    {UI_CMD_MOD_TOGGLE, ui_handle_mod_toggle},
    {UI_CMD_MOD_P2_TOGGLE, ui_handle_mod_p2_toggle},
    {0, NULL},
};
