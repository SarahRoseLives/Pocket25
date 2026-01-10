// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*
 * ncurses_dsp_display.c
 * DSP status panel (RTL-SDR pipeline state)
 */

#include <dsd-neo/ui/ncurses_dsp_display.h>

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/curses_compat.h>
#include <dsd-neo/ui/ui_prims.h>

#include <stdarg.h>
#include <string.h>

#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>
#endif

/* Small helpers to align key/value fields to a consistent value column. */
static inline void
ui_print_label_pad(const char* label) {
    const int value_col = 14; /* column (from label start) where values begin */
    int lab_len = (int)strlen(label);
    if (lab_len < 0) {
        lab_len = 0;
    }
    /* Draw the left border in the primary UI color (teal/cyan) for consistency */
    ui_print_lborder();
    addch(' ');
    addstr(label);
    addch(':');
    int need = value_col - (lab_len + 1); /* +1 for ':' */
    if (need < 1) {
        need = 1; /* at least one space after ':' */
    }
    for (int i = 0; i < need; i++) {
        addch(' ');
    }
}

static void
ui_print_kv_line(const char* label, const char* fmt, ...) {
    ui_print_label_pad(label);
    va_list ap;
    va_start(ap, fmt);
    vw_printw(stdscr, fmt, ap);
    va_end(ap);
    addch('\n');
}

/* Print a compact DSP status summary (which blocks are active). */
void
print_dsp_status(dsd_opts* opts, dsd_state* state) {
#ifdef USE_RTLSDR
    (void)opts;
    /* Preserve current color pair so our colored header/HR won't force default */
#ifdef PRETTY_COLORS
    attr_t saved_attrs = 0;
    short saved_pair = 0;
    attr_get(&saved_attrs, &saved_pair, NULL);
#endif
    int cq = 0, fll = 0, ted = 0;
    rtl_stream_dsp_get(&cq, &fll, &ted);
    int iqb = rtl_stream_get_iq_balance();
    int dc_k = 0;
    int dc_on = rtl_stream_get_iq_dc(&dc_k);
    int ted_force = rtl_stream_get_ted_force();
    int clk_mode = rtl_stream_get_c4fm_clk();
    int clk_sync = rtl_stream_get_c4fm_clk_sync();
    float agc_tgt = 0.0f, agc_min = 0.0f, agc_up = 0.0f, agc_down = 0.0f;
    int agc_on = rtl_stream_get_fm_agc();
    rtl_stream_get_fm_agc_params(&agc_tgt, &agc_min, &agc_up, &agc_down);
    int lim_on = rtl_stream_get_fm_limiter();

    ui_print_header("DSP");
    attron(COLOR_PAIR(14)); /* explicit yellow for DSP items */
    /* Determine current modulation for capability-aware display: 0=C4FM, 1=CQPSK, 2=GFSK */
    int mod = (state ? state->rf_mod : (cq ? 1 : 0));
    const char* modlab = "C4FM";
    if (mod == 1) {
        modlab = "CQPSK";
    } else if (mod == 2) {
        modlab = "GFSK";
    }

    /* Front-end helpers and path selection */
    ui_print_kv_line("Front", "IQBal:%s  IQ-DC:%s k:%d", iqb ? "On" : "Off", dc_on ? "On" : "Off", dc_k);
    ui_print_kv_line("Path", "Mod:%s  CQ:%s", modlab, cq ? "On" : "Off");
    ui_print_kv_line("FLL", "[%s]", fll ? "On" : "Off");
    /* Show TED status and basic timing metrics regardless of modulation so forced TED is visible. */
    {
        int ted_sps = rtl_stream_get_ted_sps();
        float ted_gain = rtl_stream_get_ted_gain();
        int ted_bias = rtl_stream_ted_bias(NULL);
        ui_print_kv_line("TED", "[%s] sps:%d g:%.3f bias:%d%s", ted ? "On" : "Off", ted_sps, ted_gain, ted_bias,
                         ted_force ? " force" : "");
    }
    if (mod == 1 || cq) {
        ui_print_kv_line("CQPSK Path", "[%s]", cq ? "On" : "Off");
    }

    if (cq) {
#ifdef USE_RTLSDR
        extern double rtl_stream_get_cfo_hz(void);
        extern double rtl_stream_get_residual_cfo_hz(void);
        extern int rtl_stream_get_carrier_lock(void);
        extern int rtl_stream_get_costas_err_q14(void);
        extern int rtl_stream_get_nco_q15(void);
        extern int rtl_stream_get_demod_rate_hz(void);
        extern double rtl_stream_get_fll_band_edge_freq_hz(void);
        double cfo = rtl_stream_get_cfo_hz();
        int clk = rtl_stream_get_carrier_lock();
        int e14 = rtl_stream_get_costas_err_q14();
        int nco_q15 = rtl_stream_get_nco_q15();
        int Fs = rtl_stream_get_demod_rate_hz();
        double fll_be_hz = rtl_stream_get_fll_band_edge_freq_hz();
        /* FLL band-edge shows coarse frequency offset being tracked */
        ui_print_kv_line("FLL BE", "Freq=%+0.1f Hz", fll_be_hz);
        /* Residual CFO is from FM discriminator, not meaningful for CQPSK - hide it */
        ui_print_kv_line("Carrier", "NCO=%+0.1f Hz  %s", cfo, clk ? "Locked" : "Acq");
        /* Convert average Costas error from Q14 (pi == 1<<14) into degrees
         * for easier interpretation. */
        double e_deg = 0.0;
        if (e14 != 0) {
            double e_abs = (double)((e14 >= 0) ? e14 : -e14);
            e_deg = (e_abs * 180.0) / 16384.0; /* 1<<14 */
        }
        ui_print_kv_line("Costas/NCO", "Err=%d(Q14,~%0.1f°)  NCO(q15)=%d  Fs=%d Hz", e14, e_deg, nco_q15, Fs);
#else
        ui_print_kv_line("Carrier", "(RTL disabled)");
#endif
    }

    if (mod == 0 || clk_mode != 0) {
        const char* clk = (clk_mode == 1) ? "EL" : (clk_mode == 2) ? "MM" : "Off";
        ui_print_kv_line("C4FM", "CLK:%s%s", clk, (clk_mode && clk_sync) ? " (sync)" : "");
    }
    if (mod != 1 || agc_on || lim_on) {
        ui_print_kv_line("FM AGC", "[%s] tgt:%.3f min:%.3f up:%.2f dn:%.2f | LIM:%s", agc_on ? "On" : "Off", agc_tgt,
                         agc_min, agc_up, agc_down, lim_on ? "On" : "Off");
    }
    attroff(COLOR_PAIR(14));
    attron(COLOR_PAIR(4));
    ui_print_hr();
    attroff(COLOR_PAIR(4));
    /* Restore previously active color pair (e.g., banner color) */
#ifdef PRETTY_COLORS
    attr_set(saved_attrs, saved_pair, NULL);
#endif
#endif
}
