// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief UI → Demod command queue API and command IDs.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/** Command identifiers used by the async UI command queue. */
enum UiCmdId {
    UI_CMD_TOGGLE_MUTE = 1,
    UI_CMD_TOGGLE_COMPACT = 2,
    UI_CMD_HISTORY_CYCLE = 3,

    UI_CMD_SLOT1_TOGGLE = 10,
    UI_CMD_SLOT2_TOGGLE = 11,
    UI_CMD_SLOT_PREF_CYCLE = 12,

    UI_CMD_GAIN_DELTA = 20,  // payload: int32_t delta (+1/-1)
    UI_CMD_AGAIN_DELTA = 21, // payload: int32_t delta (+1/-1) for analog gain

    UI_CMD_TRUNK_TOGGLE = 30,
    UI_CMD_SCANNER_TOGGLE = 31,

    UI_CMD_PAYLOAD_TOGGLE = 40,

    // UI/state toggles and actions
    UI_CMD_P25_GA_TOGGLE = 50,  // Toggle P25 Group Affiliation section
    UI_CMD_TG_HOLD_TOGGLE = 51, // payload: uint8_t slot (0 or 1)
    UI_CMD_LPF_TOGGLE = 52,
    UI_CMD_HPF_TOGGLE = 53,
    UI_CMD_PBF_TOGGLE = 54,
    UI_CMD_HPF_D_TOGGLE = 55,
    UI_CMD_AGGR_SYNC_TOGGLE = 56,
    UI_CMD_CALL_ALERT_TOGGLE = 57,

    // Views and visualization controls
    UI_CMD_CONST_TOGGLE = 70,
    UI_CMD_CONST_NORM_TOGGLE = 71,
    UI_CMD_CONST_GATE_DELTA = 72, // payload: float delta
    UI_CMD_EYE_TOGGLE = 73,
    UI_CMD_EYE_UNICODE_TOGGLE = 74,
    UI_CMD_EYE_COLOR_TOGGLE = 75,
    UI_CMD_FSK_HIST_TOGGLE = 76,
    UI_CMD_SPECTRUM_TOGGLE = 77,
    UI_CMD_SPEC_SIZE_DELTA = 78, // payload: int32_t (+/-)
    UI_CMD_INPUT_VOL_CYCLE = 79,

    // Event history keys
    UI_CMD_EH_NEXT = 90,
    UI_CMD_EH_PREV = 91,
    UI_CMD_EH_TOGGLE_SLOT = 92,

    // Device related
    UI_CMD_PPM_DELTA = 100, // payload: int32_t (+/-1)
    UI_CMD_INVERT_TOGGLE = 101,
    UI_CMD_MOD_TOGGLE = 102,
    UI_CMD_DMR_RESET = 103,
    UI_CMD_GAIN_SET = 104,          // payload: int32_t (0..50)
    UI_CMD_AGAIN_SET = 105,         // payload: int32_t (0..50)
    UI_CMD_INPUT_WARN_DB_SET = 106, // payload: double
    UI_CMD_INPUT_MONITOR_TOGGLE = 107,
    UI_CMD_COSINE_FILTER_TOGGLE = 108,

    UI_CMD_TCP_CONNECT_AUDIO = 200, // use opts->tcp_hostname/port; sets audio_in_type=AUDIO_IN_TCP
    UI_CMD_RIGCTL_CONNECT = 201,    // uses opts->tcp_hostname/rigctlportno
    UI_CMD_RETURN_CC = 202,
    UI_CMD_CHANNEL_CYCLE = 203,
    UI_CMD_SYMCAP_SAVE = 204, // auto-name capture file and start
    UI_CMD_SYMCAP_STOP = 205,
    UI_CMD_REPLAY_LAST = 206,
    UI_CMD_WAV_START = 207,
    UI_CMD_WAV_STOP = 208,
    UI_CMD_STOP_PLAYBACK = 209,

    // Trunk policy toggles
    UI_CMD_TRUNK_WLIST_TOGGLE = 210,
    UI_CMD_TRUNK_PRIV_TOGGLE = 211,
    UI_CMD_TRUNK_DATA_TOGGLE = 212,
    UI_CMD_TRUNK_ENC_TOGGLE = 213,

    // Additional commands to preserve legacy hotkeys in async mode
    UI_CMD_QUIT = 300,
    UI_CMD_FORCE_PRIV_TOGGLE = 301,
    UI_CMD_FORCE_RC4_TOGGLE = 302,
    UI_CMD_TRUNK_GROUP_TOGGLE = 303,
    UI_CMD_SIM_NOCAR = 304,
    UI_CMD_MOD_P2_TOGGLE = 305,
    UI_CMD_LOCKOUT_SLOT = 306, // payload: uint8_t slot (0=slot1, 1=slot2)
    UI_CMD_M17_TX_TOGGLE = 307,

    // ProVoice debug toggles
    UI_CMD_PROVOICE_ESK_TOGGLE = 308,
    UI_CMD_PROVOICE_MODE_TOGGLE = 309,

    // UI utility
    UI_CMD_UI_MSG_CLEAR = 400, // clear transient toast message in canonical state
    // Logging and maintenance helpers
    UI_CMD_EH_RESET = 401,          // clear ring-buffered event history
    UI_CMD_EVENT_LOG_DISABLE = 402, // disable event log file output
    UI_CMD_EVENT_LOG_SET = 403,     // payload: char path[]

    UI_CMD_CRC_RELAX_TOGGLE = 420,
    UI_CMD_LCW_RETUNE_TOGGLE = 421,
    UI_CMD_P25_CC_CAND_TOGGLE = 423,
    UI_CMD_REVERSE_MUTE_TOGGLE = 424,
    UI_CMD_DMR_LE_TOGGLE = 425,
    UI_CMD_ALL_MUTES_TOGGLE = 426,
    UI_CMD_INV_X2_TOGGLE = 430,
    UI_CMD_INV_DMR_TOGGLE = 431,
    UI_CMD_INV_DPMR_TOGGLE = 432,
    UI_CMD_INV_M17_TOGGLE = 433,

    // File outputs / inputs
    UI_CMD_WAV_STATIC_OPEN = 440,      // payload: char path[]
    UI_CMD_WAV_RAW_OPEN = 441,         // payload: char path[]
    UI_CMD_DSP_OUT_SET = 442,          // payload: char filename[]
    UI_CMD_SYMCAP_OPEN = 443,          // payload: char path[]
    UI_CMD_SYMBOL_IN_OPEN = 444,       // payload: char path[]
    UI_CMD_INPUT_WAV_SET = 445,        // payload: char path[]; sets type=AUDIO_IN_WAV
    UI_CMD_INPUT_SYM_STREAM_SET = 446, // payload: char path[]; sets type=AUDIO_IN_SYMBOL_FLT
    UI_CMD_INPUT_SET_PULSE = 447,      // sets audio_in_dev="pulse", type=AUDIO_IN_PULSE

    // Networking / device configs
    UI_CMD_UDP_OUT_CFG = 460,           // payload: struct { char host[256]; int32_t port; }
    UI_CMD_TCP_CONNECT_AUDIO_CFG = 461, // payload: struct { char host[256]; int32_t port; }
    UI_CMD_RIGCTL_CONNECT_CFG = 462,    // payload: struct { char host[256]; int32_t port; }
    UI_CMD_UDP_INPUT_CFG = 463,         // payload: struct { char bind[256]; int32_t port; }

    // RTL-SDR controls
    UI_CMD_RTL_ENABLE_INPUT = 480,
    UI_CMD_RTL_RESTART = 481,
    UI_CMD_RTL_SET_DEV = 482,         // payload: int32_t index
    UI_CMD_RTL_SET_FREQ = 483,        // payload: int32_t hz
    UI_CMD_RTL_SET_GAIN = 484,        // payload: int32_t gain
    UI_CMD_RTL_SET_PPM = 485,         // payload: int32_t ppm
    UI_CMD_RTL_SET_BW = 486,          // payload: int32_t khz
    UI_CMD_RTL_SET_SQL_DB = 487,      // payload: double dB
    UI_CMD_RTL_SET_VOL_MULT = 488,    // payload: int32_t mult
    UI_CMD_RTL_SET_BIAS_TEE = 489,    // payload: int32_t on(0/1)
    UI_CMD_RTLTCP_SET_AUTOTUNE = 490, // payload: int32_t on(0/1)
    UI_CMD_RTL_SET_AUTO_PPM = 491,    // payload: int32_t on(0/1)

    // Rigctl / tuning params
    UI_CMD_RIGCTL_SET_MOD_BW = 500, // payload: int32_t hz
    UI_CMD_TG_HOLD_SET = 501,       // payload: uint32_t tg
    UI_CMD_HANGTIME_SET = 502,      // payload: double seconds
    UI_CMD_SLOT_PREF_SET = 503,     // payload: int32_t pref01
    UI_CMD_SLOTS_ONOFF_SET = 504,   // payload: int32_t mask

    // Pulse audio device selection
    UI_CMD_PULSE_OUT_SET = 520, // payload: char name[]
    UI_CMD_PULSE_IN_SET = 521,  // payload: char name[]

    // Input volume
    UI_CMD_INPUT_VOL_SET = 530, // payload: int32_t mult (1..16)

    // LRRP file output
    UI_CMD_LRRP_SET_HOME = 540,
    UI_CMD_LRRP_SET_DSDP = 541,
    UI_CMD_LRRP_SET_CUSTOM = 542, // payload: char path[]
    UI_CMD_LRRP_DISABLE = 543,

    // Import helpers
    UI_CMD_IMPORT_CHANNEL_MAP = 560, // payload: char path[]
    UI_CMD_IMPORT_GROUP_LIST = 561,  // payload: char path[]
    UI_CMD_IMPORT_KEYS_DEC = 562,    // payload: char path[]
    UI_CMD_IMPORT_KEYS_HEX = 563,    // payload: char path[]

    // P25 helpers
    UI_CMD_P25_P2_PARAMS_SET = 580, // payload: struct { uint64_t wacn, sysid, cc; }

    // UI display toggles
    UI_CMD_UI_SHOW_DSP_PANEL_TOGGLE = 620,
    UI_CMD_UI_SHOW_P25_METRICS_TOGGLE = 621,
    UI_CMD_UI_SHOW_P25_AFFIL_TOGGLE = 622,
    UI_CMD_UI_SHOW_P25_NEIGHBORS_TOGGLE = 623,
    UI_CMD_UI_SHOW_P25_IDEN_TOGGLE = 624,
    UI_CMD_UI_SHOW_P25_CCC_TOGGLE = 625,
    UI_CMD_UI_SHOW_CHANNELS_TOGGLE = 626,
    UI_CMD_UI_SHOW_P25_CALLSIGN_TOGGLE = 627,

    // Key management
    UI_CMD_KEY_BASIC_SET = 640,     // payload: uint32_t
    UI_CMD_KEY_SCRAMBLER_SET = 641, // payload: uint32_t
    UI_CMD_KEY_RC4DES_SET = 642,    // payload: uint64_t
    UI_CMD_KEY_HYTERA_SET = 643,    // payload: struct { uint64_t H,K1,K2,K3,K4; }
    UI_CMD_KEY_AES_SET = 644,       // payload: struct { uint64_t K1,K2,K3,K4; }

    // Keystream creation (string payloads processed on demod thread)
    UI_CMD_KEY_TYT_AP_SET = 645,      // payload: char s[] (two 64-bit hex concatenated)
    UI_CMD_KEY_RETEVIS_RC2_SET = 646, // payload: char s[] (two 64-bit hex concatenated)
    UI_CMD_KEY_TYT_EP_SET = 647,      // payload: char s[] (two 64-bit hex concatenated)
    UI_CMD_KEY_KEN_SCR_SET = 648,     // payload: char s[] (decimal lfsr)
    UI_CMD_KEY_ANYTONE_BP_SET = 649,  // payload: char s[] (16-bit hex)
    UI_CMD_KEY_XOR_SET = 650,         // payload: char s[] ("len:hexbytes")

    // Encoders / protocol helpers
    UI_CMD_M17_USER_DATA_SET = 651, // payload: char s[] (<=49 chars)

    // DSP runtime (rtl_stream_*)
    UI_CMD_DSP_OP = 700,      // payload: UiDspPayload (see ui_dsp_cmd.h)
    UI_CMD_CONFIG_APPLY = 710 // payload: dsdneoUserConfig (see runtime/config.h)
};

/**
 * @brief Command payload envelope for the UI command queue.
 */
enum { UI_CMD_DATA_MAX = 8192 };

struct UiCmd {
    int id;
    size_t n; // payload length
    uint8_t data[UI_CMD_DATA_MAX];
};
