// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief UI service helpers invoked by menu/async command handlers.
 *
 * These helpers mutate runtime options/state in response to UI actions,
 * handling validation and any required side effects (file opens, socket
 * connects, RTL restarts, etc.). Unless noted, functions return 0 on success
 * and a negative value on invalid inputs or failures.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Toggle all encrypted-audio mute flags (P25 + DMR left/right).
 */
int svc_toggle_all_mutes(dsd_opts* opts);
/**
 * @brief Toggle call alert beeps.
 */
int svc_toggle_call_alert(dsd_opts* opts);

/**
 * @brief Enable per-call WAV capture, creating the output directory if needed.
 */
int svc_enable_per_call_wav(dsd_opts* opts, dsd_state* state);

/**
 * @brief Set the symbol capture output filename and open it for writing.
 */
int svc_open_symbol_out(dsd_opts* opts, dsd_state* state, const char* filename);
/**
 * @brief Open a captured symbol file for playback and switch input type.
 */
int svc_open_symbol_in(dsd_opts* opts, dsd_state* state, const char* filename);
/**
 * @brief Re-open the last symbol capture used as input (opts->audio_in_dev).
 */
int svc_replay_last_symbol(dsd_opts* opts, dsd_state* state);
/**
 * @brief Stop symbol playback and restore the prior input type.
 */
void svc_stop_symbol_playback(dsd_opts* opts);
/**
 * @brief Close symbol output capture and point input at the saved file path.
 */
void svc_stop_symbol_saving(dsd_opts* opts, dsd_state* state);

/**
 * @brief Connect to a PCM16LE audio stream over TCP and configure libsndfile.
 */
int svc_tcp_connect_audio(dsd_opts* opts, const char* host, int port);
/**
 * @brief Connect to a rigctl server and enable rigctl control if successful.
 */
int svc_rigctl_connect(dsd_opts* opts, const char* host, int port);

// LRRP output file helpers
/**
 * @brief Enable LRRP logging to `$HOME/lrrp.txt`.
 */
int svc_lrrp_set_home(dsd_opts* opts); // ~/lrrp.txt
/**
 * @brief Enable LRRP logging to `./DSDPlus.LRRP`.
 */
int svc_lrrp_set_dsdp(dsd_opts* opts); // ./DSDPlus.LRRP
/**
 * @brief Enable LRRP logging to a user-specified file.
 */
int svc_lrrp_set_custom(dsd_opts* opts, const char* filename);
/**
 * @brief Disable LRRP logging and clear the configured output path.
 */
void svc_lrrp_disable(dsd_opts* opts);

// Misc toggles/actions
/**
 * @brief Toggle inversion flags for DMR/dPMR/X2/YSF/M17 simultaneously.
 */
void svc_toggle_inversion(dsd_opts* opts);
/**
 * @brief Reset both event-history rings to their initial empty state.
 */
void svc_reset_event_history(dsd_state* state);
/**
 * @brief Toggle raw payload logging/display.
 */
void svc_toggle_payload(dsd_opts* opts);
/**
 * @brief Override Phase 2 system identifiers (WACN, SYSID, CC), clamped to valid ranges.
 */
void svc_set_p2_params(dsd_state* state, unsigned long long wacn, unsigned long long sysid, unsigned long long cc);

// Logging & file outputs
/** @brief Set the event log output path (enables logging). */
int svc_set_event_log(dsd_opts* opts, const char* path);
/** @brief Disable event logging and clear the path. */
void svc_disable_event_log(dsd_opts* opts);
/** @brief Configure static WAV output (single file, no stereo split) and open it. */
int svc_open_static_wav(dsd_opts* opts, dsd_state* state, const char* path);
/** @brief Configure RAW WAV output and open it. */
int svc_open_raw_wav(dsd_opts* opts, dsd_state* state, const char* path);
/** @brief Enable DSP debug output to the given filename under ./DSP/. */
int svc_set_dsp_output_file(dsd_opts* opts, const char* filename);

// Pulse/UDP output helpers
/** @brief Switch audio output to PulseAudio and select a device index/name. */
int svc_set_pulse_output(dsd_opts* opts, const char* index);
/** @brief Switch audio input to PulseAudio and select a device index/name. */
int svc_set_pulse_input(dsd_opts* opts, const char* index);
/** @brief Configure UDP audio output endpoint and enable it. */
int svc_udp_output_config(dsd_opts* opts, dsd_state* state, const char* host, int port);

// Trunking & control helpers
/** @brief Toggle P25 trunk-tracking mode (disables scanner when enabled). */
void svc_toggle_trunking(dsd_opts* opts);
/** @brief Toggle scanner mode (disables trunking when enabled). */
void svc_toggle_scanner(dsd_opts* opts);
/** @brief Import a channel map CSV into runtime state. */
int svc_import_channel_map(dsd_opts* opts, dsd_state* state, const char* path);
/** @brief Import a group list CSV into runtime state. */
int svc_import_group_list(dsd_opts* opts, dsd_state* state, const char* path);
/** @brief Import keys from a decimal CSV. */
int svc_import_keys_dec(dsd_opts* opts, dsd_state* state, const char* path);
/** @brief Import keys from a hexadecimal CSV. */
int svc_import_keys_hex(dsd_opts* opts, dsd_state* state, const char* path);
/** @brief Toggle tuning to group calls in trunking mode. */
void svc_toggle_tune_group(dsd_opts* opts);
/** @brief Toggle tuning to private calls in trunking mode. */
void svc_toggle_tune_private(dsd_opts* opts);
/** @brief Toggle tuning to data calls in trunking mode. */
void svc_toggle_tune_data(dsd_opts* opts);
/** @brief Set the current talkgroup hold value. */
void svc_set_tg_hold(dsd_state* state, unsigned tg);
/** @brief Set trunking hang time (seconds, clamped to >=0). */
void svc_set_hangtime(dsd_opts* opts, double seconds);
/** @brief Set rigctl setmod bandwidth (clamped to 0..25 kHz). */
void svc_set_rigctl_setmod_bw(dsd_opts* opts, int hz);
/** @brief Toggle reverse mute (mute when unmuted, unmute when muted). */
void svc_toggle_reverse_mute(dsd_opts* opts);
/** @brief Toggle relaxed CRC handling/aggressive frame sync. */
void svc_toggle_crc_relax(dsd_opts* opts);
/** @brief Toggle P25 LCW retune helper. */
void svc_toggle_lcw_retune(dsd_opts* opts);
/** @brief Toggle little-endian DMR symbol ordering. */
void svc_toggle_dmr_le(dsd_opts* opts);
/** @brief Set slot preference (0 or 1). */
void svc_set_slot_pref(dsd_opts* opts, int pref01);
/** @brief Enable/disable slots using bitmask (bit0=slot1, bit1=slot2). */
void svc_set_slots_onoff(dsd_opts* opts, int mask);

// Per-protocol inversion toggles
/** @brief Toggle X2-TDMA symbol inversion. */
void svc_toggle_inv_x2(dsd_opts* opts);
/** @brief Toggle DMR symbol inversion. */
void svc_toggle_inv_dmr(dsd_opts* opts);
/** @brief Toggle dPMR symbol inversion. */
void svc_toggle_inv_dpmr(dsd_opts* opts);
/** @brief Toggle M17 symbol inversion. */
void svc_toggle_inv_m17(dsd_opts* opts);

#ifdef USE_RTLSDR
// RTL-SDR configuration and lifecycle helpers
/** @brief Switch active input to RTL-SDR and restart the stream. */
int svc_rtl_enable_input(dsd_opts* opts, dsd_state* state);
/** @brief Restart the RTL stream if active, tearing down any existing context. */
int svc_rtl_restart(dsd_opts* opts, dsd_state* state);
/** @brief Set RTL device index and mark stream for restart (applied immediately if active). */
int svc_rtl_set_dev_index(dsd_opts* opts, dsd_state* state, int index);
/** @brief Tune RTL center frequency (Hz), applying live when stream active. */
int svc_rtl_set_freq(dsd_opts* opts, dsd_state* state, uint32_t hz);
/** @brief Set RTL manual gain (0–49), clamping and restarting if needed. */
int svc_rtl_set_gain(dsd_opts* opts, dsd_state* state, int value);
/** @brief Set RTL PPM correction (clamped to ±200). */
int svc_rtl_set_ppm(dsd_opts* opts, int ppm);
/** @brief Set RTL DSP baseband bandwidth (kHz: 4,6,8,12,16,24,48), clamping and restarting if needed. */
int svc_rtl_set_bandwidth(dsd_opts* opts, dsd_state* state, int khz);
/** @brief Set RTL squelch threshold in dB, converting to power units. */
int svc_rtl_set_sql_db(dsd_opts* opts, double dB);
/** @brief Set RTL volume multiplier (clamped to 0–3). */
int svc_rtl_set_volume_mult(dsd_opts* opts, int mult);
/** @brief Toggle RTL bias tee (applied live when stream active). */
int svc_rtl_set_bias_tee(dsd_opts* opts, dsd_state* state, int on);
/** @brief Toggle RTL-TCP adaptive networking and propagate to env/stream. */
int svc_rtltcp_set_autotune(dsd_opts* opts, dsd_state* state, int on);
/** @brief Toggle spectrum-based auto PPM and propagate to env/stream. */
int svc_rtl_set_auto_ppm(dsd_opts* opts, dsd_state* state, int on);
#endif

#ifdef __cplusplus
}
#endif
