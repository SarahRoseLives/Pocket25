// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Action handlers for menu items.
 */

#include "menu_actions.h"

#include "menu_callbacks.h"
#include "menu_env.h"
#include "menu_internal.h"
#include "menu_prompts.h"

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/power.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/audio.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/exitflag.h>
#include <dsd-neo/ui/menu_core.h>
#include <dsd-neo/ui/ui_async.h>
#include <dsd-neo/ui/ui_cmd.h>
#include <dsd-neo/ui/ui_prims.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>
#include <dsd-neo/ui/ui_dsp_cmd.h>
#endif

#if defined(__SSE__) || defined(__SSE2__)
#include <xmmintrin.h>
#endif

// ---- Main menu actions ----

void
act_toggle_invert(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_INVERT_TOGGLE, NULL, 0);
}

void
act_toggle_payload(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_PAYLOAD_TOGGLE, NULL, 0);
}

void
act_reset_eh(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_EH_RESET, NULL, 0);
}

void
act_exit(void* v) {
    UNUSED(v);
    exitflag = 1;
}

// ---- Event/WAV/DSP actions ----

void
act_event_log_set(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Event log filename", c->opts->event_out_file, 1024, cb_event_log_set, c);
}

void
act_event_log_disable(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_EVENT_LOG_DISABLE, NULL, 0);
}

void
act_static_wav(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Static WAV filename", c->opts->wav_out_file, 1024, cb_static_wav, c);
}

void
act_raw_wav(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Raw WAV filename", c->opts->wav_out_file_raw, 1024, cb_raw_wav, c);
}

void
act_dsp_out(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("DSP output base filename", c->opts->dsp_out_file, 256, cb_dsp_out, c);
}

// ---- Config actions ----

void
act_config_load(void* v) {
    UiCtx* c = (UiCtx*)v;
    const char* def = NULL;
    if (c && c->state && c->state->config_autosave_path[0] != '\0') {
        def = c->state->config_autosave_path;
    } else {
        def = dsd_user_config_default_path();
    }
    ui_prompt_open_string_async("Load config from path", (def && *def) ? def : "", 512, cb_config_load, v);
}

void
act_config_save_current(void* v) {
    UiCtx* c = (UiCtx*)v;
    if (!c || !c->state) {
        return;
    }

    const char* path = NULL;
    if (c->state->config_autosave_path[0] != '\0') {
        path = c->state->config_autosave_path;
    } else {
        path = dsd_user_config_default_path();
    }

    if (!path || !*path) {
        ui_statusf("No config path; nothing saved");
        return;
    }

    dsdneoUserConfig cfg;
    dsd_snapshot_opts_to_user_config(c->opts, c->state, &cfg);
    if (dsd_user_config_save_atomic(path, &cfg) == 0) {
        ui_statusf("Config saved to %s", path);
    } else {
        ui_statusf("Failed to save config to %s", path);
    }
}

void
act_config_save_default(void* v) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    const char* path = dsd_user_config_default_path();
    if (!path || !*path) {
        ui_statusf("No default config path; nothing saved");
        return;
    }
    dsdneoUserConfig cfg;
    dsd_snapshot_opts_to_user_config(c->opts, c->state, &cfg);
    if (dsd_user_config_save_atomic(path, &cfg) == 0) {
        ui_statusf("Config saved to %s", path);
    } else {
        ui_statusf("Failed to save config to %s", path);
    }
}

void
act_config_save_as(void* v) {
    const char* def = dsd_user_config_default_path();
    ui_prompt_open_string_async("Save config to path", (def && *def) ? def : "", 512, cb_config_save_as, v);
}

// ---- Trunking/scanner actions ----

void
act_crc_relax(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_CRC_RELAX_TOGGLE, NULL, 0);
}

void
act_trunk_toggle(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_TRUNK_TOGGLE, NULL, 0);
    ui_statusf("Trunking toggle requested...");
}

void
act_scan_toggle(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_SCANNER_TOGGLE, NULL, 0);
    ui_statusf("Scanner toggle requested...");
}

void
act_lcw_toggle(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_LCW_RETUNE_TOGGLE, NULL, 0);
}

void
act_p25_enc_lockout(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_TRUNK_ENC_TOGGLE, NULL, 0);
}

void
act_setmod_bw(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("Setmod BW (Hz)", c->opts->setmod_bw, cb_setmod_bw, c);
}

void
act_import_chan(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Channel map CSV", NULL, 1024, cb_import_chan, c);
}

void
act_import_group(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Group list CSV", NULL, 1024, cb_import_group, c);
}

void
act_allow_toggle(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_TRUNK_WLIST_TOGGLE, NULL, 0);
}

void
act_tune_group(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_TRUNK_GROUP_TOGGLE, NULL, 0);
}

void
act_tune_priv(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_TRUNK_PRIV_TOGGLE, NULL, 0);
}

void
act_tune_data(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_TRUNK_DATA_TOGGLE, NULL, 0);
}

void
act_tg_hold(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("TG Hold", (int)c->state->tg_hold, cb_tg_hold, c);
}

void
act_hangtime(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_double_async("Hangtime seconds", c->opts->trunk_hangtime, cb_hangtime, c);
}

// ---- DMR/TDMA actions ----

void
act_rev_mute(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_REVERSE_MUTE_TOGGLE, NULL, 0);
}

void
act_dmr_le(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_DMR_LE_TOGGLE, NULL, 0);
}

void
act_slot_pref(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("Slot 1 or 2", c->opts->slot_preference + 1, cb_slot_pref, c);
}

void
act_slots_on(void* v) {
    UiCtx* c = (UiCtx*)v;
    int m = (c->opts->slot1_on ? 1 : 0) | (c->opts->slot2_on ? 2 : 0);
    ui_prompt_open_int_async("Slots mask (0..3)", m, cb_slots_on, c);
}

// ---- Key import actions ----

void
act_keys_dec(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Keys CSV (DEC)", NULL, 1024, cb_keys_dec, c);
}

void
act_keys_hex(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Keys CSV (HEX)", NULL, 1024, cb_keys_hex, c);
}

void
act_tyt_ap(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("TYT AP string", NULL, 256, cb_tyt_ap, c);
}

void
act_retevis_rc2(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Retevis AP string", NULL, 256, cb_retevis_rc2, c);
}

void
act_tyt_ep(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("TYT EP string", NULL, 256, cb_tyt_ep, c);
}

void
act_ken_scr(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Kenwood scrambler", NULL, 256, cb_ken_scr, c);
}

void
act_anytone_bp(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Anytone BP", NULL, 256, cb_anytone_bp, c);
}

void
act_xor_ks(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("XOR keystream", NULL, 256, cb_xor_ks, c);
}

// ---- P25 Phase 2 actions ----

void
act_p2_params(void* v) {
    UiCtx* c = (UiCtx*)v;
    P2Ctx* pc = (P2Ctx*)calloc(1, sizeof(P2Ctx));
    if (!pc) {
        return;
    }
    pc->c = c;
    pc->step = 0;
    pc->w = pc->s = pc->n = 0ULL;
    char pre[64];
    snprintf(pre, sizeof pre, "%llX", (unsigned long long)c->state->p2_wacn);
    ui_prompt_open_string_async("Enter Phase 2 WACN (HEX)", pre, sizeof pre, cb_p2_step, pc);
}

// ---- Env/Advanced actions ----

void
act_toggle_ftz_daz(void* v) {
#if defined(__SSE__) || defined(__SSE2__)
    UiCtx* c = (UiCtx*)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->ftz_daz_enable) ? 1 : 0;
    on = on ? 0 : 1; // flip
    dsd_setenv("DSD_NEO_FTZ_DAZ", on ? "1" : "0", 1);
    unsigned int mxcsr = _mm_getcsr();
    if (on) {
        mxcsr |= (1u << 15) | (1u << 6);
    } else {
        mxcsr &= ~((1u << 15) | (1u << 6));
    }
    _mm_setcsr(mxcsr);
    env_reparse_runtime_cfg(c ? c->opts : NULL);
#else
    UNUSED(v);
#endif
}

void
act_set_input_warn(void* v) {
    UiCtx* c = (UiCtx*)v;
    double thr = c ? c->opts->input_warn_db : env_get_double("DSD_NEO_INPUT_WARN_DB", -40.0);
    ui_prompt_open_double_async("Low input warning threshold (dBFS)", thr, cb_input_warn, c);
}

void
act_deemph_cycle(void* v) {
    UiCtx* c = (UiCtx*)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int mode = cfg ? cfg->deemph_mode : DSD_NEO_DEEMPH_UNSET;
    mode = (mode + 1) % 5; // cycle through UNSET->OFF->50->75->NFM->UNSET
    switch (mode) {
        case DSD_NEO_DEEMPH_UNSET: dsd_setenv("DSD_NEO_DEEMPH", "", 1); break;
        case DSD_NEO_DEEMPH_OFF: dsd_setenv("DSD_NEO_DEEMPH", "off", 1); break;
        case DSD_NEO_DEEMPH_50: dsd_setenv("DSD_NEO_DEEMPH", "50", 1); break;
        case DSD_NEO_DEEMPH_75: dsd_setenv("DSD_NEO_DEEMPH", "75", 1); break;
        case DSD_NEO_DEEMPH_NFM: dsd_setenv("DSD_NEO_DEEMPH", "nfm", 1); break;
        default: break;
    }
    env_reparse_runtime_cfg(c ? c->opts : NULL);
}

void
act_set_audio_lpf(void* v) {
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int def = (cfg && cfg->audio_lpf_is_set && !cfg->audio_lpf_disable) ? cfg->audio_lpf_cutoff_hz : 0;
    ui_prompt_open_int_async("Audio LPF cutoff Hz (0=off)", def, cb_audio_lpf, v);
}

void
act_window_freeze_toggle(void* v) {
    UiCtx* c = (UiCtx*)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->window_freeze_is_set) ? cfg->window_freeze : 0;
    dsd_setenv("DSD_NEO_WINDOW_FREEZE", on ? "0" : "1", 1);
    env_reparse_runtime_cfg(c ? c->opts : NULL);
}

void
act_auto_ppm_freeze(void* v) {
    UiCtx* c = (UiCtx*)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->auto_ppm_freeze_enable) ? 1 : 0;
    dsd_setenv("DSD_NEO_AUTO_PPM_FREEZE", on ? "0" : "1", 1);
    env_reparse_runtime_cfg(c ? c->opts : NULL);
}

void
act_tcp_waitall(void* v) {
    UiCtx* c = (UiCtx*)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->tcp_waitall_enable) ? 1 : 0;
    dsd_setenv("DSD_NEO_TCP_WAITALL", on ? "0" : "1", 1);
    env_reparse_runtime_cfg(c ? c->opts : NULL);
    if (c && c->opts && c->opts->audio_in_type == AUDIO_IN_RTL) {
        ui_post_cmd(UI_CMD_RTL_RESTART, NULL, 0);
    }
}

void
act_rt_sched(void* v) {
    UiCtx* c = (UiCtx*)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->rt_sched_enable) ? 1 : 0;
    dsd_setenv("DSD_NEO_RT_SCHED", on ? "0" : "1", 1);
    env_reparse_runtime_cfg(c ? c->opts : NULL);
}

void
act_mt(void* v) {
    UiCtx* c = (UiCtx*)v;
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int on = (cfg && cfg->mt_is_set) ? cfg->mt_enable : 0;
    dsd_setenv("DSD_NEO_MT", on ? "0" : "1", 1);
    env_reparse_runtime_cfg(c ? c->opts : NULL);
}

void
act_env_editor(void* v) {
    EnvEditCtx* ec = (EnvEditCtx*)calloc(1, sizeof(EnvEditCtx));
    if (!ec) {
        return;
    }
    ec->c = (UiCtx*)v;
    ui_prompt_open_string_async("Enter DSD_NEO_* variable name", "DSD_NEO_", 128, cb_env_edit_name, ec);
}

// ---- Prompt wrappers for Advanced menu ----

void
act_auto_ppm_snr_prompt(void* v) {
    double d = env_get_double("DSD_NEO_AUTO_PPM_SNR_DB", 6.0);
    ui_prompt_open_double_async("Auto-PPM SNR threshold (dB)", d, cb_auto_ppm_snr, v);
}

void
act_auto_ppm_pwr_prompt(void* v) {
    double d = env_get_double("DSD_NEO_AUTO_PPM_PWR_DB", -80.0);
    ui_prompt_open_double_async("Auto-PPM min power (dB)", d, cb_auto_ppm_pwr, v);
}

void
act_auto_ppm_zeroppm_prompt(void* v) {
    double p = env_get_double("DSD_NEO_AUTO_PPM_ZEROLOCK_PPM", 0.6);
    ui_prompt_open_double_async("Auto-PPM zero-lock PPM", p, cb_auto_ppm_zeroppm, v);
}

void
act_auto_ppm_zerohz_prompt(void* v) {
    int h = env_get_int("DSD_NEO_AUTO_PPM_ZEROLOCK_HZ", 60);
    ui_prompt_open_int_async("Auto-PPM zero-lock Hz", h, cb_auto_ppm_zerohz, v);
}

void
act_tcp_prebuf_prompt(void* v) {
    int ms = env_get_int("DSD_NEO_TCP_PREBUF_MS", 30);
    ui_prompt_open_int_async("RTL-TCP prebuffer (ms)", ms, cb_tcp_prebuf, v);
}

void
act_tcp_rcvbuf_prompt(void* v) {
    int sz = env_get_int("DSD_NEO_TCP_RCVBUF", 0);
    ui_prompt_open_int_async("RTL-TCP SO_RCVBUF (0=default)", sz, cb_tcp_rcvbuf, v);
}

void
act_tcp_rcvtimeo_prompt(void* v) {
    int ms = env_get_int("DSD_NEO_TCP_RCVTIMEO", 0);
    ui_prompt_open_int_async("RTL-TCP SO_RCVTIMEO (ms; 0=off)", ms, cb_tcp_rcvtimeo, v);
}

// ---- P25 follower numeric settings ----

static void
act_prompt_p25_num(void* v, const char* env_name, const char* title, double defv) {
    UiCtx* c = (UiCtx*)v;
    P25NumCtx* pc = (P25NumCtx*)calloc(1, sizeof(P25NumCtx));
    if (!pc) {
        return;
    }
    pc->c = c;
    pc->name = env_name;
    ui_prompt_open_double_async(title, defv, cb_set_p25_num, pc);
}

void
act_set_p25_vc_grace(void* v) {
    act_prompt_p25_num(v, "DSD_NEO_P25_VC_GRACE", "P25: VC grace seconds", env_get_double("DSD_NEO_P25_VC_GRACE", 0));
}

void
act_set_p25_min_follow(void* v) {
    act_prompt_p25_num(v, "DSD_NEO_P25_MIN_FOLLOW_DWELL", "P25: Min follow dwell (s)",
                       env_get_double("DSD_NEO_P25_MIN_FOLLOW_DWELL", 0));
}

void
act_set_p25_grant_voice(void* v) {
    act_prompt_p25_num(v, "DSD_NEO_P25_GRANT_VOICE_TO", "P25: Grant->Voice timeout (s)",
                       env_get_double("DSD_NEO_P25_GRANT_VOICE_TO", 0));
}

void
act_set_p25_retune_backoff(void* v) {
    act_prompt_p25_num(v, "DSD_NEO_P25_RETUNE_BACKOFF", "P25: Retune backoff (s)",
                       env_get_double("DSD_NEO_P25_RETUNE_BACKOFF", 0));
}

void
act_set_p25_cc_grace(void* v) {
    act_prompt_p25_num(v, "DSD_NEO_P25_CC_GRACE", "P25: CC hunt grace (s)", env_get_double("DSD_NEO_P25_CC_GRACE", 0));
}

void
act_set_p25_force_extra(void* v) {
    act_prompt_p25_num(v, "DSD_NEO_P25_FORCE_RELEASE_EXTRA", "P25: Safety-net extra (s)",
                       env_get_double("DSD_NEO_P25_FORCE_RELEASE_EXTRA", 0));
}

void
act_set_p25_force_margin(void* v) {
    act_prompt_p25_num(v, "DSD_NEO_P25_FORCE_RELEASE_MARGIN", "P25: Safety-net margin (s)",
                       env_get_double("DSD_NEO_P25_FORCE_RELEASE_MARGIN", 0));
}

void
act_set_p25_p1_err_pct(void* v) {
    act_prompt_p25_num(v, "DSD_NEO_P25P1_ERR_HOLD_PCT", "P25p1: Error-hold percent",
                       env_get_double("DSD_NEO_P25P1_ERR_HOLD_PCT", 0));
}

void
act_set_p25_p1_err_sec(void* v) {
    act_prompt_p25_num(v, "DSD_NEO_P25P1_ERR_HOLD_S", "P25p1: Error-hold seconds",
                       env_get_double("DSD_NEO_P25P1_ERR_HOLD_S", 0));
}

// ---- IO actions ----

void
io_toggle_mute_enc(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_ALL_MUTES_TOGGLE, NULL, 0);
}

void
io_toggle_call_alert(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_CALL_ALERT_TOGGLE, NULL, 0);
}

void
io_toggle_cc_candidates(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_P25_CC_CAND_TOGGLE, NULL, 0);
}

void
io_enable_per_call_wav(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    if (c->opts->dmr_stereo_wav == 1 && c->opts->wav_out_f != NULL) {
        ui_post_cmd(UI_CMD_WAV_STOP, NULL, 0);
        ui_statusf("Per-call WAV stop requested");
    } else {
        ui_post_cmd(UI_CMD_WAV_START, NULL, 0);
        ui_statusf("Per-call WAV start requested");
    }
}

void
io_save_symbol_capture(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    ui_prompt_open_string_async("Enter Symbol Capture Filename", NULL, 1024, cb_io_save_symbol_capture, c);
}

void
io_read_symbol_bin(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    ui_prompt_open_string_async("Enter Symbol Capture Filename", NULL, 1024, cb_io_read_symbol_bin, c);
}

void
io_replay_last_symbol_bin(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_REPLAY_LAST, NULL, 0);
    ui_statusf("Replay last requested");
}

void
io_stop_symbol_playback(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_STOP_PLAYBACK, NULL, 0);
    ui_statusf("Stop playback requested");
}

void
io_stop_symbol_saving(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_SYMCAP_STOP, NULL, 0);
    ui_statusf("Stop symbol capture requested");
}

void
io_set_pulse_out(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    dsd_audio_device outs[16];
    dsd_audio_device ins[16];
    if (dsd_audio_enumerate_devices(ins, outs, 16) < 0) {
        ui_statusf("Failed to get audio device list");
        return;
    }
    int n = 0;
    const char** labels = (const char**)calloc(16, sizeof(char*));
    const char** names = (const char**)calloc(16, sizeof(char*));
    char** bufs = (char**)calloc(16, sizeof(char*));
    if (!labels || !names || !bufs) {
        free((void*)labels);
        free((void*)names);
        free((void*)bufs);
        ui_statusf("Out of memory");
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (!outs[i].initialized) {
            break;
        }
        bufs[n] = (char*)calloc(768, sizeof(char));
        if (!bufs[n]) {
            continue;
        }
        int name_len = (int)strnlen(outs[i].name, 511);
        int desc_len = (int)strnlen(outs[i].description, 255);
        snprintf(bufs[n], 768, "[%d] %.*s - %.*s", outs[i].index, name_len, outs[i].name, desc_len,
                 outs[i].description);
        labels[n] = bufs[n];
        names[n] = dsd_strdup(outs[i].name);
        n++;
    }
    if (n == 0) {
        free((void*)labels);
        free((void*)names);
        free((void*)bufs);
        ui_statusf("No Pulse outputs found");
        return;
    }

    PulseSelCtx* pctx = (PulseSelCtx*)calloc(1, sizeof(PulseSelCtx));
    if (!pctx) {
        for (int i = 0; i < n; i++) {
            free((void*)names[i]);
            free(bufs[i]);
        }
        free((void*)labels);
        free((void*)names);
        free((void*)bufs);
        ui_statusf("Out of memory");
        return;
    }
    pctx->c = c;
    pctx->labels = labels;
    pctx->names = names;
    pctx->bufs = bufs;
    pctx->n = n;
    ui_chooser_start("Select Pulse Output", labels, n, chooser_done_pulse_out, pctx);
}

void
io_set_pulse_in(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    dsd_audio_device outs[16];
    dsd_audio_device ins[16];
    if (dsd_audio_enumerate_devices(ins, outs, 16) < 0) {
        ui_statusf("Failed to get audio device list");
        return;
    }
    int n = 0;
    const char** labels = (const char**)calloc(16, sizeof(char*));
    const char** names = (const char**)calloc(16, sizeof(char*));
    char** bufs = (char**)calloc(16, sizeof(char*));
    if (!labels || !names || !bufs) {
        free((void*)labels);
        free((void*)names);
        free((void*)bufs);
        ui_statusf("Out of memory");
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (!ins[i].initialized) {
            break;
        }
        bufs[n] = (char*)calloc(768, sizeof(char));
        if (!bufs[n]) {
            continue;
        }
        int name_len2 = (int)strnlen(ins[i].name, 511);
        int desc_len2 = (int)strnlen(ins[i].description, 255);
        snprintf(bufs[n], 768, "[%d] %.*s - %.*s", ins[i].index, name_len2, ins[i].name, desc_len2, ins[i].description);
        labels[n] = bufs[n];
        names[n] = dsd_strdup(ins[i].name);
        n++;
    }
    if (n == 0) {
        free((void*)labels);
        free((void*)names);
        free((void*)bufs);
        ui_statusf("No Pulse inputs found");
        return;
    }

    PulseSelCtx* pctx = (PulseSelCtx*)calloc(1, sizeof(PulseSelCtx));
    if (!pctx) {
        for (int i = 0; i < n; i++) {
            free((void*)names[i]);
            free(bufs[i]);
        }
        free((void*)labels);
        free((void*)names);
        free((void*)bufs);
        ui_statusf("Out of memory");
        return;
    }
    pctx->c = c;
    pctx->labels = labels;
    pctx->names = names;
    pctx->bufs = bufs;
    pctx->n = n;
    ui_chooser_start("Select Pulse Input", labels, n, chooser_done_pulse_in, pctx);
}

void
io_set_udp_out(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    UdpOutCtx* u = (UdpOutCtx*)calloc(1, sizeof(UdpOutCtx));
    if (!u) {
        return;
    }
    u->c = c;
    const char* src = c->opts->udp_hostname[0] ? c->opts->udp_hostname : "127.0.0.1";
    snprintf(u->host, sizeof u->host, "%.*s", (int)sizeof(u->host) - 1, src);
    ui_prompt_open_string_async("UDP blaster host", u->host, sizeof u->host, cb_udp_out_host, u);
}

void
io_tcp_direct_link(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    TcpLinkCtx* u = (TcpLinkCtx*)calloc(1, sizeof(TcpLinkCtx));
    if (!u) {
        return;
    }
    u->c = c;
    const char* defh = c->opts->tcp_hostname[0] ? c->opts->tcp_hostname : "localhost";
    snprintf(u->host, sizeof u->host, "%.*s", (int)sizeof(u->host) - 1, defh);
    ui_prompt_open_string_async("Enter TCP Direct Link Hostname", u->host, sizeof u->host, cb_tcp_host, u);
}

void
io_set_gain_dig(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    ui_prompt_open_double_async("Digital output gain (0=auto; 1..50)", c->opts->audio_gain, cb_gain_dig, c);
}

void
io_set_gain_ana(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    ui_prompt_open_double_async("Analog output gain (0..100)", c->opts->audio_gainA, cb_gain_ana, c);
}

void
io_toggle_monitor(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_INPUT_MONITOR_TOGGLE, NULL, 0);
}

void
io_toggle_cosine(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_COSINE_FILTER_TOGGLE, NULL, 0);
}

void
io_set_input_volume(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    int m = c->opts->input_volume_multiplier;
    if (m < 1) {
        m = 1;
    }
    if (m > 16) {
        m = 16;
    }
    ui_prompt_open_int_async("Input Volume Multiplier (1..16)", m, cb_input_vol, c);
}

void
io_input_vol_up(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    int m = c->opts->input_volume_multiplier;
    if (m < 16) {
        m++;
    }
    int32_t v = m;
    ui_post_cmd(UI_CMD_INPUT_VOL_SET, &v, sizeof v);
    ui_statusf("Input Volume requested: %dX", m);
}

void
io_input_vol_dn(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    int m = c->opts->input_volume_multiplier;
    if (m > 1) {
        m--;
    }
    int32_t v = m;
    ui_post_cmd(UI_CMD_INPUT_VOL_SET, &v, sizeof v);
    ui_statusf("Input Volume requested: %dX", m);
}

void
io_rigctl_config(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    RigCtx* u = (RigCtx*)calloc(1, sizeof(RigCtx));
    if (!u) {
        return;
    }
    u->c = c;
    const char* defh = c->opts->rigctlhostname[0] ? c->opts->rigctlhostname : "localhost";
    snprintf(u->host, sizeof u->host, "%.*s", (int)sizeof(u->host) - 1, defh);
    ui_prompt_open_string_async("Enter RIGCTL Hostname", u->host, sizeof u->host, cb_rig_host, u);
}

// ---- Inversion actions ----

void
inv_x2(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_INV_X2_TOGGLE, NULL, 0);
}

void
inv_dmr(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_INV_DMR_TOGGLE, NULL, 0);
}

void
inv_dpmr(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_INV_DPMR_TOGGLE, NULL, 0);
}

void
inv_m17(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_INV_M17_TOGGLE, NULL, 0);
}

// ---- Switch input/output actions ----

void
switch_to_pulse(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_INPUT_SET_PULSE, NULL, 0);
    ui_statusf("Pulse input requested");
}

void
switch_to_wav(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    ui_prompt_open_string_async("Enter WAV/RAW filename (or named pipe)", NULL, 1024, cb_switch_to_wav, c);
}

void
switch_to_symbol(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    ui_prompt_open_string_async("Enter symbol .bin/.raw/.sym filename", NULL, 1024, cb_switch_to_symbol, c);
}

void
switch_to_tcp(void* vctx) {
    io_tcp_direct_link(vctx);
}

void
switch_to_udp(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    UdpInCtx* u = (UdpInCtx*)calloc(1, sizeof(UdpInCtx));
    if (!u) {
        return;
    }
    u->c = c;
    const char* defa = c->opts->udp_in_bindaddr[0] ? c->opts->udp_in_bindaddr : "127.0.0.1";
    snprintf(u->addr, sizeof u->addr, "%.*s", (int)sizeof(u->addr) - 1, defa);
    ui_prompt_open_string_async("Enter UDP bind address", u->addr, sizeof u->addr, cb_udp_in_addr, u);
}

void
switch_out_pulse(void* vctx) {
    UiCtx* c = (UiCtx*)vctx;
    const char* idx = c->opts->pa_output_idx[0] ? c->opts->pa_output_idx : "";
    ui_post_cmd(UI_CMD_PULSE_OUT_SET, idx, strlen(idx) + 1);
}

void
switch_out_udp(void* vctx) {
    io_set_udp_out(vctx);
}

void
switch_out_toggle_mute(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_TOGGLE_MUTE, NULL, 0);
    ui_statusf("Output mute toggle requested");
}

// ---- Key entry actions ----

void
key_basic(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("Basic Privacy Key Number (DEC)", 0, cb_key_basic, c);
}

void
key_hytera(void* v) {
    UiCtx* c = (UiCtx*)v;
    HyCtx* hc = (HyCtx*)calloc(1, sizeof(HyCtx));
    if (!hc) {
        return;
    }
    hc->c = c;
    hc->step = 0;
    ui_prompt_open_string_async("Hytera Privacy Key 1 (HEX)", NULL, 128, cb_hytera_step, hc);
}

void
key_scrambler(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("NXDN/dPMR Scrambler Key (DEC)", 0, cb_key_scrambler, c);
}

void
key_force_bp(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_FORCE_PRIV_TOGGLE, NULL, 0);
}

void
key_rc4des(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("RC4/DES Key (HEX)", NULL, 128, cb_key_rc4des, c);
}

void
key_aes(void* v) {
    UiCtx* c = (UiCtx*)v;
    AesCtx* ac = (AesCtx*)calloc(1, sizeof(AesCtx));
    if (!ac) {
        return;
    }
    ac->c = c;
    ac->step = 0;
    ui_prompt_open_string_async("AES Segment 1 (HEX) or 0", NULL, 128, cb_aes_step, ac);
}

// ---- LRRP actions ----

void
lr_home(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_LRRP_SET_HOME, NULL, 0);
    ui_statusf("LRRP set home requested");
}

void
lr_dsdp(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_LRRP_SET_DSDP, NULL, 0);
    ui_statusf("LRRP set DSDPlus requested");
}

void
lr_custom(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_string_async("Enter LRRP output filename", NULL, 1024, cb_lr_custom, c);
}

void
lr_off(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_LRRP_DISABLE, NULL, 0);
    ui_statusf("LRRP disable requested");
}

// ---- M17 actions ----

void
act_m17_user_data(void* v) {
    UiCtx* c = (UiCtx*)v;
    const char* pre = (c && c->state && c->state->m17dat[0]) ? c->state->m17dat : "";
    M17Ctx* mc = (M17Ctx*)calloc(1, sizeof(M17Ctx));
    if (!mc) {
        return;
    }
    mc->c = c;
    ui_prompt_open_string_async("Enter M17 User Data (CAN,DST,SRC)", pre, 128, cb_m17_user_data, mc);
}

// ---- UI display toggle actions ----

void
act_toggle_ui_p25_metrics(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_UI_SHOW_P25_METRICS_TOGGLE, NULL, 0);
}

void
act_toggle_ui_p25_affil(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_UI_SHOW_P25_AFFIL_TOGGLE, NULL, 0);
}

void
act_toggle_ui_p25_ga(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_P25_GA_TOGGLE, NULL, 0);
}

void
act_toggle_ui_p25_neighbors(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_UI_SHOW_P25_NEIGHBORS_TOGGLE, NULL, 0);
}

void
act_toggle_ui_p25_iden(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_UI_SHOW_P25_IDEN_TOGGLE, NULL, 0);
}

void
act_toggle_ui_p25_ccc(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_UI_SHOW_P25_CCC_TOGGLE, NULL, 0);
}

void
act_toggle_ui_channels(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_UI_SHOW_CHANNELS_TOGGLE, NULL, 0);
}

void
act_toggle_ui_p25_callsign(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_UI_SHOW_P25_CALLSIGN_TOGGLE, NULL, 0);
}

// ---- RTL-SDR actions ----

#ifdef USE_RTLSDR

void
rtl_enable(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_RTL_ENABLE_INPUT, NULL, 0);
}

void
rtl_restart(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_RTL_RESTART, NULL, 0);
}

void
rtl_set_dev(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("Device index", c->opts->rtl_dev_index, cb_rtl_dev, c);
}

void
rtl_set_freq(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("Frequency (Hz)", (int)c->opts->rtlsdr_center_freq, cb_rtl_freq, c);
}

void
rtl_set_gain(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("Gain (0=AGC, 0..49)", c->opts->rtl_gain_value, cb_rtl_gain, c);
}

void
rtl_set_ppm(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("PPM error (-200..200)", c->opts->rtlsdr_ppm_error, cb_rtl_ppm, c);
}

void
rtl_set_bw(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("DSP Bandwidth kHz (4,6,8,12,16,24,48)", c->opts->rtl_dsp_bw_khz, cb_rtl_bw, c);
}

void
rtl_set_sql(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_double_async("Squelch (dB, negative)", pwr_to_dB(c->opts->rtl_squelch_level), cb_rtl_sql, c);
}

void
rtl_set_vol(void* v) {
    UiCtx* c = (UiCtx*)v;
    ui_prompt_open_int_async("Volume multiplier (0..3)", c->opts->rtl_volume_multiplier, cb_rtl_vol, c);
}

void
rtl_toggle_bias(void* v) {
    UiCtx* c = (UiCtx*)v;
    int32_t on = c->opts->rtl_bias_tee ? 0 : 1;
    ui_post_cmd(UI_CMD_RTL_SET_BIAS_TEE, &on, sizeof on);
}

void
rtl_toggle_rtltcp_autotune(void* v) {
    UiCtx* c = (UiCtx*)v;
    int32_t on = c->opts->rtltcp_autotune ? 0 : 1;
    ui_post_cmd(UI_CMD_RTLTCP_SET_AUTOTUNE, &on, sizeof on);
}

void
rtl_toggle_auto_ppm(void* v) {
    UiCtx* c = (UiCtx*)v;
    int32_t on = c->opts->rtl_auto_ppm ? 0 : 1;
    ui_post_cmd(UI_CMD_RTL_SET_AUTO_PPM, &on, sizeof on);
}

void
rtl_toggle_tuner_autogain(void* v) {
    UiCtx* c = (UiCtx*)v;
    if (c && c->state && c->state->rtl_ctx) {
        UiDspPayload p = {.op = UI_DSP_OP_TUNER_AUTOGAIN_TOGGLE};
        ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
    } else {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        int on = (cfg && cfg->tuner_autogain_enable) ? 1 : 0;
        dsd_setenv("DSD_NEO_TUNER_AUTOGAIN", on ? "0" : "1", 1);
        env_reparse_runtime_cfg(c ? c->opts : NULL);
    }
}

void
switch_to_rtl(void* vctx) {
    UNUSED(vctx);
    ui_post_cmd(UI_CMD_RTL_ENABLE_INPUT, NULL, 0);
}

// ---- DSP actions ----

void
act_toggle_cq(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_TOGGLE_CQ};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_toggle_fll(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_TOGGLE_FLL};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_toggle_ted(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_TOGGLE_TED};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_toggle_iqbal(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_TOGGLE_IQBAL};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_toggle_fm_agc(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_FM_AGC_TOGGLE};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_toggle_fm_limiter(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_FM_LIMITER_TOGGLE};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_fm_agc_target_up(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_FM_AGC_TARGET_DELTA, .a = +2};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_fm_agc_target_dn(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_FM_AGC_TARGET_DELTA, .a = -2};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_fm_agc_min_up(void* v) {
    UNUSED(v);
    float mn = 0.0f;
    rtl_stream_get_fm_agc_params(NULL, &mn, NULL, NULL);
    mn += 0.01f;
    if (mn > 1.0f) {
        mn = 1.0f;
    }
    rtl_stream_set_fm_agc_params(-1.0f, mn, -1.0f, -1.0f);
}

void
act_fm_agc_min_dn(void* v) {
    UNUSED(v);
    float mn = 0.0f;
    rtl_stream_get_fm_agc_params(NULL, &mn, NULL, NULL);
    mn -= 0.01f;
    if (mn < 0.0f) {
        mn = 0.0f;
    }
    rtl_stream_set_fm_agc_params(-1.0f, mn, -1.0f, -1.0f);
}

void
act_fm_agc_alpha_up_up(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_FM_AGC_ATTACK_DELTA, .a = +5};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_fm_agc_alpha_up_dn(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_FM_AGC_ATTACK_DELTA, .a = -5};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_fm_agc_alpha_down_up(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_FM_AGC_DECAY_DELTA, .a = +5};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_fm_agc_alpha_down_dn(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_FM_AGC_DECAY_DELTA, .a = -5};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_toggle_iq_dc(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_IQ_DC_TOGGLE};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_iq_dc_k_up(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_IQ_DC_K_DELTA, .a = +1};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_iq_dc_k_dn(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_IQ_DC_K_DELTA, .a = -1};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_ted_gain_up(void* v) {
    UNUSED(v);
    float g = rtl_stream_get_ted_gain();
    int g_milli = (int)(g * 1000.0f + 0.5f);
    if (g_milli < 500) {
        g_milli += 5;
    }
    UiDspPayload p = {.op = UI_DSP_OP_TED_GAIN_SET, .a = g_milli};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_ted_gain_dn(void* v) {
    UNUSED(v);
    float g = rtl_stream_get_ted_gain();
    int g_milli = (int)(g * 1000.0f + 0.5f);
    if (g_milli > 10) {
        g_milli -= 5;
    }
    UiDspPayload p = {.op = UI_DSP_OP_TED_GAIN_SET, .a = g_milli};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_ted_force_toggle(void* v) {
    UNUSED(v);
    int f = rtl_stream_get_ted_force();
    if (!f) {
        rtl_stream_set_ted_force(1);
        int cq = 0, fl = 0, t = 0;
        rtl_stream_dsp_get(&cq, &fl, &t);
        if (!t) {
            rtl_stream_toggle_ted(1);
        }
    } else {
        rtl_stream_set_ted_force(0);
    }
}

void
act_c4fm_clk_cycle(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_C4FM_CLK_CYCLE};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_c4fm_clk_sync_toggle(void* v) {
    UNUSED(v);
    UiDspPayload p = {.op = UI_DSP_OP_C4FM_CLK_SYNC_TOGGLE};
    ui_post_cmd(UI_CMD_DSP_OP, &p, sizeof p);
}

void
act_toggle_dsp_panel(void* v) {
    UNUSED(v);
    ui_post_cmd(UI_CMD_UI_SHOW_DSP_PANEL_TOGGLE, NULL, 0);
}

#endif /* USE_RTLSDR */
