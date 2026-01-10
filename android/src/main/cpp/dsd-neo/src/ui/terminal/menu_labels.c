// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Dynamic label generators and visibility predicates for menu items.
 */

#include "menu_labels.h"

#include "menu_env.h"
#include "menu_internal.h"
#include "menu_items.h"

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/io/tcp_input.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/runtime/config.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>
#endif

// ---- Visibility/predicate functions ----

bool
io_always_on(void* ctx) {
    (void)ctx;
    return true;
}

bool
io_rtl_active(void* ctx) {
    UiCtx* c = (UiCtx*)ctx;
    if (!c || !c->opts) {
        return false;
    }
    return (c->opts->audio_in_type == AUDIO_IN_RTL);
}

#ifdef USE_RTLSDR
bool
dsp_cq_on(void* v) {
    UNUSED(v);
    int cq = 0, f = 0, t = 0;
    rtl_stream_dsp_get(&cq, &f, &t);
    return cq != 0;
}

int
ui_current_mod(const void* v) {
    const UiCtx* c = (const UiCtx*)v;
    int mod = -1;

    // Honor CLI-locked demod selection when present
    if (c && c->opts && c->opts->mod_cli_lock) {
        if (c->opts->mod_qpsk) {
            mod = 1;
        } else if (c->opts->mod_gfsk) {
            mod = 2;
        } else {
            mod = 0;
        }
    }

    // Prefer live state when available (any valid rf_mod)
    if (mod < 0 && c && c->state) {
        int rf = c->state->rf_mod;
        if (rf >= 0 && rf <= 2) {
            mod = rf;
        }
    }

    // Snap to the active DSP path: CQPSK toggle always means QPSK path
    int cq = 0;
    rtl_stream_dsp_get(&cq, NULL, NULL);
    if (cq) {
        mod = 1;
    }

    // Fallback: default to FM/C4FM family (or GFSK when hinted)
    if (mod < 0) {
        mod = 0;
    }
    return mod;
}

bool
is_mod_qpsk(void* v) {
    return ui_current_mod(v) == 1;
}

bool
is_mod_c4fm(void* v) {
    return ui_current_mod(v) == 0;
}

bool
is_mod_gfsk(void* v) {
    return ui_current_mod(v) == 2;
}

bool
is_mod_fm(void* v) {
    int m = ui_current_mod(v);
    return m == 0 || m == 2;
}

bool
is_not_qpsk(void* v) {
    return !is_mod_qpsk(v);
}

bool
is_fll_allowed(void* v) {
    return is_mod_qpsk(v) || is_mod_fm(v);
}

bool
is_ted_allowed(void* v) {
    return is_mod_qpsk(v) || is_mod_fm(v);
}

// DSP submenu arrays declared in menu_items.h

bool
dsp_agc_any(void* v) {
    return ui_submenu_has_visible(DSP_AGC_ITEMS, DSP_AGC_ITEMS_LEN, v) ? true : false;
}

bool
dsp_ted_any(void* v) {
    return ui_submenu_has_visible(DSP_TED_ITEMS, DSP_TED_ITEMS_LEN, v) ? true : false;
}
#endif

// ---- State labels ----

const char*
lbl_invert_all(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Signal Inversion [%s]", c->opts->inverted_dmr ? "Active" : "Inactive");
    return b;
}

const char*
lbl_toggle_payload(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Payload Logging [%s]", c->opts->payload ? "Active" : "Inactive");
    return b;
}

const char*
lbl_trunk(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Trunking [%s]", c->opts->p25_trunk ? "Active" : "Inactive");
    return b;
}

const char*
lbl_scan(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Scanning Mode [%s]", c->opts->scanner_mode ? "Active" : "Inactive");
    return b;
}

const char*
lbl_lcw(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle P25 LCW Retune [%s]", c->opts->p25_lcw_retune ? "Active" : "Inactive");
    return b;
}

const char*
lbl_p25_enc_lockout(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    int on = (c && c->opts) ? ((c->opts->trunk_tune_enc_calls == 0) ? 1 : 0) : 0;
    snprintf(b, n, "P25 Encrypted Call Lockout [%s]", on ? "On" : "Off");
    return b;
}

const char*
lbl_crc_relax(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    int relaxed = (c->opts->aggressive_framesync == 0);
    snprintf(b, n, "Toggle Relaxed CRC checks [%s]", relaxed ? "Active" : "Inactive");
    return b;
}

const char*
lbl_allow(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Allow/White List [%s]", c->opts->trunk_use_allow_list ? "Active" : "Inactive");
    return b;
}

const char*
lbl_tune_group(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Tune Group Calls [%s]", c->opts->trunk_tune_group_calls ? "Active" : "Inactive");
    return b;
}

const char*
lbl_tune_priv(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Tune Private Calls [%s]", c->opts->trunk_tune_private_calls ? "Active" : "Inactive");
    return b;
}

const char*
lbl_tune_data(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Tune Data Calls [%s]", c->opts->trunk_tune_data_calls ? "Active" : "Inactive");
    return b;
}

const char*
lbl_rev_mute(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Reverse Mute [%s]", c->opts->reverse_mute ? "Active" : "Inactive");
    return b;
}

const char*
lbl_dmr_le(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle DMR Late Entry [%s]", c->opts->dmr_le ? "Active" : "Inactive");
    return b;
}

const char*
lbl_slotpref(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    const char* now = (c->opts->slot_preference == 0) ? "1" : (c->opts->slot_preference == 1) ? "2" : "Auto";
    snprintf(b, n, "Set TDMA Slot Preference... [now %s]", now);
    return b;
}

const char*
lbl_slots_on(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    const char* now = (c->opts->slot1_on && c->opts->slot2_on)
                          ? "both"
                          : (c->opts->slot1_on ? "1" : (c->opts->slot2_on ? "2" : "off"));
    snprintf(b, n, "Set TDMA Synth Slots... [now %s]", now);
    return b;
}

const char*
lbl_muting(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    int dmr = (c->opts->dmr_mute_encL == 1 && c->opts->dmr_mute_encR == 1);
    int p25 = (c->opts->unmute_encrypted_p25 == 0);
    int active = (dmr && p25);
    snprintf(b, n, "Toggle Encrypted Audio Muting [%s]", active ? "Active" : "Inactive");
    return b;
}

const char*
lbl_call_alert(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Call Alert Beep [%s]", c->opts->call_alert ? "Active" : "Inactive");
    return b;
}

const char*
lbl_pref_cc(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Prefer P25 CC Candidates [%s]", c->opts->p25_prefer_candidates ? "Active" : "Inactive");
    return b;
}

// ---- IO labels ----

const char*
lbl_current_output(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    const char* name;
    switch (c->opts->audio_out_type) {
        case 0: name = "Pulse Digital"; break;
        case 8: name = "UDP"; break;
        default: name = "?"; break;
    }
    if (c->opts->audio_out_type == 0) {
        if (c->opts->pa_output_idx[0]) {
            size_t prefix = strlen("Current Output: Pulse []") - 2; /* exclude %s */
            int m = (n > prefix) ? (int)(n - prefix) : 0;
            snprintf(b, n, "Current Output: Pulse [%.*s]", m, c->opts->pa_output_idx);
        } else {
            snprintf(b, n, "Current Output: Pulse [default]");
        }
    } else if (c->opts->audio_out_type == 8) {
        int m = (n > 32) ? (int)(n - 32) : 0; /* leave room for prefix and port */
        snprintf(b, n, "Current Output: UDP %.*s:%d", m, c->opts->udp_hostname, c->opts->udp_portno);
    } else {
        snprintf(b, n, "Current Output: %s", name);
    }
    return b;
}

const char*
lbl_current_input(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    const char* name;
    switch (c->opts->audio_in_type) {
        case AUDIO_IN_PULSE: name = "Pulse"; break;
        case AUDIO_IN_STDIN: name = "STDIN"; break;
        case AUDIO_IN_WAV: name = "WAV/File"; break;
        case AUDIO_IN_RTL: name = "RTL-SDR"; break;
        case AUDIO_IN_SYMBOL_BIN: name = "Symbol .bin"; break;
        case AUDIO_IN_UDP: name = "UDP"; break;
        case AUDIO_IN_TCP: name = "TCP"; break;
        case AUDIO_IN_SYMBOL_FLT: name = "Symbol Float"; break;
        default: name = "?"; break;
    }
    if (c->opts->audio_in_type == AUDIO_IN_TCP) {
        int m = (n > 32) ? (int)(n - 32) : 0;
        snprintf(b, n, "Current Input: TCP %.*s:%d", m, c->opts->tcp_hostname, c->opts->tcp_portno);
    } else if (c->opts->audio_in_type == AUDIO_IN_UDP) {
        const char* addr = c->opts->udp_in_bindaddr[0] ? c->opts->udp_in_bindaddr : "127.0.0.1";
        int m = (n > 32) ? (int)(n - 32) : 0;
        snprintf(b, n, "Current Input: UDP %.*s:%d", m, addr, c->opts->udp_in_portno);
    } else if (c->opts->audio_in_type == AUDIO_IN_WAV || c->opts->audio_in_type == AUDIO_IN_SYMBOL_BIN
               || c->opts->audio_in_type == AUDIO_IN_SYMBOL_FLT) {
        int m = (n > 18) ? (int)(n - 18) : 0;
        snprintf(b, n, "Current Input: %.*s", m, c->opts->audio_in_dev);
    } else if (c->opts->audio_in_type == AUDIO_IN_RTL) {
        snprintf(b, n, "Current Input: RTL-SDR dev %d", c->opts->rtl_dev_index);
    } else {
        snprintf(b, n, "Current Input: %s", name);
    }
    return b;
}

const char*
lbl_out_mute(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    snprintf(b, n, "Mute Output [%s]", (c->opts->audio_out == 0) ? "On" : "Off");
    return b;
}

const char*
lbl_monitor(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Source Audio Monitor [%s]", c->opts->monitor_input_audio ? "Active" : "Inactive");
    return b;
}

const char*
lbl_cosine(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Toggle Cosine Filter [%s]", c->opts->use_cosine_filter ? "Active" : "Inactive");
    return b;
}

const char*
lbl_input_volume(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    int m = c->opts->input_volume_multiplier;
    if (m < 1) {
        m = 1;
    }
    snprintf(b, n, "Input Volume: %dX", m);
    return b;
}

const char*
lbl_tcp(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    int active = (c->opts->audio_in_type == AUDIO_IN_TCP && tcp_input_is_valid(c->opts->tcp_in_ctx));
    if (c->opts->tcp_hostname[0] != '\0' && c->opts->tcp_portno > 0) {
        int m = (n > 32) ? (int)(n - 32) : 0;
        if (active) {
            snprintf(b, n, "TCP Direct Audio: %.*s:%d [Active]", m, c->opts->tcp_hostname, c->opts->tcp_portno);
        } else {
            snprintf(b, n, "TCP Direct Audio: %.*s:%d [Inactive]", m, c->opts->tcp_hostname, c->opts->tcp_portno);
        }
    } else {
        snprintf(b, n, active ? "TCP Direct Audio [Active]" : "Start TCP Direct Audio [Inactive]");
    }
    return b;
}

const char*
lbl_rigctl(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    int connected = (c->opts->use_rigctl && c->opts->rigctl_sockfd != 0);
    if (c->opts->rigctlhostname[0] != '\0' && c->opts->rigctlportno > 0) {
        int m = (n > 24) ? (int)(n - 24) : 0;
        if (connected) {
            snprintf(b, n, "Rigctl: %.*s:%d [Active]", m, c->opts->rigctlhostname, c->opts->rigctlportno);
        } else {
            snprintf(b, n, "Rigctl: %.*s:%d [Inactive]", m, c->opts->rigctlhostname, c->opts->rigctlportno);
        }
    } else {
        snprintf(b, n, connected ? "Rigctl [Active]" : "Configure Rigctl [Inactive]");
    }
    return b;
}

const char*
lbl_sym_save(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    if (c->opts->symbol_out_f) {
        size_t prefix = strlen("Save Symbols to File [Active: ]") - 2; /* exclude %s */
        int m = (n > prefix) ? (int)(n - prefix) : 0;
        snprintf(b, n, "Save Symbols to File [Active: %.*s]", m, c->opts->symbol_out_file);
    } else {
        snprintf(b, n, "Save Symbols to File [Inactive]");
    }
    return b;
}

const char*
lbl_per_call_wav(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    if (c->opts->dmr_stereo_wav == 1 && c->opts->wav_out_f != NULL) {
        snprintf(b, n, "Save Per-Call WAV [Active]");
    } else {
        snprintf(b, n, "Save Per-Call WAV [Inactive]");
    }
    return b;
}

const char*
lbl_stop_symbol_playback(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    if (c->opts->symbolfile != NULL && c->opts->audio_in_type == AUDIO_IN_SYMBOL_BIN) {
        if (c->opts->audio_in_dev[0] != '\0') {
            snprintf(b, n, "Stop Symbol Playback [Active: %s]", c->opts->audio_in_dev);
        } else {
            snprintf(b, n, "Stop Symbol Playback [Active]");
        }
    } else {
        snprintf(b, n, "Stop Symbol Playback [Inactive]");
    }
    return b;
}

const char*
lbl_stop_symbol_capture(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    if (c->opts->symbol_out_f) {
        if (c->opts->symbol_out_file[0] != '\0') {
            snprintf(b, n, "Stop Symbol Capture [Active: %s]", c->opts->symbol_out_file);
        } else {
            snprintf(b, n, "Stop Symbol Capture [Active]");
        }
    } else {
        snprintf(b, n, "Stop Symbol Capture [Inactive]");
    }
    return b;
}

const char*
lbl_replay_last(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    if (c->opts->audio_in_dev[0] != '\0') {
        struct stat sb;
        if (stat(c->opts->audio_in_dev, &sb) == 0 && S_ISREG(sb.st_mode)) {
            snprintf(b, n, "Replay Last Symbol Capture [%s]", c->opts->audio_in_dev);
            return b;
        }
    }
    snprintf(b, n, "Replay Last Symbol Capture [Inactive]");
    return b;
}

// ---- Inversion labels ----

const char*
lbl_inv_x2(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Invert X2-TDMA [%s]", c->opts->inverted_x2tdma ? "Active" : "Inactive");
    return b;
}

const char*
lbl_inv_dmr(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Invert DMR [%s]", c->opts->inverted_dmr ? "Active" : "Inactive");
    return b;
}

const char*
lbl_inv_dpmr(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Invert dPMR [%s]", c->opts->inverted_dpmr ? "Active" : "Inactive");
    return b;
}

const char*
lbl_inv_m17(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Invert M17 [%s]", c->opts->inverted_m17 ? "Active" : "Inactive");
    return b;
}

// ---- Env/Advanced labels ----

const char*
lbl_ftz_daz(void* v, char* b, size_t n) {
    (void)v;
#if defined(__SSE__) || defined(__SSE2__)
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->ftz_daz_enable) ? 1 : 0;
    snprintf(b, n, "SSE FTZ/DAZ: %s", on ? "On" : "Off");
    return b;
#else
    snprintf(b, n, "SSE FTZ/DAZ: Unavailable");
    return b;
#endif
}

const char*
lbl_input_warn(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    double thr = c ? c->opts->input_warn_db : env_get_double("DSD_NEO_INPUT_WARN_DB", -40.0);
    snprintf(b, n, "Low Input Warning: %.1f dBFS", thr);
    return b;
}

const char*
lbl_deemph(void* v, char* b, size_t n) {
    (void)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    const char* s = "Unset";
    if (cfg) {
        switch (cfg->deemph_mode) {
            case DSD_NEO_DEEMPH_OFF: s = "Off"; break;
            case DSD_NEO_DEEMPH_50: s = "50"; break;
            case DSD_NEO_DEEMPH_75: s = "75"; break;
            case DSD_NEO_DEEMPH_NFM: s = "NFM"; break;
            default: s = "Unset"; break;
        }
    }
    snprintf(b, n, "Deemphasis: %s", s);
    return b;
}

const char*
lbl_audio_lpf(void* v, char* b, size_t n) {
    (void)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (cfg && cfg->audio_lpf_is_set && !cfg->audio_lpf_disable && cfg->audio_lpf_cutoff_hz > 0) {
        snprintf(b, n, "Audio LPF: %d Hz", cfg->audio_lpf_cutoff_hz);
    } else {
        snprintf(b, n, "Audio LPF: Off");
    }
    return b;
}

const char*
lbl_window_freeze(void* v, char* b, size_t n) {
    (void)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->window_freeze_is_set) ? cfg->window_freeze : 0;
    snprintf(b, n, "Freeze Symbol Window: %s", on ? "On" : "Off");
    return b;
}

const char*
lbl_auto_ppm_snr(void* v, char* b, size_t n) {
    (void)v;
    double d = env_get_double("DSD_NEO_AUTO_PPM_SNR_DB", 6.0);
    snprintf(b, n, "Auto-PPM SNR threshold: %.1f dB", d);
    return b;
}

const char*
lbl_auto_ppm_pwr(void* v, char* b, size_t n) {
    (void)v;
    double d = env_get_double("DSD_NEO_AUTO_PPM_PWR_DB", -80.0);
    snprintf(b, n, "Auto-PPM Min power: %.1f dB", d);
    return b;
}

const char*
lbl_auto_ppm_zeroppm(void* v, char* b, size_t n) {
    (void)v;
    double p = env_get_double("DSD_NEO_AUTO_PPM_ZEROLOCK_PPM", 0.6);
    snprintf(b, n, "Auto-PPM Zero-lock PPM: %.2f", p);
    return b;
}

const char*
lbl_auto_ppm_zerohz(void* v, char* b, size_t n) {
    (void)v;
    int h = env_get_int("DSD_NEO_AUTO_PPM_ZEROLOCK_HZ", 60);
    snprintf(b, n, "Auto-PPM Zero-lock Hz: %d", h);
    return b;
}

const char*
lbl_auto_ppm_freeze(void* v, char* b, size_t n) {
    (void)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->auto_ppm_freeze_enable) ? 1 : 0;
    snprintf(b, n, "Auto-PPM Freeze: %s", on ? "On" : "Off");
    return b;
}

const char*
lbl_tcp_prebuf(void* v, char* b, size_t n) {
    (void)v;
    int ms = env_get_int("DSD_NEO_TCP_PREBUF_MS", 30);
    snprintf(b, n, "RTL-TCP Prebuffer: %d ms", ms);
    return b;
}

const char*
lbl_tcp_rcvbuf(void* v, char* b, size_t n) {
    (void)v;
    int sz = env_get_int("DSD_NEO_TCP_RCVBUF", 0);
    if (sz > 0) {
        snprintf(b, n, "RTL-TCP SO_RCVBUF: %d bytes", sz);
    } else {
        snprintf(b, n, "RTL-TCP SO_RCVBUF: system default");
    }
    return b;
}

const char*
lbl_tcp_rcvtimeo(void* v, char* b, size_t n) {
    (void)v;
    int ms = env_get_int("DSD_NEO_TCP_RCVTIMEO", 0);
    if (ms > 0) {
        snprintf(b, n, "RTL-TCP SO_RCVTIMEO: %d ms", ms);
    } else {
        snprintf(b, n, "RTL-TCP SO_RCVTIMEO: off");
    }
    return b;
}

const char*
lbl_tcp_waitall(void* v, char* b, size_t n) {
    (void)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->tcp_waitall_enable) ? 1 : 0;
    snprintf(b, n, "RTL-TCP MSG_WAITALL: %s", on ? "On" : "Off");
    return b;
}

const char*
lbl_rt_sched(void* v, char* b, size_t n) {
    (void)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->rt_sched_enable) ? 1 : 0;
    snprintf(b, n, "Realtime Scheduling: %s", on ? "On" : "Off");
    return b;
}

const char*
lbl_mt(void* v, char* b, size_t n) {
    (void)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->mt_is_set) ? cfg->mt_enable : 0;
    snprintf(b, n, "Intra-block MT: %s", on ? "On" : "Off");
    return b;
}

// ---- P25 follower labels ----

static const char*
lbl_p25_num(void* v, char* b, size_t n, const char* env_name, const char* fmt, double defv) {
    (void)v;
    double val = env_get_double(env_name, defv);
    snprintf(b, n, fmt, val);
    return b;
}

const char*
lbl_p25_vc_grace(void* v, char* b, size_t n) {
    return lbl_p25_num(v, b, n, "DSD_NEO_P25_VC_GRACE", "P25: VC grace (s): %.3f", 0.0);
}

const char*
lbl_p25_min_follow(void* v, char* b, size_t n) {
    return lbl_p25_num(v, b, n, "DSD_NEO_P25_MIN_FOLLOW_DWELL", "P25: Min follow dwell (s): %.3f", 0.0);
}

const char*
lbl_p25_grant_voice(void* v, char* b, size_t n) {
    return lbl_p25_num(v, b, n, "DSD_NEO_P25_GRANT_VOICE_TO", "P25: Grant->Voice timeout (s): %.3f", 0.0);
}

const char*
lbl_p25_retune_backoff(void* v, char* b, size_t n) {
    return lbl_p25_num(v, b, n, "DSD_NEO_P25_RETUNE_BACKOFF", "P25: Retune backoff (s): %.3f", 0.0);
}

const char*
lbl_p25_cc_grace(void* v, char* b, size_t n) {
    return lbl_p25_num(v, b, n, "DSD_NEO_P25_CC_GRACE", "P25: CC hunt grace (s): %.3f", 0.0);
}

const char*
lbl_p25_force_extra(void* v, char* b, size_t n) {
    return lbl_p25_num(v, b, n, "DSD_NEO_P25_FORCE_RELEASE_EXTRA", "P25: Force release extra (s): %.3f", 0.0);
}

const char*
lbl_p25_force_margin(void* v, char* b, size_t n) {
    return lbl_p25_num(v, b, n, "DSD_NEO_P25_FORCE_RELEASE_MARGIN", "P25: Force release margin (s): %.3f", 0.0);
}

const char*
lbl_p25_p1_err_pct(void* v, char* b, size_t n) {
    return lbl_p25_num(v, b, n, "DSD_NEO_P25P1_ERR_HOLD_PCT", "P25p1: Err-hold pct: %.1f%%", 0.0);
}

const char*
lbl_p25_p1_err_sec(void* v, char* b, size_t n) {
    return lbl_p25_num(v, b, n, "DSD_NEO_P25P1_ERR_HOLD_S", "P25p1: Err-hold sec: %.3f", 0.0);
}

// ---- UI display labels ----

const char*
lbl_ui_p25_metrics(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Show P25 Metrics [%s]", (c && c->opts && c->opts->show_p25_metrics) ? "On" : "Off");
    return b;
}

const char*
lbl_ui_p25_affil(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Show P25 Affiliations [%s]", (c && c->opts && c->opts->show_p25_affiliations) ? "On" : "Off");
    return b;
}

const char*
lbl_ui_p25_ga(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Show P25 Group Affiliation [%s]",
             (c && c->opts && c->opts->show_p25_group_affiliations) ? "On" : "Off");
    return b;
}

const char*
lbl_ui_p25_neighbors(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Show P25 Neighbors [%s]", (c && c->opts && c->opts->show_p25_neighbors) ? "On" : "Off");
    return b;
}

const char*
lbl_ui_p25_iden(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Show P25 IDEN Plan [%s]", (c && c->opts && c->opts->show_p25_iden_plan) ? "On" : "Off");
    return b;
}

const char*
lbl_ui_p25_ccc(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Show P25 CC Candidates [%s]", (c && c->opts && c->opts->show_p25_cc_candidates) ? "On" : "Off");
    return b;
}

const char*
lbl_ui_channels(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Show Channels [%s]", (c && c->opts && c->opts->show_channels) ? "On" : "Off");
    return b;
}

const char*
lbl_ui_p25_callsign(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Show P25 Callsign Decode [%s]", (c && c->opts && c->opts->show_p25_callsign_decode) ? "On" : "Off");
    return b;
}

// ---- LRRP labels ----

const char*
lbl_lrrp_current(void* vctx, char* b, size_t n) {
    UiCtx* c = (UiCtx*)vctx;
    if (c->opts->lrrp_file_output && c->opts->lrrp_out_file[0] != '\0') {
        snprintf(b, n, "LRRP Output [Active: %s]", c->opts->lrrp_out_file);
    } else {
        snprintf(b, n, "LRRP Output [Inactive]");
    }
    return b;
}

// ---- Keys labels ----

const char*
lbl_m17_user_data(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    const char* s = (c && c->state && c->state->m17dat[0]) ? c->state->m17dat : "<unset>";
    int m = (int)n - 18;
    if (m < 0) {
        m = 0;
    }
    snprintf(b, n, "M17 Encoder User Data: %.*s", m, s);
    return b;
}

// ---- DSP labels (USE_RTLSDR only) ----

#ifdef USE_RTLSDR

const char*
lbl_onoff_cq(void* v, char* b, size_t n) {
    UNUSED(v);
    int cq = 0, f = 0, t = 0;
    rtl_stream_dsp_get(&cq, &f, &t);
    snprintf(b, n, "Toggle CQPSK [%s]", cq ? "Active" : "Inactive");
    return b;
}

const char*
lbl_onoff_fll(void* v, char* b, size_t n) {
    UNUSED(v);
    int cq = 0, f = 0, t = 0;
    rtl_stream_dsp_get(&cq, &f, &t);
    snprintf(b, n, "Toggle FLL [%s]", f ? "Active" : "Inactive");
    return b;
}

const char*
lbl_onoff_ted(void* v, char* b, size_t n) {
    UNUSED(v);
    int cq = 0, f = 0, t = 0;
    rtl_stream_dsp_get(&cq, &f, &t);
    snprintf(b, n, "Toggle TED [%s]", t ? "Active" : "Inactive");
    return b;
}

const char*
lbl_onoff_iqbal(void* v, char* b, size_t n) {
    UNUSED(v);
    int on = rtl_stream_get_iq_balance();
    snprintf(b, n, "Toggle IQ Balance [%s]", on ? "Active" : "Inactive");
    return b;
}

const char*
lbl_fm_agc(void* v, char* b, size_t n) {
    UNUSED(v);
    int on = rtl_stream_get_fm_agc();
    snprintf(b, n, "FM AGC [%s]", on ? "On" : "Off");
    return b;
}

const char*
lbl_fm_limiter(void* v, char* b, size_t n) {
    UNUSED(v);
    int on = rtl_stream_get_fm_limiter();
    snprintf(b, n, "FM Limiter [%s]", on ? "On" : "Off");
    return b;
}

const char*
lbl_fm_agc_target(void* v, char* b, size_t n) {
    UNUSED(v);
    float tgt = 0.0f;
    rtl_stream_get_fm_agc_params(&tgt, NULL, NULL, NULL);
    snprintf(b, n, "AGC Target: %.3f (+/-)", tgt);
    return b;
}

const char*
lbl_fm_agc_min(void* v, char* b, size_t n) {
    UNUSED(v);
    float mn = 0.0f;
    rtl_stream_get_fm_agc_params(NULL, &mn, NULL, NULL);
    snprintf(b, n, "AGC Min: %.3f (+/-)", mn);
    return b;
}

const char*
lbl_fm_agc_alpha_up(void* v, char* b, size_t n) {
    UNUSED(v);
    float au = 0.0f;
    rtl_stream_get_fm_agc_params(NULL, NULL, &au, NULL);
    int pct = (int)lrintf(au * 100.0f);
    snprintf(b, n, "AGC Alpha Up: %.3f (~%d%%)", au, pct);
    return b;
}

const char*
lbl_fm_agc_alpha_down(void* v, char* b, size_t n) {
    UNUSED(v);
    float ad = 0.0f;
    rtl_stream_get_fm_agc_params(NULL, NULL, NULL, &ad);
    int pct = (int)lrintf(ad * 100.0f);
    snprintf(b, n, "AGC Alpha Down: %.3f (~%d%%)", ad, pct);
    return b;
}

const char*
lbl_iq_dc(void* v, char* b, size_t n) {
    UNUSED(v);
    int k = 0;
    int on = rtl_stream_get_iq_dc(&k);
    snprintf(b, n, "IQ DC Block [%s]", on ? "On" : "Off");
    return b;
}

const char*
lbl_iq_dc_k(void* v, char* b, size_t n) {
    UNUSED(v);
    int k = 0;
    rtl_stream_get_iq_dc(&k);
    snprintf(b, n, "IQ DC Shift k: %d (+/-)", k);
    return b;
}

const char*
lbl_ted_gain(void* v, char* b, size_t n) {
    UNUSED(v);
    float g = rtl_stream_get_ted_gain();
    int g_milli = (int)(g * 1000.0f + 0.5f);
    snprintf(b, n, "TED Gain: %d (x0.001, +/-)", g_milli);
    return b;
}

const char*
lbl_ted_force(void* v, char* b, size_t n) {
    UNUSED(v);
    int f = rtl_stream_get_ted_force();
    snprintf(b, n, "TED Force [%s]", f ? "Active" : "Inactive");
    return b;
}

const char*
lbl_ted_bias(void* v, char* b, size_t n) {
    UNUSED(v);
    int eb = rtl_stream_ted_bias(NULL);
    snprintf(b, n, "TED Bias (EMA): %d", eb);
    return b;
}

const char*
lbl_dsp_panel(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Show DSP Panel [%s]", (c && c->opts && c->opts->show_dsp_panel) ? "On" : "Off");
    return b;
}

const char*
lbl_c4fm_clk(void* v, char* b, size_t n) {
    UNUSED(v);
    int mode = rtl_stream_get_c4fm_clk();
    const char* s = (mode == 1) ? "EL" : (mode == 2) ? "MM" : "Off";
    snprintf(b, n, "C4FM Clock: %s (cycle)", s);
    return b;
}

const char*
lbl_c4fm_clk_sync(void* v, char* b, size_t n) {
    UNUSED(v);
    int en = rtl_stream_get_c4fm_clk_sync();
    snprintf(b, n, "C4FM Clock While Synced [%s]", en ? "Active" : "Inactive");
    return b;
}

const char*
lbl_rtl_bias(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "Bias Tee: %s", (c->opts->rtl_bias_tee ? "On" : "Off"));
    return b;
}

const char*
lbl_rtl_rtltcp_autotune(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    snprintf(b, n, "RTL-TCP Adaptive Networking: %s", (c->opts->rtltcp_autotune ? "On" : "Off"));
    return b;
}

const char*
lbl_rtl_auto_ppm(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    int on = c->opts->rtl_auto_ppm ? 1 : 0;
    /* If stream active, reflect runtime state */
    if (c->state && c->state->rtl_ctx) {
        extern int rtl_stream_get_auto_ppm(void);
        on = rtl_stream_get_auto_ppm();
    }
    snprintf(b, n, "Auto-PPM (Spectrum): %s", on ? "On" : "Off");
    return b;
}

const char*
lbl_rtl_tuner_autogain(void* v, char* b, size_t n) {
    UiCtx* c = (UiCtx*)v;
    int on = 0;
    if (c->state && c->state->rtl_ctx) {
        on = rtl_stream_get_tuner_autogain();
    } else {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        on = (cfg && cfg->tuner_autogain_enable) ? 1 : 0;
    }
    snprintf(b, n, "Tuner Autogain: %s", on ? "On" : "Off");
    return b;
}

#endif /* USE_RTLSDR */
