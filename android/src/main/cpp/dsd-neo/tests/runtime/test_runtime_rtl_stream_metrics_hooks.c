// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>

#include <dsd-neo/runtime/rtl_stream_metrics_hooks.h>

static int g_output_rate_calls = 0;
static int g_dsp_get_calls = 0;
static int g_ted_bias_calls = 0;
static int g_snr_bias_calls = 0;
static int g_snr_c4fm_calls = 0;
static int g_snr_c4fm_eye_calls = 0;
static int g_snr_cqpsk_calls = 0;
static int g_snr_gfsk_calls = 0;
static int g_snr_qpsk_const_calls = 0;
static int g_p25p1_ber_calls = 0;
static int g_p25p2_err_calls = 0;

static int g_p25p1_ok_delta = 0;
static int g_p25p1_err_delta = 0;

static int g_p25p2_slot = 0;
static int g_p25p2_facch_ok_delta = 0;
static int g_p25p2_facch_err_delta = 0;
static int g_p25p2_sacch_ok_delta = 0;
static int g_p25p2_sacch_err_delta = 0;
static int g_p25p2_voice_err_delta = 0;

static unsigned int
fake_output_rate_hz(void) {
    g_output_rate_calls++;
    return 24000U;
}

static int
fake_dsp_get(int* out_cqpsk_enable, int* out_fll_enable, int* out_ted_enable) {
    g_dsp_get_calls++;
    if (out_cqpsk_enable) {
        *out_cqpsk_enable = 1;
    }
    if (out_fll_enable) {
        *out_fll_enable = 2;
    }
    if (out_ted_enable) {
        *out_ted_enable = 3;
    }
    return -7;
}

static int
fake_ted_bias(void) {
    g_ted_bias_calls++;
    return 123;
}

static double
fake_snr_bias_evm(void) {
    g_snr_bias_calls++;
    return 9.87;
}

static double
fake_snr_c4fm_db(void) {
    g_snr_c4fm_calls++;
    return 12.34;
}

static double
fake_snr_c4fm_eye_db(void) {
    g_snr_c4fm_eye_calls++;
    return 56.78;
}

static double
fake_snr_cqpsk_db(void) {
    g_snr_cqpsk_calls++;
    return 23.45;
}

static double
fake_snr_gfsk_db(void) {
    g_snr_gfsk_calls++;
    return 34.56;
}

static double
fake_snr_qpsk_const_db(void) {
    g_snr_qpsk_const_calls++;
    return 45.67;
}

static void
fake_p25p1_ber_update(int ok_delta, int err_delta) {
    g_p25p1_ber_calls++;
    g_p25p1_ok_delta = ok_delta;
    g_p25p1_err_delta = err_delta;
}

static void
fake_p25p2_err_update(int slot, int facch_ok_delta, int facch_err_delta, int sacch_ok_delta, int sacch_err_delta,
                      int voice_err_delta) {
    g_p25p2_err_calls++;
    g_p25p2_slot = slot;
    g_p25p2_facch_ok_delta = facch_ok_delta;
    g_p25p2_facch_err_delta = facch_err_delta;
    g_p25p2_sacch_ok_delta = sacch_ok_delta;
    g_p25p2_sacch_err_delta = sacch_err_delta;
    g_p25p2_voice_err_delta = voice_err_delta;
}

int
main(void) {
    // Default behavior with hooks unset
    dsd_rtl_stream_metrics_hooks_set((dsd_rtl_stream_metrics_hooks){0});

    assert(dsd_rtl_stream_metrics_hook_output_rate_hz() == 0U);

    int cqpsk = -1;
    int fll = -1;
    int ted = -1;
    assert(dsd_rtl_stream_metrics_hook_dsp_get(&cqpsk, &fll, &ted) == 0);
    assert(cqpsk == 0);
    assert(fll == 0);
    assert(ted == 0);

    assert(dsd_rtl_stream_metrics_hook_ted_bias() == 0);

    assert(dsd_rtl_stream_metrics_hook_snr_bias_evm() == 2.43);

    assert(dsd_rtl_stream_metrics_hook_snr_c4fm_db() == -100.0);
    assert(dsd_rtl_stream_metrics_hook_snr_c4fm_eye_db() == -100.0);
    assert(dsd_rtl_stream_metrics_hook_snr_cqpsk_db() == -100.0);
    assert(dsd_rtl_stream_metrics_hook_snr_gfsk_db() == -100.0);
    assert(dsd_rtl_stream_metrics_hook_snr_qpsk_const_db() == -100.0);

    dsd_rtl_stream_metrics_hook_p25p1_ber_update(1, 0);
    dsd_rtl_stream_metrics_hook_p25p2_err_update(0, 1, 0, 0, 0, 0);

    // Installed hooks should be invoked through wrappers
    g_output_rate_calls = 0;
    g_dsp_get_calls = 0;
    g_ted_bias_calls = 0;
    g_snr_bias_calls = 0;
    g_snr_c4fm_calls = 0;
    g_snr_c4fm_eye_calls = 0;
    g_snr_cqpsk_calls = 0;
    g_snr_gfsk_calls = 0;
    g_snr_qpsk_const_calls = 0;
    g_p25p1_ber_calls = 0;
    g_p25p2_err_calls = 0;

    dsd_rtl_stream_metrics_hooks hooks = {0};
    hooks.output_rate_hz = fake_output_rate_hz;
    hooks.dsp_get = fake_dsp_get;
    hooks.ted_bias = fake_ted_bias;
    hooks.snr_bias_evm = fake_snr_bias_evm;
    hooks.snr_c4fm_db = fake_snr_c4fm_db;
    hooks.snr_c4fm_eye_db = fake_snr_c4fm_eye_db;
    hooks.snr_cqpsk_db = fake_snr_cqpsk_db;
    hooks.snr_gfsk_db = fake_snr_gfsk_db;
    hooks.snr_qpsk_const_db = fake_snr_qpsk_const_db;
    hooks.p25p1_ber_update = fake_p25p1_ber_update;
    hooks.p25p2_err_update = fake_p25p2_err_update;
    dsd_rtl_stream_metrics_hooks_set(hooks);

    assert(dsd_rtl_stream_metrics_hook_output_rate_hz() == 24000U);
    assert(g_output_rate_calls == 1);

    cqpsk = fll = ted = 0;
    assert(dsd_rtl_stream_metrics_hook_dsp_get(&cqpsk, &fll, &ted) == -7);
    assert(g_dsp_get_calls == 1);
    assert(cqpsk == 1);
    assert(fll == 2);
    assert(ted == 3);

    assert(dsd_rtl_stream_metrics_hook_ted_bias() == 123);
    assert(g_ted_bias_calls == 1);

    assert(dsd_rtl_stream_metrics_hook_snr_bias_evm() == 9.87);
    assert(g_snr_bias_calls == 1);

    assert(dsd_rtl_stream_metrics_hook_snr_c4fm_db() == 12.34);
    assert(g_snr_c4fm_calls == 1);

    assert(dsd_rtl_stream_metrics_hook_snr_c4fm_eye_db() == 56.78);
    assert(g_snr_c4fm_eye_calls == 1);

    assert(dsd_rtl_stream_metrics_hook_snr_cqpsk_db() == 23.45);
    assert(g_snr_cqpsk_calls == 1);

    assert(dsd_rtl_stream_metrics_hook_snr_gfsk_db() == 34.56);
    assert(g_snr_gfsk_calls == 1);

    assert(dsd_rtl_stream_metrics_hook_snr_qpsk_const_db() == 45.67);
    assert(g_snr_qpsk_const_calls == 1);

    g_p25p1_ok_delta = 0;
    g_p25p1_err_delta = 0;
    dsd_rtl_stream_metrics_hook_p25p1_ber_update(7, 9);
    assert(g_p25p1_ber_calls == 1);
    assert(g_p25p1_ok_delta == 7);
    assert(g_p25p1_err_delta == 9);

    g_p25p2_slot = 0;
    g_p25p2_facch_ok_delta = 0;
    g_p25p2_facch_err_delta = 0;
    g_p25p2_sacch_ok_delta = 0;
    g_p25p2_sacch_err_delta = 0;
    g_p25p2_voice_err_delta = 0;
    dsd_rtl_stream_metrics_hook_p25p2_err_update(1, 2, 3, 4, 5, 6);
    assert(g_p25p2_err_calls == 1);
    assert(g_p25p2_slot == 1);
    assert(g_p25p2_facch_ok_delta == 2);
    assert(g_p25p2_facch_err_delta == 3);
    assert(g_p25p2_sacch_ok_delta == 4);
    assert(g_p25p2_sacch_err_delta == 5);
    assert(g_p25p2_voice_err_delta == 6);

    return 0;
}
