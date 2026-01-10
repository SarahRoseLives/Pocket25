// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Action handlers for menu items.
 *
 * This header is internal to src/ui/terminal/ and should NOT be installed.
 */
#pragma once

// ---- Main menu actions ----
void act_toggle_invert(void* v);
void act_toggle_payload(void* v);
void act_reset_eh(void* v);
void act_exit(void* v);

// ---- Event/WAV/DSP actions ----
void act_event_log_set(void* v);
void act_event_log_disable(void* v);
void act_static_wav(void* v);
void act_raw_wav(void* v);
void act_dsp_out(void* v);

// ---- Config actions ----
void act_config_load(void* v);
void act_config_save_current(void* v);
void act_config_save_default(void* v);
void act_config_save_as(void* v);

// ---- Trunking/scanner actions ----
void act_crc_relax(void* v);
void act_trunk_toggle(void* v);
void act_scan_toggle(void* v);
void act_lcw_toggle(void* v);
void act_p25_enc_lockout(void* v);
void act_setmod_bw(void* v);
void act_import_chan(void* v);
void act_import_group(void* v);
void act_allow_toggle(void* v);
void act_tune_group(void* v);
void act_tune_priv(void* v);
void act_tune_data(void* v);
void act_tg_hold(void* v);
void act_hangtime(void* v);

// ---- DMR/TDMA actions ----
void act_rev_mute(void* v);
void act_dmr_le(void* v);
void act_slot_pref(void* v);
void act_slots_on(void* v);

// ---- Key import actions ----
void act_keys_dec(void* v);
void act_keys_hex(void* v);
void act_tyt_ap(void* v);
void act_retevis_rc2(void* v);
void act_tyt_ep(void* v);
void act_ken_scr(void* v);
void act_anytone_bp(void* v);
void act_xor_ks(void* v);

// ---- P25 Phase 2 actions ----
void act_p2_params(void* v);

// ---- Env/Advanced actions ----
void act_toggle_ftz_daz(void* v);
void act_set_input_warn(void* v);
void act_deemph_cycle(void* v);
void act_set_audio_lpf(void* v);
void act_window_freeze_toggle(void* v);
void act_auto_ppm_freeze(void* v);
void act_tcp_waitall(void* v);
void act_rt_sched(void* v);
void act_mt(void* v);
void act_env_editor(void* v);

// ---- Prompt wrappers for Advanced menu ----
void act_auto_ppm_snr_prompt(void* v);
void act_auto_ppm_pwr_prompt(void* v);
void act_auto_ppm_zeroppm_prompt(void* v);
void act_auto_ppm_zerohz_prompt(void* v);
void act_tcp_prebuf_prompt(void* v);
void act_tcp_rcvbuf_prompt(void* v);
void act_tcp_rcvtimeo_prompt(void* v);

// ---- P25 follower numeric settings ----
void act_set_p25_vc_grace(void* v);
void act_set_p25_min_follow(void* v);
void act_set_p25_grant_voice(void* v);
void act_set_p25_retune_backoff(void* v);
void act_set_p25_cc_grace(void* v);
void act_set_p25_force_extra(void* v);
void act_set_p25_force_margin(void* v);
void act_set_p25_p1_err_pct(void* v);
void act_set_p25_p1_err_sec(void* v);

// ---- IO actions ----
void io_toggle_mute_enc(void* vctx);
void io_toggle_call_alert(void* vctx);
void io_toggle_cc_candidates(void* vctx);
void io_enable_per_call_wav(void* vctx);
void io_save_symbol_capture(void* vctx);
void io_read_symbol_bin(void* vctx);
void io_replay_last_symbol_bin(void* vctx);
void io_stop_symbol_playback(void* vctx);
void io_stop_symbol_saving(void* vctx);
void io_set_pulse_out(void* vctx);
void io_set_pulse_in(void* vctx);
void io_set_udp_out(void* vctx);
void io_tcp_direct_link(void* vctx);
void io_set_gain_dig(void* vctx);
void io_set_gain_ana(void* vctx);
void io_toggle_monitor(void* vctx);
void io_toggle_cosine(void* vctx);
void io_set_input_volume(void* vctx);
void io_input_vol_up(void* vctx);
void io_input_vol_dn(void* vctx);
void io_rigctl_config(void* vctx);

// ---- Inversion actions ----
void inv_x2(void* v);
void inv_dmr(void* v);
void inv_dpmr(void* v);
void inv_m17(void* v);

// ---- Switch input/output actions ----
void switch_to_pulse(void* vctx);
void switch_to_wav(void* vctx);
void switch_to_symbol(void* vctx);
void switch_to_tcp(void* vctx);
void switch_to_udp(void* vctx);
void switch_out_pulse(void* vctx);
void switch_out_udp(void* vctx);
void switch_out_toggle_mute(void* vctx);

// ---- Key entry actions ----
void key_basic(void* v);
void key_hytera(void* v);
void key_scrambler(void* v);
void key_force_bp(void* v);
void key_rc4des(void* v);
void key_aes(void* v);

// ---- LRRP actions ----
void lr_home(void* v);
void lr_dsdp(void* v);
void lr_custom(void* v);
void lr_off(void* v);

// ---- M17 actions ----
void act_m17_user_data(void* v);

// ---- UI display toggle actions ----
void act_toggle_ui_p25_metrics(void* v);
void act_toggle_ui_p25_affil(void* v);
void act_toggle_ui_p25_ga(void* v);
void act_toggle_ui_p25_neighbors(void* v);
void act_toggle_ui_p25_iden(void* v);
void act_toggle_ui_p25_ccc(void* v);
void act_toggle_ui_channels(void* v);
void act_toggle_ui_p25_callsign(void* v);

// ---- RTL-SDR actions (USE_RTLSDR only) ----
#ifdef USE_RTLSDR
void rtl_enable(void* v);
void rtl_restart(void* v);
void rtl_set_dev(void* v);
void rtl_set_freq(void* v);
void rtl_set_gain(void* v);
void rtl_set_ppm(void* v);
void rtl_set_bw(void* v);
void rtl_set_sql(void* v);
void rtl_set_vol(void* v);
void rtl_toggle_bias(void* v);
void rtl_toggle_rtltcp_autotune(void* v);
void rtl_toggle_auto_ppm(void* v);
void rtl_toggle_tuner_autogain(void* v);
void switch_to_rtl(void* vctx);
#endif

// ---- DSP actions (USE_RTLSDR only) ----
#ifdef USE_RTLSDR
void act_toggle_cq(void* v);
void act_toggle_fll(void* v);
void act_toggle_ted(void* v);
void act_toggle_iqbal(void* v);
void act_toggle_fm_agc(void* v);
void act_toggle_fm_limiter(void* v);
void act_fm_agc_target_up(void* v);
void act_fm_agc_target_dn(void* v);
void act_fm_agc_min_up(void* v);
void act_fm_agc_min_dn(void* v);
void act_fm_agc_alpha_up_up(void* v);
void act_fm_agc_alpha_up_dn(void* v);
void act_fm_agc_alpha_down_up(void* v);
void act_fm_agc_alpha_down_dn(void* v);
void act_toggle_iq_dc(void* v);
void act_iq_dc_k_up(void* v);
void act_iq_dc_k_dn(void* v);
void act_ted_gain_up(void* v);
void act_ted_gain_dn(void* v);
void act_ted_force_toggle(void* v);
void act_c4fm_clk_cycle(void* v);
void act_c4fm_clk_sync_toggle(void* v);
void act_toggle_dsp_panel(void* v);
#endif
