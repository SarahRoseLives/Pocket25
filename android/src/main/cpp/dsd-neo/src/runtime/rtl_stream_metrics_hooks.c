// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/rtl_stream_metrics_hooks.h>

static dsd_rtl_stream_metrics_hooks g_rtl_stream_metrics_hooks = {0};

void
dsd_rtl_stream_metrics_hooks_set(dsd_rtl_stream_metrics_hooks hooks) {
    g_rtl_stream_metrics_hooks = hooks;
}

unsigned int
dsd_rtl_stream_metrics_hook_output_rate_hz(void) {
    if (g_rtl_stream_metrics_hooks.output_rate_hz) {
        return g_rtl_stream_metrics_hooks.output_rate_hz();
    }
    return 0;
}

int
dsd_rtl_stream_metrics_hook_dsp_get(int* out_cqpsk_enable, int* out_fll_enable, int* out_ted_enable) {
    if (g_rtl_stream_metrics_hooks.dsp_get) {
        return g_rtl_stream_metrics_hooks.dsp_get(out_cqpsk_enable, out_fll_enable, out_ted_enable);
    }
    if (out_cqpsk_enable) {
        *out_cqpsk_enable = 0;
    }
    if (out_fll_enable) {
        *out_fll_enable = 0;
    }
    if (out_ted_enable) {
        *out_ted_enable = 0;
    }
    return 0;
}

int
dsd_rtl_stream_metrics_hook_ted_bias(void) {
    if (g_rtl_stream_metrics_hooks.ted_bias) {
        return g_rtl_stream_metrics_hooks.ted_bias();
    }
    return 0;
}

double
dsd_rtl_stream_metrics_hook_snr_bias_evm(void) {
    if (g_rtl_stream_metrics_hooks.snr_bias_evm) {
        return g_rtl_stream_metrics_hooks.snr_bias_evm();
    }
    return 2.43;
}

double
dsd_rtl_stream_metrics_hook_snr_c4fm_db(void) {
    if (g_rtl_stream_metrics_hooks.snr_c4fm_db) {
        return g_rtl_stream_metrics_hooks.snr_c4fm_db();
    }
    return -100.0;
}

double
dsd_rtl_stream_metrics_hook_snr_c4fm_eye_db(void) {
    if (g_rtl_stream_metrics_hooks.snr_c4fm_eye_db) {
        return g_rtl_stream_metrics_hooks.snr_c4fm_eye_db();
    }
    return -100.0;
}

double
dsd_rtl_stream_metrics_hook_snr_cqpsk_db(void) {
    if (g_rtl_stream_metrics_hooks.snr_cqpsk_db) {
        return g_rtl_stream_metrics_hooks.snr_cqpsk_db();
    }
    return -100.0;
}

double
dsd_rtl_stream_metrics_hook_snr_gfsk_db(void) {
    if (g_rtl_stream_metrics_hooks.snr_gfsk_db) {
        return g_rtl_stream_metrics_hooks.snr_gfsk_db();
    }
    return -100.0;
}

double
dsd_rtl_stream_metrics_hook_snr_qpsk_const_db(void) {
    if (g_rtl_stream_metrics_hooks.snr_qpsk_const_db) {
        return g_rtl_stream_metrics_hooks.snr_qpsk_const_db();
    }
    return -100.0;
}

void
dsd_rtl_stream_metrics_hook_p25p1_ber_update(int ok_delta, int err_delta) {
    if (g_rtl_stream_metrics_hooks.p25p1_ber_update) {
        g_rtl_stream_metrics_hooks.p25p1_ber_update(ok_delta, err_delta);
    }
}

void
dsd_rtl_stream_metrics_hook_p25p2_err_update(int slot, int facch_ok, int facch_err, int sacch_ok, int sacch_err,
                                             int voice_err) {
    if (g_rtl_stream_metrics_hooks.p25p2_err_update) {
        g_rtl_stream_metrics_hooks.p25p2_err_update(slot, facch_ok, facch_err, sacch_ok, sacch_err, voice_err);
    }
}
