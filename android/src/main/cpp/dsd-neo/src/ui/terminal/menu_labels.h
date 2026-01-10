// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Dynamic label generators and visibility predicates for menu items.
 *
 * This header is internal to src/ui/terminal/ and should NOT be installed.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

// ---- Visibility/predicate functions ----
bool io_always_on(void* ctx);
bool io_rtl_active(void* ctx);

#ifdef USE_RTLSDR
bool dsp_cq_on(void* v);
bool is_mod_qpsk(void* v);
bool is_mod_c4fm(void* v);
bool is_mod_gfsk(void* v);
bool is_mod_fm(void* v);
bool is_not_qpsk(void* v);
bool is_fll_allowed(void* v);
bool is_ted_allowed(void* v);
bool dsp_agc_any(void* v);
bool dsp_ted_any(void* v);
int ui_current_mod(const void* v);
#endif

// ---- Label functions ----
// State labels
const char* lbl_invert_all(void* v, char* b, size_t n);
const char* lbl_toggle_payload(void* v, char* b, size_t n);
const char* lbl_trunk(void* v, char* b, size_t n);
const char* lbl_scan(void* v, char* b, size_t n);
const char* lbl_lcw(void* v, char* b, size_t n);
const char* lbl_p25_enc_lockout(void* v, char* b, size_t n);
const char* lbl_crc_relax(void* v, char* b, size_t n);
const char* lbl_allow(void* v, char* b, size_t n);
const char* lbl_tune_group(void* v, char* b, size_t n);
const char* lbl_tune_priv(void* v, char* b, size_t n);
const char* lbl_tune_data(void* v, char* b, size_t n);
const char* lbl_rev_mute(void* v, char* b, size_t n);
const char* lbl_dmr_le(void* v, char* b, size_t n);
const char* lbl_slotpref(void* v, char* b, size_t n);
const char* lbl_slots_on(void* v, char* b, size_t n);
const char* lbl_muting(void* v, char* b, size_t n);
const char* lbl_call_alert(void* v, char* b, size_t n);
const char* lbl_pref_cc(void* v, char* b, size_t n);

// IO labels
const char* lbl_current_output(void* vctx, char* b, size_t n);
const char* lbl_current_input(void* vctx, char* b, size_t n);
const char* lbl_out_mute(void* vctx, char* b, size_t n);
const char* lbl_monitor(void* v, char* b, size_t n);
const char* lbl_cosine(void* v, char* b, size_t n);
const char* lbl_input_volume(void* vctx, char* b, size_t n);
const char* lbl_tcp(void* vctx, char* b, size_t n);
const char* lbl_rigctl(void* vctx, char* b, size_t n);
const char* lbl_sym_save(void* vctx, char* b, size_t n);
const char* lbl_per_call_wav(void* vctx, char* b, size_t n);
const char* lbl_stop_symbol_playback(void* vctx, char* b, size_t n);
const char* lbl_stop_symbol_capture(void* vctx, char* b, size_t n);
const char* lbl_replay_last(void* vctx, char* b, size_t n);

// Inversion labels
const char* lbl_inv_x2(void* v, char* b, size_t n);
const char* lbl_inv_dmr(void* v, char* b, size_t n);
const char* lbl_inv_dpmr(void* v, char* b, size_t n);
const char* lbl_inv_m17(void* v, char* b, size_t n);

// Env/Advanced labels
const char* lbl_ftz_daz(void* v, char* b, size_t n);
const char* lbl_input_warn(void* v, char* b, size_t n);
const char* lbl_deemph(void* v, char* b, size_t n);
const char* lbl_audio_lpf(void* v, char* b, size_t n);
const char* lbl_window_freeze(void* v, char* b, size_t n);
const char* lbl_auto_ppm_snr(void* v, char* b, size_t n);
const char* lbl_auto_ppm_pwr(void* v, char* b, size_t n);
const char* lbl_auto_ppm_zeroppm(void* v, char* b, size_t n);
const char* lbl_auto_ppm_zerohz(void* v, char* b, size_t n);
const char* lbl_auto_ppm_freeze(void* v, char* b, size_t n);
const char* lbl_tcp_prebuf(void* v, char* b, size_t n);
const char* lbl_tcp_rcvbuf(void* v, char* b, size_t n);
const char* lbl_tcp_rcvtimeo(void* v, char* b, size_t n);
const char* lbl_tcp_waitall(void* v, char* b, size_t n);
const char* lbl_rt_sched(void* v, char* b, size_t n);
const char* lbl_mt(void* v, char* b, size_t n);

// P25 follower labels
const char* lbl_p25_vc_grace(void* v, char* b, size_t n);
const char* lbl_p25_min_follow(void* v, char* b, size_t n);
const char* lbl_p25_grant_voice(void* v, char* b, size_t n);
const char* lbl_p25_retune_backoff(void* v, char* b, size_t n);
const char* lbl_p25_cc_grace(void* v, char* b, size_t n);
const char* lbl_p25_force_extra(void* v, char* b, size_t n);
const char* lbl_p25_force_margin(void* v, char* b, size_t n);
const char* lbl_p25_p1_err_pct(void* v, char* b, size_t n);
const char* lbl_p25_p1_err_sec(void* v, char* b, size_t n);

// UI display labels
const char* lbl_ui_p25_metrics(void* v, char* b, size_t n);
const char* lbl_ui_p25_affil(void* v, char* b, size_t n);
const char* lbl_ui_p25_ga(void* v, char* b, size_t n);
const char* lbl_ui_p25_neighbors(void* v, char* b, size_t n);
const char* lbl_ui_p25_iden(void* v, char* b, size_t n);
const char* lbl_ui_p25_ccc(void* v, char* b, size_t n);
const char* lbl_ui_channels(void* v, char* b, size_t n);
const char* lbl_ui_p25_callsign(void* v, char* b, size_t n);

// LRRP labels
const char* lbl_lrrp_current(void* vctx, char* b, size_t n);

// Keys labels
const char* lbl_m17_user_data(void* v, char* b, size_t n);

// DSP labels (USE_RTLSDR only)
#ifdef USE_RTLSDR
const char* lbl_onoff_cq(void* v, char* b, size_t n);
const char* lbl_onoff_fll(void* v, char* b, size_t n);
const char* lbl_onoff_ted(void* v, char* b, size_t n);
const char* lbl_onoff_iqbal(void* v, char* b, size_t n);
const char* lbl_fm_agc(void* v, char* b, size_t n);
const char* lbl_fm_limiter(void* v, char* b, size_t n);
const char* lbl_fm_agc_target(void* v, char* b, size_t n);
const char* lbl_fm_agc_min(void* v, char* b, size_t n);
const char* lbl_fm_agc_alpha_up(void* v, char* b, size_t n);
const char* lbl_fm_agc_alpha_down(void* v, char* b, size_t n);
const char* lbl_iq_dc(void* v, char* b, size_t n);
const char* lbl_iq_dc_k(void* v, char* b, size_t n);
const char* lbl_ted_gain(void* v, char* b, size_t n);
const char* lbl_ted_force(void* v, char* b, size_t n);
const char* lbl_ted_bias(void* v, char* b, size_t n);
const char* lbl_dsp_panel(void* v, char* b, size_t n);
const char* lbl_c4fm_clk(void* v, char* b, size_t n);
const char* lbl_c4fm_clk_sync(void* v, char* b, size_t n);
const char* lbl_rtl_bias(void* v, char* b, size_t n);
const char* lbl_rtl_rtltcp_autotune(void* v, char* b, size_t n);
const char* lbl_rtl_auto_ppm(void* v, char* b, size_t n);
const char* lbl_rtl_tuner_autogain(void* v, char* b, size_t n);
#endif
