// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
* dsd_ncurses_handler.c
* DSD-FME ncurses terminal user input handler
*
* LWVMOBILE
* 2025-05 DSD-FME Florida Man Edition
*-----------------------------------------------------------------------------*/

#include <dsd-neo/core/opts.h>
#include <dsd-neo/platform/curses_compat.h>
#include <dsd-neo/ui/keymap.h>
#include <dsd-neo/ui/menu_core.h>
#include <dsd-neo/ui/ncurses.h>
#include <dsd-neo/ui/ui_async.h>
#include <dsd-neo/ui/ui_cmd.h>
#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>
#endif

uint8_t
ncurses_input_handler(dsd_opts* opts, dsd_state* state, int c) {

    if (!opts || !state) {
        return 1;
    }

    // If the nonblocking menu overlay is open, route keys to it first.
    if (ui_menu_is_open()) {
        if (c != -1) {
            ui_menu_handle_key(c, opts, state);
        }
        return 1; // consume all keys while menu overlay is active
    }

    switch (c) {
        case DSD_KEY_ESC: {
            // Drain any pending escape sequence bytes without spinning.
            int ch2;
            while ((ch2 = getch()) != ERR) {
                (void)ch2;
            }
            return 1;
        }
        case DSD_KEY_MUTE_LOWER:
        case DSD_KEY_MUTE_UPPER: ui_post_cmd(UI_CMD_TOGGLE_MUTE, NULL, 0); return 1;
        case DSD_KEY_COMPACT: ui_post_cmd(UI_CMD_TOGGLE_COMPACT, NULL, 0); return 1;
        case DSD_KEY_HISTORY: ui_post_cmd(UI_CMD_HISTORY_CYCLE, NULL, 0); return 1;
        case DSD_KEY_SLOT1_TOGGLE: ui_post_cmd(UI_CMD_SLOT1_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_SLOT2_TOGGLE: ui_post_cmd(UI_CMD_SLOT2_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_SLOT_PREF: ui_post_cmd(UI_CMD_SLOT_PREF_CYCLE, NULL, 0); return 1;
        case DSD_KEY_GAIN_PLUS: {
            int32_t d = +1;
            ui_post_cmd(UI_CMD_GAIN_DELTA, &d, sizeof d);
            return 1;
        }
        case DSD_KEY_GAIN_MINUS: {
            int32_t d = -1;
            ui_post_cmd(UI_CMD_GAIN_DELTA, &d, sizeof d);
            return 1;
        }
        case DSD_KEY_TRUNK_TOGGLE: ui_post_cmd(UI_CMD_TRUNK_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_SCANNER_TOGGLE: ui_post_cmd(UI_CMD_SCANNER_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_PAYLOAD_TOGGLE: ui_post_cmd(UI_CMD_PAYLOAD_TOGGLE, NULL, 0); return 1;

        case DSD_KEY_TOGGLE_P25GA: ui_post_cmd(UI_CMD_P25_GA_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_TG_HOLD1: {
            uint8_t s = 0;
            ui_post_cmd(UI_CMD_TG_HOLD_TOGGLE, &s, sizeof s);
            return 1;
        }
        case DSD_KEY_TG_HOLD2: {
            uint8_t s = 1;
            ui_post_cmd(UI_CMD_TG_HOLD_TOGGLE, &s, sizeof s);
            return 1;
        }

        case DSD_KEY_AGAIN_PLUS: {
            int32_t d = +1;
            ui_post_cmd(UI_CMD_AGAIN_DELTA, &d, sizeof d);
            return 1;
        }
        case DSD_KEY_AGAIN_MINUS: {
            int32_t d = -1;
            ui_post_cmd(UI_CMD_AGAIN_DELTA, &d, sizeof d);
            return 1;
        }

        case DSD_KEY_CONST_VIEW_LOWER:
        case DSD_KEY_CONST_VIEW_UPPER: ui_post_cmd(UI_CMD_CONST_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_CONST_NORM: ui_post_cmd(UI_CMD_CONST_NORM_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_CONST_GATE_DEC: {
            float d = -0.02f;
            ui_post_cmd(UI_CMD_CONST_GATE_DELTA, &d, sizeof d);
            return 1;
        }
        case DSD_KEY_CONST_GATE_INC: {
            float d = +0.02f;
            ui_post_cmd(UI_CMD_CONST_GATE_DELTA, &d, sizeof d);
            return 1;
        }

        case DSD_KEY_EYE_VIEW: ui_post_cmd(UI_CMD_EYE_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_EYE_UNICODE: ui_post_cmd(UI_CMD_EYE_UNICODE_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_EYE_COLOR: ui_post_cmd(UI_CMD_EYE_COLOR_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_FSK_HIST: ui_post_cmd(UI_CMD_FSK_HIST_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_SPECTRUM: ui_post_cmd(UI_CMD_SPECTRUM_TOGGLE, NULL, 0); return 1;
#ifdef USE_RTLSDR
        case DSD_KEY_SPEC_DEC: {
            int32_t d = -(rtl_stream_spectrum_get_size() / 2);
            ui_post_cmd(UI_CMD_SPEC_SIZE_DELTA, &d, sizeof d);
            return 1;
        }
        case DSD_KEY_SPEC_INC: {
            int32_t d = +(rtl_stream_spectrum_get_size());
            ui_post_cmd(UI_CMD_SPEC_SIZE_DELTA, &d, sizeof d);
            return 1;
        }
#endif

        case DSD_KEY_EH_NEXT: ui_post_cmd(UI_CMD_EH_NEXT, NULL, 0); return 1;
        case DSD_KEY_EH_PREV: ui_post_cmd(UI_CMD_EH_PREV, NULL, 0); return 1;
        case DSD_KEY_EH_TOGGLE: {
            if (opts->m17encoder == 1) {
                ui_post_cmd(UI_CMD_M17_TX_TOGGLE, NULL, 0);
            } else {
                ui_post_cmd(UI_CMD_EH_TOGGLE_SLOT, NULL, 0);
            }
            return 1;
        }

        case DSD_KEY_RTL_VOL_CYCLE: ui_post_cmd(UI_CMD_INPUT_VOL_CYCLE, NULL, 0); return 1;
        case DSD_KEY_LPF_TOGGLE: ui_post_cmd(UI_CMD_LPF_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_HPF_TOGGLE: ui_post_cmd(UI_CMD_HPF_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_PBF_TOGGLE: ui_post_cmd(UI_CMD_PBF_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_HPF_DIG_TOGGLE: ui_post_cmd(UI_CMD_HPF_D_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_AGGR_SYNC: ui_post_cmd(UI_CMD_AGGR_SYNC_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_CALL_ALERT: ui_post_cmd(UI_CMD_CALL_ALERT_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_INVERT: ui_post_cmd(UI_CMD_INVERT_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_MOD_TOGGLE: ui_post_cmd(UI_CMD_MOD_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_MOD_P2: ui_post_cmd(UI_CMD_MOD_P2_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_DMR_RESET: ui_post_cmd(UI_CMD_DMR_RESET, NULL, 0); return 1;
        case DSD_KEY_PPM_UP: {
            int32_t d = +1;
            ui_post_cmd(UI_CMD_PPM_DELTA, &d, sizeof d);
            return 1;
        }
        case DSD_KEY_PPM_DOWN: {
            int32_t d = -1;
            ui_post_cmd(UI_CMD_PPM_DELTA, &d, sizeof d);
            return 1;
        }
        case DSD_KEY_TRUNK_WLIST: ui_post_cmd(UI_CMD_TRUNK_WLIST_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_TRUNK_PRIV: ui_post_cmd(UI_CMD_TRUNK_PRIV_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_TRUNK_DATA: ui_post_cmd(UI_CMD_TRUNK_DATA_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_TRUNK_ENC: ui_post_cmd(UI_CMD_TRUNK_ENC_TOGGLE, NULL, 0); return 1;
        case 'g': ui_post_cmd(UI_CMD_TRUNK_GROUP_TOGGLE, NULL, 0); return 1;
        case 'A': ui_post_cmd(UI_CMD_PROVOICE_ESK_TOGGLE, NULL, 0); return 1;
        case 'S': ui_post_cmd(UI_CMD_PROVOICE_MODE_TOGGLE, NULL, 0); return 1;

        // Heavy actions: TCP/rigctl connect, capture/playback, retune, scan
        case DSD_KEY_TCP_AUDIO: ui_post_cmd(UI_CMD_TCP_CONNECT_AUDIO, NULL, 0); return 1;
        case DSD_KEY_RIGCTL_CONN: ui_post_cmd(UI_CMD_RIGCTL_CONNECT, NULL, 0); return 1;
        case DSD_KEY_RETURN_CC: ui_post_cmd(UI_CMD_RETURN_CC, NULL, 0); return 1;
        case DSD_KEY_CHANNEL_CYCLE: ui_post_cmd(UI_CMD_CHANNEL_CYCLE, NULL, 0); return 1;
        case DSD_KEY_SYMCAP_SAVE: ui_post_cmd(UI_CMD_SYMCAP_SAVE, NULL, 0); return 1;
        case DSD_KEY_SYMCAP_STOP: ui_post_cmd(UI_CMD_SYMCAP_STOP, NULL, 0); return 1;
        case DSD_KEY_REPLAY_LAST: ui_post_cmd(UI_CMD_REPLAY_LAST, NULL, 0); return 1;
        case DSD_KEY_WAV_START: ui_post_cmd(UI_CMD_WAV_START, NULL, 0); return 1;
        case DSD_KEY_WAV_STOP: ui_post_cmd(UI_CMD_WAV_STOP, NULL, 0); return 1;
        case DSD_KEY_STOP_PLAYBACK: ui_post_cmd(UI_CMD_STOP_PLAYBACK, NULL, 0); return 1;

        case DSD_KEY_QUIT: ui_post_cmd(UI_CMD_QUIT, NULL, 0); return 1;
        case DSD_KEY_FORCE_PRIV: ui_post_cmd(UI_CMD_FORCE_PRIV_TOGGLE, NULL, 0); return 1;
        case DSD_KEY_FORCE_RC4: ui_post_cmd(UI_CMD_FORCE_RC4_TOGGLE, NULL, 0); return 1;
        case '!': {
            uint8_t s = 0;
            ui_post_cmd(UI_CMD_LOCKOUT_SLOT, &s, sizeof s);
            return 1;
        }
        case '@': {
            uint8_t s = 1;
            ui_post_cmd(UI_CMD_LOCKOUT_SLOT, &s, sizeof s);
            return 1;
        }
        case DSD_KEY_SIM_NOCAR: ui_post_cmd(UI_CMD_SIM_NOCAR, NULL, 0); return 1;
        case DSD_KEY_ENTER:
        case '\r':
        case KEY_ENTER:
            if (opts->m17encoder == 0) {
                // Open nonblocking menu overlay from the UI thread
                ui_menu_open_async(opts, state);
            }
            return 1;
        default:
            // Consume unknown keys to avoid legacy mutations.
            return 1;
    }
}
