// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Async callback handlers for menu prompts.
 */

#include "menu_callbacks.h"

#include "menu_env.h"
#include "menu_internal.h"
#include "menu_prompts.h"

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/ui/menu_core.h>
#include <dsd-neo/ui/ui_async.h>
#include <dsd-neo/ui/ui_cmd.h>
#include <dsd-neo/ui/ui_prims.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Simple path callbacks ----

void
cb_event_log_set(void* v, const char* path) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (path && *path) {
        ui_post_cmd(UI_CMD_EVENT_LOG_SET, path, strlen(path) + 1);
        ui_statusf("Event log set requested");
    }
}

void
cb_static_wav(void* v, const char* path) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (path && *path) {
        ui_post_cmd(UI_CMD_WAV_STATIC_OPEN, path, strlen(path) + 1);
        ui_statusf("Static WAV open requested");
    }
}

void
cb_raw_wav(void* v, const char* path) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (path && *path) {
        ui_post_cmd(UI_CMD_WAV_RAW_OPEN, path, strlen(path) + 1);
        ui_statusf("Raw WAV open requested");
    }
}

void
cb_dsp_out(void* v, const char* name) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (name && *name) {
        ui_post_cmd(UI_CMD_DSP_OUT_SET, name, strlen(name) + 1);
        ui_statusf("DSP output set requested");
    }
}

void
cb_import_chan(void* v, const char* p) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (p && *p) {
        ui_post_cmd(UI_CMD_IMPORT_CHANNEL_MAP, p, strlen(p) + 1);
        ui_statusf("Import channel map requested");
    }
}

void
cb_import_group(void* v, const char* p) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (p && *p) {
        ui_post_cmd(UI_CMD_IMPORT_GROUP_LIST, p, strlen(p) + 1);
        ui_statusf("Import group list requested");
    }
}

void
cb_keys_dec(void* v, const char* p) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (p && *p) {
        ui_post_cmd(UI_CMD_IMPORT_KEYS_DEC, p, strlen(p) + 1);
        ui_statusf("Import keys (DEC) requested");
    }
}

void
cb_keys_hex(void* v, const char* p) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (p && *p) {
        ui_post_cmd(UI_CMD_IMPORT_KEYS_HEX, p, strlen(p) + 1);
        ui_statusf("Import keys (HEX) requested");
    }
}

// ---- Config callbacks ----

void
cb_config_load(void* v, const char* path) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (!path || !*path) {
        ui_statusf("Config load canceled");
        return;
    }

    dsdneoUserConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    if (dsd_user_config_load(path, &cfg) != 0) {
        ui_statusf("Failed to load config from %s", path);
        return;
    }

    // Treat UI-loaded configs as the active config path for later saves/autosave.
    if (c->state) {
        c->state->config_autosave_enabled = 1;
        snprintf(c->state->config_autosave_path, sizeof c->state->config_autosave_path, "%s", path);
        c->state->config_autosave_path[sizeof c->state->config_autosave_path - 1] = '\0';
    }

    ui_post_cmd(UI_CMD_CONFIG_APPLY, &cfg, sizeof cfg);
    ui_statusf("Config loaded from %s", path);
}

void
cb_config_save_as(void* v, const char* path) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (!path || !*path) {
        ui_statusf("Config save canceled");
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

// ---- Typed value callbacks ----

void
cb_setmod_bw(void* v, int ok, int bw) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (ok) {
        int32_t hz = bw;
        ui_post_cmd(UI_CMD_RIGCTL_SET_MOD_BW, &hz, sizeof hz);
    }
}

void
cb_tg_hold(void* v, int ok, int tg) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (ok) {
        uint32_t t = (unsigned)tg;
        ui_post_cmd(UI_CMD_TG_HOLD_SET, &t, sizeof t);
    }
}

void
cb_hangtime(void* v, int ok, double s) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (ok) {
        double d = s;
        ui_post_cmd(UI_CMD_HANGTIME_SET, &d, sizeof d);
    }
}

void
cb_slot_pref(void* v, int ok, int p) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (ok) {
        if (p < 1) {
            p = 1;
        }
        if (p > 2) {
            p = 2;
        }
        int32_t pref01 = p - 1;
        ui_post_cmd(UI_CMD_SLOT_PREF_SET, &pref01, sizeof pref01);
    }
}

void
cb_slots_on(void* v, int ok, int m) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (ok) {
        int32_t mask = m;
        ui_post_cmd(UI_CMD_SLOTS_ONOFF_SET, &mask, sizeof mask);
    }
}

// ---- Keystream callbacks ----

void
cb_tyt_ap(void* v, const char* s) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (s && *s) {
        ui_post_cmd(UI_CMD_KEY_TYT_AP_SET, s, strlen(s) + 1);
        ui_statusf("TYT AP keystream set requested");
    }
}

void
cb_retevis_rc2(void* v, const char* s) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (s && *s) {
        ui_post_cmd(UI_CMD_KEY_RETEVIS_RC2_SET, s, strlen(s) + 1);
        ui_statusf("Retevis AP keystream set requested");
    }
}

void
cb_tyt_ep(void* v, const char* s) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (s && *s) {
        ui_post_cmd(UI_CMD_KEY_TYT_EP_SET, s, strlen(s) + 1);
        ui_statusf("TYT EP keystream set requested");
    }
}

void
cb_ken_scr(void* v, const char* s) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (s && *s) {
        ui_post_cmd(UI_CMD_KEY_KEN_SCR_SET, s, strlen(s) + 1);
        ui_statusf("Kenwood scrambler keystream set requested");
    }
}

void
cb_anytone_bp(void* v, const char* s) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (s && *s) {
        ui_post_cmd(UI_CMD_KEY_ANYTONE_BP_SET, s, strlen(s) + 1);
        ui_statusf("Anytone BP keystream set requested");
    }
}

void
cb_xor_ks(void* v, const char* s) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (s && *s) {
        ui_post_cmd(UI_CMD_KEY_XOR_SET, s, strlen(s) + 1);
        ui_statusf("XOR keystream set requested");
    }
}

// ---- Key entry callbacks ----

void
cb_key_basic(void* v, int ok, int val) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (ok) {
        unsigned long long vdec = val;
        if (vdec > 255ULL) {
            vdec = 255ULL;
        }
        uint32_t k = (uint32_t)vdec;
        ui_post_cmd(UI_CMD_KEY_BASIC_SET, &k, sizeof k);
    }
}

void
cb_key_scrambler(void* v, int ok, int val) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (ok) {
        unsigned long long vdec = val;
        if (vdec > 0x7FFFULL) {
            vdec = 0x7FFFULL;
        }
        uint32_t r = (uint32_t)vdec;
        ui_post_cmd(UI_CMD_KEY_SCRAMBLER_SET, &r, sizeof r);
    }
}

void
cb_key_rc4des(void* v, const char* text) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (text && *text) {
        unsigned long long th = 0ULL;
        if (parse_hex_u64(text, &th)) {
            uint64_t r = th;
            ui_post_cmd(UI_CMD_KEY_RC4DES_SET, &r, sizeof r);
        }
    }
}

// ---- Multi-step callbacks ----

void
cb_hytera_step(void* u, const char* text) {
    HyCtx* hc = (HyCtx*)u;
    if (!hc) {
        return;
    }
    unsigned long long t = 0ULL;
    if (text && *text && parse_hex_u64(text, &t)) {
        if (hc->step == 0) {
            hc->H = t;
            hc->K1 = t;
        } else if (hc->step == 1) {
            hc->K2 = t;
        } else if (hc->step == 2) {
            hc->K3 = t;
        } else if (hc->step == 3) {
            hc->K4 = t;
        }
    }
    hc->step++;
    if (hc->step == 1) {
        ui_prompt_open_string_async("Hytera Privacy Key 2 (HEX) or 0", NULL, 128, cb_hytera_step, hc);
        return;
    }
    if (hc->step == 2) {
        ui_prompt_open_string_async("Hytera Privacy Key 3 (HEX) or 0", NULL, 128, cb_hytera_step, hc);
        return;
    }
    if (hc->step == 3) {
        ui_prompt_open_string_async("Hytera Privacy Key 4 (HEX) or 0", NULL, 128, cb_hytera_step, hc);
        return;
    }

    struct {
        uint64_t H, K1, K2, K3, K4;
    } p = {hc->H, hc->K1, hc->K2, hc->K3, hc->K4};

    ui_post_cmd(UI_CMD_KEY_HYTERA_SET, &p, sizeof p);
    free(hc);
}

void
cb_aes_step(void* u, const char* text) {
    AesCtx* ac = (AesCtx*)u;
    if (!ac) {
        return;
    }
    unsigned long long t = 0ULL;
    if (text && *text && parse_hex_u64(text, &t)) {
        if (ac->step == 0) {
            ac->K1 = t;
        } else if (ac->step == 1) {
            ac->K2 = t;
        } else if (ac->step == 2) {
            ac->K3 = t;
        } else if (ac->step == 3) {
            ac->K4 = t;
        }
    }
    ac->step++;
    if (ac->step == 1) {
        ui_prompt_open_string_async("AES Segment 2 (HEX) or 0", NULL, 128, cb_aes_step, ac);
        return;
    }
    if (ac->step == 2) {
        ui_prompt_open_string_async("AES Segment 3 (HEX) or 0", NULL, 128, cb_aes_step, ac);
        return;
    }
    if (ac->step == 3) {
        ui_prompt_open_string_async("AES Segment 4 (HEX) or 0", NULL, 128, cb_aes_step, ac);
        return;
    }

    struct {
        uint64_t K1, K2, K3, K4;
    } p = {ac->K1, ac->K2, ac->K3, ac->K4};

    ui_post_cmd(UI_CMD_KEY_AES_SET, &p, sizeof p);
    free(ac);
}

void
cb_p2_step(void* u, const char* text) {
    P2Ctx* pc = (P2Ctx*)u;
    if (!pc) {
        return;
    }
    unsigned long long t = 0ULL;
    if (text && *text) {
        parse_hex_u64(text, &t);
    }
    if (pc->step == 0) {
        pc->w = t;
    } else if (pc->step == 1) {
        pc->s = t;
    } else if (pc->step == 2) {
        pc->n = t;
    }
    pc->step++;
    char pre[64];
    if (pc->step == 1) {
        snprintf(pre, sizeof pre, "%llX", (unsigned long long)pc->c->state->p2_sysid);
        ui_prompt_open_string_async("Enter Phase 2 SYSID (HEX)", pre, sizeof pre, cb_p2_step, pc);
        return;
    }
    if (pc->step == 2) {
        snprintf(pre, sizeof pre, "%llX", (unsigned long long)pc->c->state->p2_cc);
        ui_prompt_open_string_async("Enter Phase 2 NAC/CC (HEX)", pre, sizeof pre, cb_p2_step, pc);
        return;
    }

    struct {
        uint64_t w;
        uint64_t s;
        uint64_t n;
    } p = {pc->w, pc->s, pc->n};

    ui_post_cmd(UI_CMD_P25_P2_PARAMS_SET, &p, sizeof p);
    free(pc);
}

// ---- IO callbacks ----

void
cb_io_save_symbol_capture(void* v, const char* path) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (path && *path) {
        ui_post_cmd(UI_CMD_SYMCAP_OPEN, path, strlen(path) + 1);
        ui_statusf("Symbol capture open requested");
    }
}

void
cb_io_read_symbol_bin(void* v, const char* path) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (path && *path) {
        ui_post_cmd(UI_CMD_SYMBOL_IN_OPEN, path, strlen(path) + 1);
        ui_statusf("Symbol input open requested");
    }
}

void
cb_udp_out_port(void* u, int ok, int port) {
    UdpOutCtx* ctx = (UdpOutCtx*)u;
    if (!ctx) {
        return;
    }
    if (!ok) {
        free(ctx);
        return;
    }
    ctx->port = port;

    struct {
        char host[256];
        int32_t port;
    } payload = {0};

    snprintf(payload.host, sizeof payload.host, "%s", ctx->host);
    payload.port = port;
    ui_post_cmd(UI_CMD_UDP_OUT_CFG, &payload, sizeof payload);
    ui_statusf("UDP out requested: %s:%d", ctx->host, ctx->port);
    free(ctx);
}

void
cb_udp_out_host(void* u, const char* host) {
    UdpOutCtx* ctx = (UdpOutCtx*)u;
    if (!ctx) {
        return;
    }
    if (!host || !*host) {
        free(ctx);
        return;
    }
    snprintf(ctx->host, sizeof ctx->host, "%s", host);
    int port_default = ctx->c->opts->udp_portno > 0 ? ctx->c->opts->udp_portno : 23456;
    ui_prompt_open_int_async("UDP blaster port", port_default, cb_udp_out_port, ctx);
}

void
cb_tcp_port(void* u, int ok, int port) {
    TcpLinkCtx* ctx = (TcpLinkCtx*)u;
    if (!ctx) {
        return;
    }
    if (!ok) {
        free(ctx);
        return;
    }
    ctx->port = port;

    struct {
        char host[256];
        int32_t port;
    } payload = {0};

    snprintf(payload.host, sizeof payload.host, "%s", ctx->host);
    payload.port = ctx->port;
    ui_post_cmd(UI_CMD_TCP_CONNECT_AUDIO_CFG, &payload, sizeof payload);
    ui_statusf("TCP connect requested: %s:%d", ctx->host, ctx->port);
    free(ctx);
}

void
cb_tcp_host(void* u, const char* host) {
    TcpLinkCtx* ctx = (TcpLinkCtx*)u;
    if (!ctx) {
        return;
    }
    if (!host || !*host) {
        free(ctx);
        return;
    }
    snprintf(ctx->host, sizeof ctx->host, "%s", host);
    int defp = ctx->c->opts->tcp_portno > 0 ? ctx->c->opts->tcp_portno : 7355;
    ui_prompt_open_int_async("Enter TCP Direct Link Port Number", defp, cb_tcp_port, ctx);
}

void
cb_udp_in_port(void* u, int ok, int port) {
    UdpInCtx* ctx = (UdpInCtx*)u;
    if (!ctx) {
        return;
    }
    if (!ok) {
        free(ctx);
        return;
    }
    ctx->port = port;

    struct {
        char bind[256];
        int32_t port;
    } payload = {0};

    snprintf(payload.bind, sizeof payload.bind, "%s", ctx->addr);
    payload.port = ctx->port;
    ui_post_cmd(UI_CMD_UDP_INPUT_CFG, &payload, sizeof payload);
    ui_statusf("UDP input set requested: %s:%d", ctx->addr, ctx->port);
    free(ctx);
}

void
cb_udp_in_addr(void* u, const char* addr) {
    UdpInCtx* ctx = (UdpInCtx*)u;
    if (!ctx) {
        return;
    }
    if (!addr || !*addr) {
        free(ctx);
        return;
    }
    snprintf(ctx->addr, sizeof ctx->addr, "%s", addr);
    int defp = ctx->c->opts->udp_in_portno > 0 ? ctx->c->opts->udp_in_portno : 7355;
    ui_prompt_open_int_async("Enter UDP bind port", defp, cb_udp_in_port, ctx);
}

void
cb_rig_port(void* u, int ok, int port) {
    RigCtx* ctx = (RigCtx*)u;
    if (!ctx) {
        return;
    }
    if (!ok) {
        free(ctx);
        return;
    }
    ctx->port = port;

    struct {
        char host[256];
        int32_t port;
    } payload = {0};

    snprintf(payload.host, sizeof payload.host, "%s", ctx->host);
    payload.port = ctx->port;
    ui_post_cmd(UI_CMD_RIGCTL_CONNECT_CFG, &payload, sizeof payload);
    ui_statusf("Rigctl connect requested: %s:%d", ctx->host, ctx->port);
    free(ctx);
}

void
cb_rig_host(void* u, const char* host) {
    RigCtx* ctx = (RigCtx*)u;
    if (!ctx) {
        return;
    }
    if (!host || !*host) {
        free(ctx);
        return;
    }
    snprintf(ctx->host, sizeof ctx->host, "%s", host);
    int defp = ctx->c->opts->rigctlportno > 0 ? ctx->c->opts->rigctlportno : 4532;
    ui_prompt_open_int_async("Enter RIGCTL Port Number", defp, cb_rig_port, ctx);
}

void
cb_switch_to_wav(void* v, const char* path) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (path && *path) {
        ui_post_cmd(UI_CMD_INPUT_WAV_SET, path, strlen(path) + 1);
        ui_statusf("WAV input requested: %s", path);
    }
}

void
cb_switch_to_symbol(void* v, const char* path) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (path && *path) {
        size_t len = strlen(path);
        if (len >= 4 && dsd_strcasecmp(path + len - 4, ".bin") == 0) {
            ui_post_cmd(UI_CMD_SYMBOL_IN_OPEN, path, strlen(path) + 1);
            ui_statusf("Symbol input open requested");
        } else {
            ui_post_cmd(UI_CMD_INPUT_SYM_STREAM_SET, path, strlen(path) + 1);
            ui_statusf("Symbol stream input requested");
        }
    }
}

// ---- Gain callbacks ----

void
cb_gain_dig(void* u, int ok, double g) {
    UiCtx* c = (UiCtx*)u;
    if (!c) {
        return;
    }
    if (ok) {
        if (g < 0.0) {
            g = 0.0;
        }
        if (g > 50.0) {
            g = 50.0;
        }
        int32_t v = (int32_t)g;
        ui_post_cmd(UI_CMD_GAIN_SET, &v, sizeof v);
        ui_statusf("Digital gain set requested to %.1f", g);
    }
}

void
cb_gain_ana(void* u, int ok, double g) {
    UiCtx* c = (UiCtx*)u;
    if (!c) {
        return;
    }
    if (ok) {
        if (g < 0.0) {
            g = 0.0;
        }
        if (g > 100.0) {
            g = 100.0;
        }
        int32_t v = (int32_t)g;
        ui_post_cmd(UI_CMD_AGAIN_SET, &v, sizeof v);
        ui_statusf("Analog gain set requested to %.1f", g);
    }
}

void
cb_input_vol(void* u, int ok, int m) {
    UiCtx* c = (UiCtx*)u;
    if (!c) {
        return;
    }
    if (ok) {
        if (m < 1) {
            m = 1;
        }
        if (m > 16) {
            m = 16;
        }
        int32_t v = m;
        ui_post_cmd(UI_CMD_INPUT_VOL_SET, &v, sizeof v);
        ui_statusf("Input Volume set requested to %dX", m);
    }
}

// ---- RTL callbacks ----

void
cb_rtl_dev(void* u, int ok, int i) {
    UiCtx* c = (UiCtx*)u;
    if (!c) {
        return;
    }
    if (ok) {
        int32_t v = i;
        ui_post_cmd(UI_CMD_RTL_SET_DEV, &v, sizeof v);
    }
}

void
cb_rtl_freq(void* u, int ok, int f) {
    UiCtx* c = (UiCtx*)u;
    if (!c) {
        return;
    }
    if (ok) {
        int32_t v = f;
        ui_post_cmd(UI_CMD_RTL_SET_FREQ, &v, sizeof v);
    }
}

void
cb_rtl_gain(void* u, int ok, int g) {
    UiCtx* c = (UiCtx*)u;
    if (!c) {
        return;
    }
    if (ok) {
        int32_t v = g;
        ui_post_cmd(UI_CMD_RTL_SET_GAIN, &v, sizeof v);
    }
}

void
cb_rtl_ppm(void* u, int ok, int p) {
    UiCtx* c = (UiCtx*)u;
    if (!c) {
        return;
    }
    if (ok) {
        int32_t v = p;
        ui_post_cmd(UI_CMD_RTL_SET_PPM, &v, sizeof v);
    }
}

void
cb_rtl_bw(void* u, int ok, int bw) {
    UiCtx* c = (UiCtx*)u;
    if (!c) {
        return;
    }
    if (ok) {
        int32_t v = bw;
        ui_post_cmd(UI_CMD_RTL_SET_BW, &v, sizeof v);
    }
}

void
cb_rtl_sql(void* u, int ok, double dB) {
    UiCtx* c = (UiCtx*)u;
    if (!c) {
        return;
    }
    if (ok) {
        double v = dB;
        ui_post_cmd(UI_CMD_RTL_SET_SQL_DB, &v, sizeof v);
    }
}

void
cb_rtl_vol(void* u, int ok, int m) {
    UiCtx* c = (UiCtx*)u;
    if (!c) {
        return;
    }
    if (ok) {
        int32_t v = m;
        ui_post_cmd(UI_CMD_RTL_SET_VOL_MULT, &v, sizeof v);
    }
}

// ---- DSP/Env callbacks ----

void
cb_input_warn(void* v, int ok, double thr) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (!ok) {
        return;
    }
    if (thr < -200.0) {
        thr = -200.0;
    }
    if (thr > 0.0) {
        thr = 0.0;
    }
    ui_post_cmd(UI_CMD_INPUT_WARN_DB_SET, &thr, sizeof thr);
    env_set_double("DSD_NEO_INPUT_WARN_DB", thr);
}

void
cb_set_p25_num(void* u, int ok, double val) {
    P25NumCtx* pc = (P25NumCtx*)u;
    if (!pc) {
        return;
    }
    if (ok) {
        env_set_double(pc->name, val);
    }
    free(pc);
}

void
cb_audio_lpf(void* v, int ok, int hz) {
    UiCtx* c = (UiCtx*)v;
    if (!c || !ok) {
        return;
    }
    if (hz <= 0) {
        dsd_setenv("DSD_NEO_AUDIO_LPF", "off", 1);
    } else {
        env_set_int("DSD_NEO_AUDIO_LPF", hz);
    }
    env_reparse_runtime_cfg(c->opts);
}

void
cb_auto_ppm_snr(void* v, int ok, double d) {
    (void)v;
    if (!ok) {
        return;
    }
    env_set_double("DSD_NEO_AUTO_PPM_SNR_DB", d);
}

void
cb_auto_ppm_pwr(void* v, int ok, double d) {
    (void)v;
    if (ok) {
        env_set_double("DSD_NEO_AUTO_PPM_PWR_DB", d);
    }
}

void
cb_auto_ppm_zeroppm(void* v, int ok, double p) {
    (void)v;
    if (ok) {
        env_set_double("DSD_NEO_AUTO_PPM_ZEROLOCK_PPM", p);
    }
}

void
cb_auto_ppm_zerohz(void* v, int ok, int h) {
    (void)v;
    if (ok) {
        env_set_int("DSD_NEO_AUTO_PPM_ZEROLOCK_HZ", h);
    }
}

void
cb_tcp_prebuf(void* v, int ok, int ms) {
    UiCtx* c = (UiCtx*)v;
    if (!ok) {
        return;
    }
    env_set_int("DSD_NEO_TCP_PREBUF_MS", ms);
    if (c && c->opts && c->opts->audio_in_type == AUDIO_IN_RTL) {
        ui_post_cmd(UI_CMD_RTL_RESTART, NULL, 0);
    }
}

void
cb_tcp_rcvbuf(void* v, int ok, int sz) {
    UiCtx* c = (UiCtx*)v;
    if (!ok) {
        return;
    }
    if (sz <= 0) {
        dsd_setenv("DSD_NEO_TCP_RCVBUF", "", 1);
    } else {
        env_set_int("DSD_NEO_TCP_RCVBUF", sz);
    }
    if (c && c->opts && c->opts->audio_in_type == AUDIO_IN_RTL) {
        ui_post_cmd(UI_CMD_RTL_RESTART, NULL, 0);
    }
}

void
cb_tcp_rcvtimeo(void* v, int ok, int ms) {
    UiCtx* c = (UiCtx*)v;
    if (!ok) {
        return;
    }
    if (ms <= 0) {
        dsd_setenv("DSD_NEO_TCP_RCVTIMEO", "", 1);
    } else {
        env_set_int("DSD_NEO_TCP_RCVTIMEO", ms);
    }
    if (c && c->opts && c->opts->audio_in_type == AUDIO_IN_RTL) {
        ui_post_cmd(UI_CMD_RTL_RESTART, NULL, 0);
    }
}

// ---- LRRP callback ----

void
cb_lr_custom(void* v, const char* path) {
    UiCtx* c = (UiCtx*)v;
    if (!c) {
        return;
    }
    if (path && *path) {
        ui_post_cmd(UI_CMD_LRRP_SET_CUSTOM, path, strlen(path) + 1);
        ui_statusf("LRRP custom output requested");
    }
}

// ---- Env editor callbacks ----

void
cb_env_edit_value(void* u, const char* val) {
    EnvEditCtx* ec = (EnvEditCtx*)u;
    if (!ec) {
        return;
    }
    if (val && *val) {
        dsd_setenv(ec->name, val, 1);
        // Apply to runtime config as appropriate
        env_reparse_runtime_cfg(ec->c ? ec->c->opts : NULL);
    }
    free(ec);
}

void
cb_env_edit_name(void* u, const char* name) {
    EnvEditCtx* ec = (EnvEditCtx*)u;
    if (!ec) {
        return;
    }
    if (!name || !*name) {
        free(ec);
        return;
    }
    // Require DSD_NEO_ prefix for safety
    if (dsd_strncasecmp(name, "DSD_NEO_", 8) != 0) {
        free(ec);
        return;
    }
    snprintf(ec->name, sizeof ec->name, "%s", name);
    const char* cur = dsd_neo_env_get(ec->name);
    ui_prompt_open_string_async("Enter value (empty to clear)", cur ? cur : "", 256, cb_env_edit_value, ec);
}

// ---- M17 callback ----

void
cb_m17_user_data(void* u, const char* text) {
    M17Ctx* mc = (M17Ctx*)u;
    if (mc && mc->c && text && *text) {
        ui_post_cmd(UI_CMD_M17_USER_DATA_SET, text, strlen(text) + 1);
        ui_statusf("M17 user data set requested");
    }
    free(mc);
}

// ---- Chooser completion handlers ----

static void
chooser_free_lists(const char** names, char** bufs, int n, const char** labels) {
    for (int i = 0; i < n; i++) {
        if (names) {
            free((void*)names[i]);
        }
        if (bufs) {
            free(bufs[i]);
        }
    }
    if (labels) {
        free((void*)labels);
    }
    if (names) {
        free((void*)names);
    }
    if (bufs) {
        free((void*)bufs);
    }
}

void
chooser_done_pulse_out(void* u, int sel) {
    PulseSelCtx* pc = (PulseSelCtx*)u;
    if (pc) {
        if (sel >= 0 && sel < pc->n) {
            const char* name = pc->names[sel];
            ui_post_cmd(UI_CMD_PULSE_OUT_SET, name, strlen(name) + 1);
            ui_statusf("Pulse out requested: %s", name);
        }
        chooser_free_lists(pc->names, pc->bufs, pc->n, pc->labels);
        free(pc);
    }
}

void
chooser_done_pulse_in(void* u, int sel) {
    PulseSelCtx* pc = (PulseSelCtx*)u;
    if (pc) {
        if (sel >= 0 && sel < pc->n) {
            const char* name = pc->names[sel];
            ui_post_cmd(UI_CMD_PULSE_IN_SET, name, strlen(name) + 1);
            ui_statusf("Pulse in requested: %s", name);
        }
        chooser_free_lists(pc->names, pc->bufs, pc->n, pc->labels);
        free(pc);
    }
}
