// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Async callback handlers for menu prompts.
 *
 * This header is internal to src/ui/terminal/ and should NOT be installed.
 */
#pragma once

// ---- Simple path callbacks ----
void cb_event_log_set(void* v, const char* path);
void cb_static_wav(void* v, const char* path);
void cb_raw_wav(void* v, const char* path);
void cb_dsp_out(void* v, const char* name);
void cb_import_chan(void* v, const char* p);
void cb_import_group(void* v, const char* p);
void cb_keys_dec(void* v, const char* p);
void cb_keys_hex(void* v, const char* p);

// ---- Config callbacks ----
void cb_config_load(void* v, const char* path);
void cb_config_save_as(void* v, const char* path);

// ---- Typed value callbacks ----
void cb_setmod_bw(void* v, int ok, int bw);
void cb_tg_hold(void* v, int ok, int tg);
void cb_hangtime(void* v, int ok, double s);
void cb_slot_pref(void* v, int ok, int p);
void cb_slots_on(void* v, int ok, int m);

// ---- Keystream callbacks ----
void cb_tyt_ap(void* v, const char* s);
void cb_retevis_rc2(void* v, const char* s);
void cb_tyt_ep(void* v, const char* s);
void cb_ken_scr(void* v, const char* s);
void cb_anytone_bp(void* v, const char* s);
void cb_xor_ks(void* v, const char* s);

// ---- Key entry callbacks ----
void cb_key_basic(void* v, int ok, int val);
void cb_key_scrambler(void* v, int ok, int val);
void cb_key_rc4des(void* v, const char* text);

// ---- Multi-step callbacks ----
void cb_hytera_step(void* u, const char* text);
void cb_aes_step(void* u, const char* text);
void cb_p2_step(void* u, const char* text);

// ---- IO callbacks ----
void cb_io_save_symbol_capture(void* v, const char* path);
void cb_io_read_symbol_bin(void* v, const char* path);
void cb_udp_out_host(void* u, const char* host);
void cb_udp_out_port(void* u, int ok, int port);
void cb_tcp_host(void* u, const char* host);
void cb_tcp_port(void* u, int ok, int port);
void cb_udp_in_addr(void* u, const char* addr);
void cb_udp_in_port(void* u, int ok, int port);
void cb_rig_host(void* u, const char* host);
void cb_rig_port(void* u, int ok, int port);
void cb_switch_to_wav(void* v, const char* path);
void cb_switch_to_symbol(void* v, const char* path);

// ---- Gain callbacks ----
void cb_gain_dig(void* u, int ok, double g);
void cb_gain_ana(void* u, int ok, double g);
void cb_input_vol(void* u, int ok, int m);

// ---- RTL callbacks ----
void cb_rtl_dev(void* u, int ok, int i);
void cb_rtl_freq(void* u, int ok, int f);
void cb_rtl_gain(void* u, int ok, int g);
void cb_rtl_ppm(void* u, int ok, int p);
void cb_rtl_bw(void* u, int ok, int bw);
void cb_rtl_sql(void* u, int ok, double dB);
void cb_rtl_vol(void* u, int ok, int m);

// ---- DSP/Env callbacks ----
void cb_input_warn(void* v, int ok, double thr);
void cb_set_p25_num(void* u, int ok, double val);
void cb_audio_lpf(void* v, int ok, int hz);
void cb_auto_ppm_snr(void* v, int ok, double d);
void cb_auto_ppm_pwr(void* v, int ok, double d);
void cb_auto_ppm_zeroppm(void* v, int ok, double p);
void cb_auto_ppm_zerohz(void* v, int ok, int h);
void cb_tcp_prebuf(void* v, int ok, int ms);
void cb_tcp_rcvbuf(void* v, int ok, int sz);
void cb_tcp_rcvtimeo(void* v, int ok, int ms);

// ---- LRRP callback ----
void cb_lr_custom(void* v, const char* path);

// ---- Env editor callbacks ----
void cb_env_edit_name(void* u, const char* name);
void cb_env_edit_value(void* u, const char* val);

// ---- M17 callback ----
void cb_m17_user_data(void* u, const char* text);

// ---- Chooser completion handlers (from menu_prompts.c) ----
void chooser_done_pulse_out(void* u, int sel);
void chooser_done_pulse_in(void* u, int sel);
