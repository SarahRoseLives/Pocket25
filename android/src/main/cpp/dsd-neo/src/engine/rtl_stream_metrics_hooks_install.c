// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/rtl_stream_metrics_hooks.h>

#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>
unsigned int dsd_rtl_stream_output_rate(void);

static int
rtl_stream_metrics_ted_bias(void) {
    return rtl_stream_ted_bias(NULL);
}
#endif

void
dsd_engine_rtl_stream_metrics_hooks_install(void) {
    dsd_rtl_stream_metrics_hooks hooks = {0};
#ifdef USE_RTLSDR
    hooks.output_rate_hz = dsd_rtl_stream_output_rate;
    hooks.dsp_get = rtl_stream_dsp_get;
    hooks.ted_bias = rtl_stream_metrics_ted_bias;
    hooks.snr_bias_evm = rtl_stream_get_snr_bias_evm;
    hooks.snr_c4fm_db = rtl_stream_get_snr_c4fm;
    hooks.snr_c4fm_eye_db = rtl_stream_estimate_snr_c4fm_eye;
    hooks.snr_cqpsk_db = rtl_stream_get_snr_cqpsk;
    hooks.snr_gfsk_db = rtl_stream_get_snr_gfsk;
    hooks.snr_qpsk_const_db = rtl_stream_estimate_snr_qpsk_const;
    hooks.p25p1_ber_update = rtl_stream_p25p1_ber_update;
    hooks.p25p2_err_update = rtl_stream_p25p2_err_update;
#endif
    dsd_rtl_stream_metrics_hooks_set(hooks);
}
