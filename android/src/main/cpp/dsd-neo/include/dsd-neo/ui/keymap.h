// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Centralized ncurses UI hotkeys to avoid conflicts and keep hints in sync.
 *
 * Only keys actively used in the ncurses visual aids and related toggles are defined here.
 */

#ifndef DSD_NEO_UI_KEYMAP_H
#define DSD_NEO_UI_KEYMAP_H

// General control
#define DSD_KEY_ESC              '\033'
#define DSD_KEY_ENTER            10
#define DSD_KEY_QUIT             'q'

// Constellation view
#define DSD_KEY_CONST_VIEW_LOWER 'o'
#define DSD_KEY_CONST_VIEW_UPPER 'O'
#define DSD_KEY_CONST_NORM       'n' // lowercase only (avoid clash with PBF 'N')
#define DSD_KEY_CONST_GATE_DEC   '<'
#define DSD_KEY_CONST_GATE_INC   '>'

// Eye diagram
#define DSD_KEY_EYE_VIEW         'E'
#define DSD_KEY_EYE_UNICODE      'U'
// Note: 'L' is used for channel cycle; keep eye color on 'G'
#define DSD_KEY_EYE_COLOR        'G'

// FSK histogram
#define DSD_KEY_FSK_HIST         'K'

// Spectrum analyzer (FFT)
#define DSD_KEY_SPECTRUM         'f'
#define DSD_KEY_SPEC_DEC         ','
#define DSD_KEY_SPEC_INC         '.'

// Other related keys in this area
#define DSD_KEY_HISTORY          'h'
#define DSD_KEY_STOP_PLAYBACK    's' // keep here for conflict checking only

// Compact view toggle
#define DSD_KEY_COMPACT          'c'

// UI section visibility
#define DSD_KEY_TOGGLE_P25M      'J' // show/hide P25 Metrics
#define DSD_KEY_TOGGLE_CHANS     'I' // show/hide Channels
// P25 affiliations (RID list)
#define DSD_KEY_TOGGLE_P25A      'A' // show/hide P25 Affiliations
// P25 Group Affiliation (RID↔TG)
#define DSD_KEY_TOGGLE_P25GA     'T' // show/hide P25 Group Affiliation

// Audio output mute
#define DSD_KEY_MUTE_LOWER       'x'
#define DSD_KEY_MUTE_UPPER       'X'

// Trunking / scanner
#define DSD_KEY_TRUNK_TOGGLE     't'
#define DSD_KEY_SCANNER_TOGGLE   'y'
#define DSD_KEY_CALL_ALERT       'a'
#define DSD_KEY_RETURN_CC        'C'
#define DSD_KEY_CHANNEL_CYCLE    'L'
#define DSD_KEY_TRUNK_WLIST      'w'
#define DSD_KEY_TRUNK_PRIV       'u'
#define DSD_KEY_TRUNK_DATA       'd'
#define DSD_KEY_TRUNK_ENC        'e'

// TDMA/DMR slot toggles and prefs
#define DSD_KEY_SLOT1_TOGGLE     '1'
#define DSD_KEY_SLOT2_TOGGLE     '2'
#define DSD_KEY_SLOT_PREF        '3'
#define DSD_KEY_TG_HOLD1         'k'
#define DSD_KEY_TG_HOLD2         'l'
#define DSD_KEY_FORCE_PRIV       '4'
#define DSD_KEY_FORCE_RC4        '6'

// Gain adjustments
#define DSD_KEY_GAIN_PLUS        '+'
#define DSD_KEY_GAIN_MINUS       '-'
#define DSD_KEY_AGAIN_PLUS       '*'
#define DSD_KEY_AGAIN_MINUS      '/'

// Payload/logging
#define DSD_KEY_PAYLOAD_TOGGLE   'z'

// Modulation and invert
#define DSD_KEY_INVERT           'i'
#define DSD_KEY_MOD_TOGGLE       'm'
#define DSD_KEY_MOD_P2           'M'

// Symbol capture / replay
#define DSD_KEY_SYMCAP_SAVE      'R'
#define DSD_KEY_SYMCAP_STOP      'r'
#define DSD_KEY_REPLAY_LAST      ' '

// Per-call WAV
#define DSD_KEY_WAV_START        'P'
#define DSD_KEY_WAV_STOP         'p'

// Aggressive sync / resets / debug
#define DSD_KEY_AGGR_SYNC        'F'
#define DSD_KEY_DMR_RESET        'D'
#define DSD_KEY_SIM_NOCAR        'Z'
#define DSD_KEY_EH_NEXT          ']'
#define DSD_KEY_EH_PREV          '['
#define DSD_KEY_EH_TOGGLE        '\\'

// RigCTL/TCP
#define DSD_KEY_TCP_AUDIO        '8'
#define DSD_KEY_RIGCTL_CONN      '9'

// Filters
#define DSD_KEY_LPF_TOGGLE       'V'
#define DSD_KEY_HPF_TOGGLE       'B'
#define DSD_KEY_PBF_TOGGLE       'N'
#define DSD_KEY_HPF_DIG_TOGGLE   'H'

// RTL-SDR helpers
#define DSD_KEY_RTL_VOL_CYCLE    'v'
#define DSD_KEY_PPM_UP           '}'
#define DSD_KEY_PPM_DOWN         '{'

#endif // DSD_NEO_UI_KEYMAP_H
