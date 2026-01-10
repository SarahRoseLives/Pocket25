// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime configuration parser for environment-derived settings.
 *
 * Parses environment variables into a typed `dsd-neoRuntimeConfig` and exposes
 * an immutable accessor. Intended to be called early during application init.
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/dsp/costas.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/runtime/config.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static dsdneoRuntimeConfig g_config;
static int g_config_inited = 0;

/**
 * @brief Check whether an environment string is set and non-empty.
 *
 * @param v Environment value string pointer (may be NULL).
 * @return 1 if set and non-empty; otherwise 0.
 */
static int
env_is_set(const char* v) {
    return v && v[0] != '\0';
}

static int
env_is_truthy(const char* v) {
    if (!env_is_set(v)) {
        return 0;
    }
    return (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T');
}

static int
env_is_falsey(const char* v) {
    if (!env_is_set(v)) {
        return 0;
    }
    return (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

static int
env_parse_int_range(const char* v, int min_val, int max_val, int* out) {
    if (!env_is_set(v) || !out) {
        return 0;
    }
    char* end = NULL;
    long x = strtol(v, &end, 10);
    if (end == v) {
        return 0;
    }
    if (x < (long)min_val || x > (long)max_val) {
        return 0;
    }
    *out = (int)x;
    return 1;
}

static int
env_parse_long_range(const char* v, long min_val, long max_val, long* out) {
    if (!env_is_set(v) || !out) {
        return 0;
    }
    char* end = NULL;
    long x = strtol(v, &end, 10);
    if (end == v) {
        return 0;
    }
    if (x < min_val || x > max_val) {
        return 0;
    }
    *out = x;
    return 1;
}

static int
env_parse_double_range(const char* v, double min_val, double max_val, double* out) {
    if (!env_is_set(v) || !out) {
        return 0;
    }
    char* end = NULL;
    double x = strtod(v, &end);
    if (end == v) {
        return 0;
    }
    if (x < min_val || x > max_val) {
        return 0;
    }
    *out = x;
    return 1;
}

static void
env_copy_str(char* dst, size_t dst_size, const char* v) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!env_is_set(v)) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", v);
    dst[dst_size - 1] = '\0';
}

static void
default_cache_dir(char* dst, size_t dst_size) {
    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';

    const char* base = getenv("HOME");
#if defined(_WIN32)
    if (!env_is_set(base)) {
        base = getenv("LOCALAPPDATA");
    }
#endif
    if (env_is_set(base)) {
        snprintf(dst, dst_size, "%s/.cache/dsd-neo", base);
        dst[dst_size - 1] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", ".dsdneo_cache");
    dst[dst_size - 1] = '\0';
}

/**
 * @brief Parse environment variables and initialize the runtime configuration.
 *
 * Precedence note: future CLI/opts may override env values; currently opts
 * are not applied beyond presence for future extension.
 *
 * @param opts Decoder options for potential precedence overrides.
 * @note Safe to call multiple times; the most recent call wins.
 */
void
dsd_neo_config_init(const dsd_opts* opts) {
    (void)opts; /* precedence hook reserved for future CLI/opts overrides */

    dsdneoRuntimeConfig c{};

    /* Defaults for centralized knobs (may be overridden by env parsing below). */
    c.sync_warmstart_enable = 1;

    c.dmr_hangtime_s = 2.0;
    c.dmr_grant_timeout_s = 4.0;

    c.p25_hangtime_s = 2.0;
    c.p25_grant_timeout_s = 3.0;
    c.p25_cc_grace_s = 5.0;
    c.p25_vc_grace_s = 0.75;
    c.p25_ring_hold_s = 0.75;
    c.p25_mac_hold_s = 0.75;
    c.p25_voice_hold_s = 0.75;
    c.p25_wd_ms = 0; /* 0 => dynamic default selected by caller */

    c.p25p1_soft_erasure_thresh = 64;
    c.p25p2_soft_erasure_thresh = 64;

    c.input_volume_multiplier = 1;
    c.input_warn_db = -40.0;

    c.dmr_t3_step_hz = 0;
    c.dmr_t3_cc_freq_hz = 0;
    c.dmr_t3_cc_lcn = 0;
    c.dmr_t3_start_lcn = 1;

    c.cc_cache_enable = 1;

    c.tcp_max_timeouts = 3;
    c.tcp_rcvbuf_bytes = 4 * 1024 * 1024;
    c.tcp_rcvtimeo_ms = 2000;
    c.rigctl_rcvtimeo_ms = 1500;
    c.tcp_prebuf_ms = 1000;

    c.rtl_agc_enable = 1;
    c.rtl_direct_mode = 0;

    c.tuner_autogain_probe_ms = 3000;
    c.tuner_autogain_seed_db = 30.0;
    c.tuner_autogain_spec_snr_db = 6.0;
    c.tuner_autogain_inband_ratio = 0.60;
    c.tuner_autogain_up_step_db = 3.0;
    c.tuner_autogain_up_persist = 2;

    c.auto_ppm_snr_db = 6.0;
    c.auto_ppm_pwr_db = -80.0;
    c.auto_ppm_zerolock_ppm = 0.6;
    c.auto_ppm_zerolock_hz = 60;
    c.auto_ppm_freeze_enable = 1;

    /* User config discovery */
    const char* cfgp = getenv("DSD_NEO_CONFIG");
    c.config_path_is_set = env_is_set(cfgp);
    env_copy_str(c.config_path, sizeof c.config_path, cfgp);

    /* Cache/path knobs */
    const char* cache_dir = getenv("DSD_NEO_CACHE_DIR");
    c.cache_dir_is_set = env_is_set(cache_dir);
    if (c.cache_dir_is_set) {
        env_copy_str(c.cache_dir, sizeof c.cache_dir, cache_dir);
    } else {
        default_cache_dir(c.cache_dir, sizeof c.cache_dir);
    }

    const char* cc_cache = getenv("DSD_NEO_CC_CACHE");
    c.cc_cache_is_set = env_is_set(cc_cache);
    if (c.cc_cache_is_set) {
        c.cc_cache_enable = env_is_falsey(cc_cache) ? 0 : 1;
    }

    /* Realtime scheduling and CPU affinity */
    const char* rt = getenv("DSD_NEO_RT_SCHED");
    c.rt_sched_is_set = env_is_set(rt);
    c.rt_sched_enable = c.rt_sched_is_set ? (env_is_falsey(rt) ? 0 : 1) : 0;

    const char* rpu = getenv("DSD_NEO_RT_PRIO_USB");
    c.rt_prio_usb_is_set = env_parse_int_range(rpu, 1, 99, &c.rt_prio_usb);

    const char* rpd = getenv("DSD_NEO_RT_PRIO_DONGLE");
    c.rt_prio_dongle_is_set = env_parse_int_range(rpd, 1, 99, &c.rt_prio_dongle);

    const char* rpm = getenv("DSD_NEO_RT_PRIO_DEMOD");
    c.rt_prio_demod_is_set = env_parse_int_range(rpm, 1, 99, &c.rt_prio_demod);

    const char* cpuu = getenv("DSD_NEO_CPU_USB");
    c.cpu_usb_is_set = env_parse_int_range(cpuu, 0, 4096, &c.cpu_usb);

    const char* cpud = getenv("DSD_NEO_CPU_DONGLE");
    c.cpu_dongle_is_set = env_parse_int_range(cpud, 0, 4096, &c.cpu_dongle);

    const char* cpum = getenv("DSD_NEO_CPU_DEMOD");
    c.cpu_demod_is_set = env_parse_int_range(cpum, 0, 4096, &c.cpu_demod);

    /* Bootstrap/system toggles */
    const char* ftz = getenv("DSD_NEO_FTZ_DAZ");
    c.ftz_daz_is_set = env_is_set(ftz);
    c.ftz_daz_enable = c.ftz_daz_is_set ? (env_is_falsey(ftz) ? 0 : 1) : 0;

    const char* nb = getenv("DSD_NEO_NO_BOOTSTRAP");
    c.no_bootstrap_is_set = env_is_set(nb);
    c.no_bootstrap_enable = c.no_bootstrap_is_set ? (env_is_falsey(nb) ? 0 : 1) : 0;

    /* Debug/tuning toggles */
    const char* ds = getenv("DSD_NEO_DEBUG_SYNC");
    c.debug_sync_is_set = env_is_set(ds);
    c.debug_sync_enable = c.debug_sync_is_set ? (env_is_falsey(ds) ? 0 : 1) : 0;

    const char* dcq = getenv("DSD_NEO_DEBUG_CQPSK");
    c.debug_cqpsk_is_set = env_is_set(dcq);
    c.debug_cqpsk_enable = c.debug_cqpsk_is_set ? (env_is_falsey(dcq) ? 0 : 1) : 0;

    /* CQPSK runtime toggles */
    const char* cq = getenv("DSD_NEO_CQPSK");
    c.cqpsk_is_set = env_is_set(cq);
    if (c.cqpsk_is_set) {
        if (env_is_truthy(cq)) {
            c.cqpsk_enable = 1;
        } else if (env_is_falsey(cq)) {
            c.cqpsk_enable = 0;
        } else {
            c.cqpsk_is_set = 0; /* ignore unrecognized values */
        }
    }

    const char* cqi = getenv("DSD_NEO_CQPSK_SYNC_INV");
    c.cqpsk_sync_inv_is_set = env_is_set(cqi);
    c.cqpsk_sync_inv = c.cqpsk_sync_inv_is_set ? (env_is_falsey(cqi) ? 0 : 1) : 0;

    const char* cqn = getenv("DSD_NEO_CQPSK_SYNC_NEG");
    c.cqpsk_sync_neg_is_set = env_is_set(cqn);
    c.cqpsk_sync_neg = c.cqpsk_sync_neg_is_set ? (env_is_falsey(cqn) ? 0 : 1) : 0;

    /* Sync warm-start (kill-switch): DSD_NEO_SYNC_WARMSTART=0 disables. */
    const char* sw = getenv("DSD_NEO_SYNC_WARMSTART");
    c.sync_warmstart_is_set = env_is_set(sw);
    if (c.sync_warmstart_is_set && strcmp(sw, "0") == 0) {
        c.sync_warmstart_enable = 0;
    }

    /* DMR timers */
    const char* dmr_hang = getenv("DSD_NEO_DMR_HANGTIME");
    c.dmr_hangtime_is_set = env_parse_double_range(dmr_hang, 0.0, 10.0, &c.dmr_hangtime_s);

    const char* dmr_gt = getenv("DSD_NEO_DMR_GRANT_TIMEOUT");
    c.dmr_grant_timeout_is_set = env_parse_double_range(dmr_gt, 0.0, 30.0, &c.dmr_grant_timeout_s);

    /* P25 timers/holds */
    const char* p25_hang = getenv("DSD_NEO_P25_HANGTIME");
    c.p25_hangtime_is_set = env_parse_double_range(p25_hang, 0.0, 10.0, &c.p25_hangtime_s);

    const char* p25_gt = getenv("DSD_NEO_P25_GRANT_TIMEOUT");
    c.p25_grant_timeout_is_set = env_parse_double_range(p25_gt, 0.0, 30.0, &c.p25_grant_timeout_s);

    const char* p25_cc = getenv("DSD_NEO_P25_CC_GRACE");
    c.p25_cc_grace_is_set = env_parse_double_range(p25_cc, 0.0, 30.0, &c.p25_cc_grace_s);

    const char* p25_vcg = getenv("DSD_NEO_P25_VC_GRACE");
    c.p25_vc_grace_is_set = env_parse_double_range(p25_vcg, 0.0, 10.0, &c.p25_vc_grace_s);

    const char* p25_rh = getenv("DSD_NEO_P25_RING_HOLD");
    c.p25_ring_hold_is_set = env_parse_double_range(p25_rh, 0.0, 5.0, &c.p25_ring_hold_s);

    const char* p25_mh = getenv("DSD_NEO_P25_MAC_HOLD");
    c.p25_mac_hold_is_set = env_parse_double_range(p25_mh, 0.0, 10.0, &c.p25_mac_hold_s);

    const char* p25_vh = getenv("DSD_NEO_P25_VOICE_HOLD");
    c.p25_voice_hold_is_set = env_parse_double_range(p25_vh, 0.0, 5.0, &c.p25_voice_hold_s);

    const char* p25_wd = getenv("DSD_NEO_P25_WD_MS");
    c.p25_wd_ms_is_set = env_parse_int_range(p25_wd, 20, 2000, &c.p25_wd_ms);

    /* P25 follower (UI-exposed) knobs */
    const char* p25_mfd = getenv("DSD_NEO_P25_MIN_FOLLOW_DWELL");
    c.p25_min_follow_dwell_is_set = env_parse_double_range(p25_mfd, 0.0, 120.0, &c.p25_min_follow_dwell_s);

    const char* p25_gvt = getenv("DSD_NEO_P25_GRANT_VOICE_TO");
    c.p25_grant_voice_to_is_set = env_parse_double_range(p25_gvt, 0.0, 120.0, &c.p25_grant_voice_to_s);

    const char* p25_rb = getenv("DSD_NEO_P25_RETUNE_BACKOFF");
    c.p25_retune_backoff_is_set = env_parse_double_range(p25_rb, 0.0, 120.0, &c.p25_retune_backoff_s);

    const char* p25_fe = getenv("DSD_NEO_P25_FORCE_RELEASE_EXTRA");
    c.p25_force_release_extra_is_set = env_parse_double_range(p25_fe, 0.0, 120.0, &c.p25_force_release_extra_s);

    const char* p25_fm = getenv("DSD_NEO_P25_FORCE_RELEASE_MARGIN");
    c.p25_force_release_margin_is_set = env_parse_double_range(p25_fm, 0.0, 120.0, &c.p25_force_release_margin_s);

    const char* p25_ehp = getenv("DSD_NEO_P25P1_ERR_HOLD_PCT");
    c.p25p1_err_hold_pct_is_set = env_parse_double_range(p25_ehp, 0.0, 100.0, &c.p25p1_err_hold_pct);

    const char* p25_ehs = getenv("DSD_NEO_P25P1_ERR_HOLD_S");
    c.p25p1_err_hold_s_is_set = env_parse_double_range(p25_ehs, 0.0, 120.0, &c.p25p1_err_hold_s);

    /* P25 soft-decision erasure thresholds (0..255) */
    const char* p1e = getenv("DSD_NEO_P25P1_SOFT_ERASURE_THRESH");
    c.p25p1_soft_erasure_thresh_is_set = env_parse_int_range(p1e, 0, 255, &c.p25p1_soft_erasure_thresh);

    const char* p2e = getenv("DSD_NEO_P25P2_SOFT_ERASURE_THRESH");
    c.p25p2_soft_erasure_thresh_is_set = env_parse_int_range(p2e, 0, 255, &c.p25p2_soft_erasure_thresh);

    /* Input processing knobs */
    const char* iv = getenv("DSD_NEO_INPUT_VOLUME");
    c.input_volume_is_set = env_parse_int_range(iv, 1, 16, &c.input_volume_multiplier);

    const char* iw = getenv("DSD_NEO_INPUT_WARN_DB");
    c.input_warn_db_is_set = env_parse_double_range(iw, -200.0, 0.0, &c.input_warn_db);

    /* DMR TIII tools (one-shot LCN calculator) */
    const char* t3csv = getenv("DSD_NEO_DMR_T3_CALC_CSV");
    c.dmr_t3_calc_csv_is_set = env_is_set(t3csv);
    env_copy_str(c.dmr_t3_calc_csv, sizeof c.dmr_t3_calc_csv, t3csv);

    const char* t3step = getenv("DSD_NEO_DMR_T3_STEP_HZ");
    c.dmr_t3_step_hz_is_set = env_parse_long_range(t3step, 1, 1000000000L, &c.dmr_t3_step_hz);

    const char* t3ccl = getenv("DSD_NEO_DMR_T3_CC_LCN");
    c.dmr_t3_cc_lcn_is_set = env_parse_long_range(t3ccl, 1, 1000000000L, &c.dmr_t3_cc_lcn);

    const char* t3start = getenv("DSD_NEO_DMR_T3_START_LCN");
    c.dmr_t3_start_lcn_is_set = env_parse_long_range(t3start, 1, 1000000000L, &c.dmr_t3_start_lcn);

    const char* t3ccf = getenv("DSD_NEO_DMR_T3_CC_FREQ");
    c.dmr_t3_cc_freq_is_set = 0;
    if (env_is_set(t3ccf)) {
        char* end = NULL;
        double v = strtod(t3ccf, &end);
        if (end != t3ccf) {
            long hz = (v < 1e5) ? (long)llround(v * 1000000.0) : (long)llround(v);
            if (hz > 0) {
                c.dmr_t3_cc_freq_hz = hz;
                c.dmr_t3_cc_freq_is_set = 1;
            }
        }
    }

    /* DMR TIII heuristic fill */
    const char* t3h = getenv("DSD_NEO_DMR_T3_HEUR");
    c.dmr_t3_heur_is_set = env_is_set(t3h);
    c.dmr_t3_heur_enable = c.dmr_t3_heur_is_set ? (env_is_falsey(t3h) ? 0 : 1) : 0;

    /* TCP/rigctl knobs */
    const char* tbs = getenv("DSD_NEO_TCP_BUFSZ");
    c.tcp_bufsz_is_set = 0;
    if (env_is_set(tbs)) {
        long v = strtol(tbs, NULL, 10);
        if (v > 4096 && v < (32L * 1024L * 1024L)) {
            c.tcp_bufsz_bytes = (int)v;
            c.tcp_bufsz_is_set = 1;
        }
    }

    const char* twa = getenv("DSD_NEO_TCP_WAITALL");
    c.tcp_waitall_is_set = env_is_set(twa);
    if (c.tcp_waitall_is_set) {
        c.tcp_waitall_enable = env_is_falsey(twa) ? 0 : 1;
    }

    const char* tau = getenv("DSD_NEO_TCP_AUTOTUNE");
    c.tcp_autotune_is_set = env_is_set(tau);
    if (c.tcp_autotune_is_set) {
        c.tcp_autotune_enable = env_is_falsey(tau) ? 0 : 1;
    }

    const char* tstats = getenv("DSD_NEO_TCP_STATS");
    c.tcp_stats_is_set = env_is_set(tstats);
    if (c.tcp_stats_is_set) {
        c.tcp_stats_enable = env_is_falsey(tstats) ? 0 : 1;
    }

    const char* tmt = getenv("DSD_NEO_TCP_MAX_TIMEOUTS");
    c.tcp_max_timeouts_is_set = env_parse_int_range(tmt, 1, 100, &c.tcp_max_timeouts);

    const char* trb = getenv("DSD_NEO_TCP_RCVBUF");
    c.tcp_rcvbuf_is_set = 0;
    if (env_is_set(trb)) {
        long v = strtol(trb, NULL, 10);
        if (v > 0 && v <= INT_MAX) {
            c.tcp_rcvbuf_bytes = (int)v;
            c.tcp_rcvbuf_is_set = 1;
        }
    }

    const char* trt = getenv("DSD_NEO_TCP_RCVTIMEO");
    c.tcp_rcvtimeo_is_set = env_parse_int_range(trt, 100, 60000, &c.tcp_rcvtimeo_ms);

    const char* rrt = getenv("DSD_NEO_RIGCTL_RCVTIMEO");
    c.rigctl_rcvtimeo_is_set = env_parse_int_range(rrt, 100, 60000, &c.rigctl_rcvtimeo_ms);

    const char* tpb = getenv("DSD_NEO_TCP_PREBUF_MS");
    c.tcp_prebuf_ms_is_set = env_parse_int_range(tpb, 5, 1000, &c.tcp_prebuf_ms);

    /* RTL device/tuner knobs */
    const char* ragc = getenv("DSD_NEO_RTL_AGC");
    c.rtl_agc_is_set = env_is_set(ragc);
    if (c.rtl_agc_is_set) {
        c.rtl_agc_enable = env_is_falsey(ragc) ? 0 : 1;
    }

    const char* rdir = getenv("DSD_NEO_RTL_DIRECT");
    c.rtl_direct_is_set = 0;
    if (env_is_set(rdir)) {
        int mode = 0;
        if (rdir[0] == '1' || rdir[0] == 'I' || rdir[0] == 'i') {
            mode = 1;
        } else if (rdir[0] == '2' || rdir[0] == 'Q' || rdir[0] == 'q') {
            mode = 2;
        } else if (rdir[0] == '0') {
            mode = 0;
        } else {
            int v = atoi(rdir);
            if (v >= 0 && v <= 2) {
                mode = v;
            } else {
                mode = -1;
            }
        }
        if (mode >= 0 && mode <= 2) {
            c.rtl_direct_mode = mode;
            c.rtl_direct_is_set = 1;
        }
    }

    const char* rot = getenv("DSD_NEO_RTL_OFFSET_TUNING");
    c.rtl_offset_tuning_is_set = env_is_set(rot);
    if (c.rtl_offset_tuning_is_set) {
        c.rtl_offset_tuning_enable = env_is_falsey(rot) ? 0 : 1;
    }

    const char* rxt = getenv("DSD_NEO_RTL_XTAL_HZ");
    c.rtl_xtal_hz_is_set = env_parse_int_range(rxt, 1, 1000000000, &c.rtl_xtal_hz);

    const char* txt = getenv("DSD_NEO_TUNER_XTAL_HZ");
    c.tuner_xtal_hz_is_set = env_parse_int_range(txt, 1, 1000000000, &c.tuner_xtal_hz);

    const char* rtm = getenv("DSD_NEO_RTL_TESTMODE");
    c.rtl_testmode_is_set = env_is_set(rtm);
    if (c.rtl_testmode_is_set) {
        c.rtl_testmode_enable = env_is_falsey(rtm) ? 0 : 1;
    }

    const char* rig = getenv("DSD_NEO_RTL_IF_GAINS");
    c.rtl_if_gains_is_set = env_is_set(rig);
    env_copy_str(c.rtl_if_gains, sizeof c.rtl_if_gains, rig);

    const char* tbw = getenv("DSD_NEO_TUNER_BW_HZ");
    c.tuner_bw_hz_is_set = 0;
    if (env_is_set(tbw)) {
        if (dsd_strcasecmp(tbw, "auto") == 0) {
            c.tuner_bw_hz = 0;
            c.tuner_bw_hz_is_set = 1;
        } else {
            int v = 0;
            if (env_parse_int_range(tbw, 0, 20000000, &v)) {
                c.tuner_bw_hz = v;
                c.tuner_bw_hz_is_set = 1;
            }
        }
    }

    /* Supervisory tuner autogain knobs */
    const char* tag = getenv("DSD_NEO_TUNER_AUTOGAIN");
    c.tuner_autogain_is_set = env_is_set(tag);
    c.tuner_autogain_enable = c.tuner_autogain_is_set ? (env_is_truthy(tag) ? 1 : 0) : 0;

    const char* tap = getenv("DSD_NEO_TUNER_AUTOGAIN_PROBE_MS");
    c.tuner_autogain_probe_ms_is_set = env_parse_int_range(tap, 0, 20000, &c.tuner_autogain_probe_ms);

    const char* tas = getenv("DSD_NEO_TUNER_AUTOGAIN_SEED_DB");
    c.tuner_autogain_seed_db_is_set = env_parse_double_range(tas, 0.0, 60.0, &c.tuner_autogain_seed_db);

    const char* tss = getenv("DSD_NEO_TUNER_AUTOGAIN_SPEC_SNR_DB");
    c.tuner_autogain_spec_snr_db_is_set = env_parse_double_range(tss, 0.0, 60.0, &c.tuner_autogain_spec_snr_db);

    const char* tir = getenv("DSD_NEO_TUNER_AUTOGAIN_INBAND_RATIO");
    c.tuner_autogain_inband_ratio_is_set = env_parse_double_range(tir, 0.10, 0.95, &c.tuner_autogain_inband_ratio);

    const char* tus = getenv("DSD_NEO_TUNER_AUTOGAIN_UP_STEP_DB");
    c.tuner_autogain_up_step_db_is_set = env_parse_double_range(tus, 1.0, 10.0, &c.tuner_autogain_up_step_db);

    const char* tup = getenv("DSD_NEO_TUNER_AUTOGAIN_UP_PERSIST");
    c.tuner_autogain_up_persist_is_set = env_parse_int_range(tup, 1, 5, &c.tuner_autogain_up_persist);

    /* Auto-PPM knobs */
    const char* appm = getenv("DSD_NEO_AUTO_PPM");
    c.auto_ppm_is_set = env_is_set(appm);
    c.auto_ppm_enable = c.auto_ppm_is_set ? (env_is_truthy(appm) ? 1 : 0) : 0;

    const char* apsnr = getenv("DSD_NEO_AUTO_PPM_SNR_DB");
    c.auto_ppm_snr_db_is_set = env_parse_double_range(apsnr, 0.0, 40.0, &c.auto_ppm_snr_db);

    const char* apwr = getenv("DSD_NEO_AUTO_PPM_PWR_DB");
    c.auto_ppm_pwr_db_is_set = 0;
    if (env_is_set(apwr)) {
        char* end = NULL;
        double v = strtod(apwr, &end);
        if (end != apwr && v <= 0.0) {
            c.auto_ppm_pwr_db = v;
            c.auto_ppm_pwr_db_is_set = 1;
        }
    }

    const char* azppm = getenv("DSD_NEO_AUTO_PPM_ZEROLOCK_PPM");
    c.auto_ppm_zerolock_ppm_is_set = env_parse_double_range(azppm, 0.0, 2.0, &c.auto_ppm_zerolock_ppm);

    const char* azhz = getenv("DSD_NEO_AUTO_PPM_ZEROLOCK_HZ");
    c.auto_ppm_zerolock_hz_is_set = env_parse_int_range(azhz, 10, 500, &c.auto_ppm_zerolock_hz);

    const char* afrz = getenv("DSD_NEO_AUTO_PPM_FREEZE");
    c.auto_ppm_freeze_is_set = env_is_set(afrz);
    if (c.auto_ppm_freeze_is_set) {
        c.auto_ppm_freeze_enable = env_is_falsey(afrz) ? 0 : 1;
    }

    /* COMBINE_ROT */
    const char* cr = getenv("DSD_NEO_COMBINE_ROT");
    c.combine_rot_is_set = env_is_set(cr);
    c.combine_rot = c.combine_rot_is_set ? (atoi(cr) != 0) : 1;

    /* UPSAMPLE_FP */
    const char* ufp = getenv("DSD_NEO_UPSAMPLE_FP");
    c.upsample_fp_is_set = env_is_set(ufp);
    c.upsample_fp = c.upsample_fp_is_set ? (atoi(ufp) != 0) : 1;

    /* RESAMP */
    const char* rs = getenv("DSD_NEO_RESAMP");
    c.resamp_is_set = env_is_set(rs);
    c.resamp_disable = 0;
    c.resamp_target_hz = 48000;
    if (c.resamp_is_set) {
        if (dsd_strcasecmp(rs, "off") == 0 || strcmp(rs, "0") == 0) {
            c.resamp_disable = 1;
        } else {
            int v = atoi(rs);
            if (v > 0) {
                c.resamp_target_hz = v;
            }
        }
    }

    /* FLL */
    const char* fll = getenv("DSD_NEO_FLL");
    c.fll_is_set = env_is_set(fll);
    c.fll_enable = (c.fll_is_set && fll[0] == '1') ? 1 : 0; /* may be overridden by mode later */

    const char* fa = getenv("DSD_NEO_FLL_ALPHA");
    const char* fb = getenv("DSD_NEO_FLL_BETA");
    const char* fdb = getenv("DSD_NEO_FLL_DEADBAND");
    const char* fsl = getenv("DSD_NEO_FLL_SLEW");
    c.fll_alpha_is_set = env_is_set(fa);
    c.fll_beta_is_set = env_is_set(fb);
    c.fll_deadband_is_set = env_is_set(fdb);
    c.fll_slew_is_set = env_is_set(fsl);
    /* Native float FLL parameters (GNU Radio style):
     * alpha: proportional gain (typ 0.001-0.01)
     * beta: integral gain (typ 0.0001-0.001)
     * deadband: minimum error threshold (typ 0.001-0.01)
     * slew_max: max freq change per sample in rad/sample */
    c.fll_alpha = c.fll_alpha_is_set ? (float)atof(fa) : 0.005f;
    c.fll_beta = c.fll_beta_is_set ? (float)atof(fb) : 0.0005f;
    c.fll_deadband = c.fll_deadband_is_set ? (float)atof(fdb) : 0.003f;
    c.fll_slew_max = c.fll_slew_is_set ? (float)atof(fsl) : 0.002f;

    /* CQPSK Costas loop (carrier recovery) using GNU Radio control loop */
    const char* cbw = getenv("DSD_NEO_COSTAS_BW");
    const char* cdp = getenv("DSD_NEO_COSTAS_DAMPING");
    c.costas_bw_is_set = env_is_set(cbw);
    c.costas_damping_is_set = env_is_set(cdp);
    c.costas_loop_bw = c.costas_bw_is_set ? atof(cbw) : dsd_neo_costas_default_loop_bw();
    c.costas_damping = c.costas_damping_is_set ? atof(cdp) : dsd_neo_costas_default_damping();

    /* TED - native float Gardner timing gain */
    const char* ted = getenv("DSD_NEO_TED");
    const char* tg = getenv("DSD_NEO_TED_GAIN");
    const char* tf = getenv("DSD_NEO_TED_FORCE");
    c.ted_is_set = env_is_set(ted);
    c.ted_enable = (c.ted_is_set && ted[0] == '1') ? 1 : 0;
    c.ted_gain_is_set = env_is_set(tg);
    c.ted_gain = c.ted_gain_is_set ? (float)atof(tg) : 0.05f;
    c.ted_force_is_set = env_is_set(tf);
    c.ted_force = (c.ted_force_is_set && tf[0] == '1') ? 1 : 0;

    /* C4FM clock assist */
    const char* clk = getenv("DSD_NEO_C4FM_CLK");
    const char* clk_sync = getenv("DSD_NEO_C4FM_CLK_SYNC");
    c.c4fm_clk_is_set = env_is_set(clk);
    c.c4fm_clk_mode = 0;
    if (c.c4fm_clk_is_set && clk && clk[0] != '\0') {
        if (dsd_strcasecmp(clk, "el") == 0 || strcmp(clk, "1") == 0) {
            c.c4fm_clk_mode = 1;
        } else if (dsd_strcasecmp(clk, "mm") == 0 || strcmp(clk, "2") == 0) {
            c.c4fm_clk_mode = 2;
        }
    }
    c.c4fm_clk_sync_is_set = env_is_set(clk_sync);
    c.c4fm_clk_sync = c.c4fm_clk_sync_is_set ? ((clk_sync[0] == '1') ? 1 : 0) : 0;

    /* Deemphasis */
    const char* deemph = getenv("DSD_NEO_DEEMPH");
    c.deemph_is_set = env_is_set(deemph);
    c.deemph_mode = DSD_NEO_DEEMPH_UNSET;
    if (c.deemph_is_set) {
        if (dsd_strcasecmp(deemph, "off") == 0 || strcmp(deemph, "0") == 0) {
            c.deemph_mode = DSD_NEO_DEEMPH_OFF;
        } else if (strcmp(deemph, "50") == 0) {
            c.deemph_mode = DSD_NEO_DEEMPH_50;
        } else if (strcmp(deemph, "75") == 0) {
            c.deemph_mode = DSD_NEO_DEEMPH_75;
        } else if (dsd_strcasecmp(deemph, "nfm") == 0) {
            c.deemph_mode = DSD_NEO_DEEMPH_NFM;
        }
    }

    /* Audio LPF */
    const char* alpf = getenv("DSD_NEO_AUDIO_LPF");
    c.audio_lpf_is_set = env_is_set(alpf);
    c.audio_lpf_disable = 0;
    c.audio_lpf_cutoff_hz = 0;
    if (c.audio_lpf_is_set) {
        if (dsd_strcasecmp(alpf, "off") == 0 || strcmp(alpf, "0") == 0) {
            c.audio_lpf_disable = 1;
        } else {
            int cutoff = atoi(alpf);
            if (cutoff > 0) {
                c.audio_lpf_cutoff_hz = cutoff;
            }
        }
    }

    /* MT (intra-block worker pool) */
    const char* mt = getenv("DSD_NEO_MT");
    c.mt_is_set = env_is_set(mt);
    c.mt_enable = (c.mt_is_set && mt[0] == '1') ? 1 : 0;

    /* Disable fs/4 capture shift */
    const char* dfs4 = getenv("DSD_NEO_DISABLE_FS4_SHIFT");
    c.fs4_shift_disable_is_set = env_is_set(dfs4);
    c.fs4_shift_disable = (c.fs4_shift_disable_is_set && dfs4[0] == '1') ? 1 : 0;

    /* Output clear/drain on retune */
    const char* clr = getenv("DSD_NEO_OUTPUT_CLEAR_ON_RETUNE");
    const char* dms = getenv("DSD_NEO_RETUNE_DRAIN_MS");
    c.output_clear_on_retune_is_set = env_is_set(clr);
    c.output_clear_on_retune = c.output_clear_on_retune_is_set ? (atoi(clr) != 0) : 0;
    c.retune_drain_ms_is_set = env_is_set(dms);
    c.retune_drain_ms = c.retune_drain_ms_is_set ? atoi(dms) : 50;

    /* TCP input reconnect backoff (ms) */
    const char* tb = getenv("DSD_NEO_TCPIN_BACKOFF_MS");
    c.tcpin_backoff_ms_is_set = 0;
    c.tcpin_backoff_ms = 300;
    if (env_is_set(tb)) {
        int v = atoi(tb);
        if (v >= 50 && v <= 5000) {
            c.tcpin_backoff_ms_is_set = 1;
            c.tcpin_backoff_ms = v;
        }
    }

    /* Symbol window freeze for A/B testing */
    const char* wf = getenv("DSD_NEO_WINDOW_FREEZE");
    c.window_freeze_is_set = env_is_set(wf);
    c.window_freeze = c.window_freeze_is_set ? (atoi(wf) != 0) : 0;

    /* Optional JSON emitter for P25 PDUs */
    const char* pj = getenv("DSD_NEO_PDU_JSON");
    c.pdu_json_is_set = env_is_set(pj);
    c.pdu_json_enable = c.pdu_json_is_set ? (atoi(pj) != 0) : 0;

    /* Optional SNR-based digital squelch threshold (dB) */
    const char* snrsql = getenv("DSD_NEO_SNR_SQL_DB");
    c.snr_sql_is_set = env_is_set(snrsql);
    c.snr_sql_db = c.snr_sql_is_set ? atoi(snrsql) : 0;

    /* FM/C4FM amplitude AGC (pre-discriminator) */
    const char* fm_agc = getenv("DSD_NEO_FM_AGC");
    c.fm_agc_is_set = env_is_set(fm_agc);
    c.fm_agc_enable = c.fm_agc_is_set ? (atoi(fm_agc) != 0) : 0; /* default off unless overridden */

    const char* fm_tgt = getenv("DSD_NEO_FM_AGC_TARGET");
    c.fm_agc_target_is_set = env_is_set(fm_tgt);
    c.fm_agc_target_rms = c.fm_agc_target_is_set ? (float)atof(fm_tgt) : 0.30f;

    const char* fm_min = getenv("DSD_NEO_FM_AGC_MIN");
    c.fm_agc_min_is_set = env_is_set(fm_min);
    c.fm_agc_min_rms = c.fm_agc_min_is_set ? (float)atof(fm_min) : 0.06f;

    const char* fm_au = getenv("DSD_NEO_FM_AGC_ALPHA_UP");
    c.fm_agc_alpha_up_is_set = env_is_set(fm_au);
    c.fm_agc_alpha_up = c.fm_agc_alpha_up_is_set ? (float)atof(fm_au) : 0.25f; /* ~0.25 */

    const char* fm_ad = getenv("DSD_NEO_FM_AGC_ALPHA_DOWN");
    c.fm_agc_alpha_down_is_set = env_is_set(fm_ad);
    c.fm_agc_alpha_down = c.fm_agc_alpha_down_is_set ? (float)atof(fm_ad) : 0.75f; /* ~0.75 */

    /* FM constant-envelope limiter */
    const char* fml = getenv("DSD_NEO_FM_LIMITER");
    c.fm_limiter_is_set = env_is_set(fml);
    c.fm_limiter_enable = c.fm_limiter_is_set ? (atoi(fml) != 0) : 0;

    /* Complex DC blocker */
    const char* dcb = getenv("DSD_NEO_IQ_DC_BLOCK");
    const char* dck = getenv("DSD_NEO_IQ_DC_SHIFT");
    c.iq_dc_block_is_set = env_is_set(dcb);
    c.iq_dc_block_enable = c.iq_dc_block_is_set ? (atoi(dcb) != 0) : 0;
    c.iq_dc_shift_is_set = env_is_set(dck);
    c.iq_dc_shift = c.iq_dc_shift_is_set ? atoi(dck) : 11;

    /* Channel complex low-pass on RTL baseband (post-HB).
       Default: off for digital voice modes at 24 kHz; may be enabled via env. */
    const char* clpf = getenv("DSD_NEO_CHANNEL_LPF");
    c.channel_lpf_is_set = env_is_set(clpf);
    c.channel_lpf_enable = c.channel_lpf_is_set ? atoi(clpf) : 0;

    g_config = c;
    g_config_inited = 1;
}

/**
 * @brief Get immutable pointer to the current runtime configuration, or NULL if
 * initialization has not been performed.
 *
 * @return Pointer to config or NULL.
 */
const dsdneoRuntimeConfig*
dsd_neo_get_config(void) {
    return g_config_inited ? &g_config : NULL;
}

extern "C" void
dsd_apply_runtime_config_to_opts(const dsdneoRuntimeConfig* cfg, dsd_opts* opts, dsd_state* state) {
    (void)state;
    if (!cfg || !opts) {
        return;
    }

    if (cfg->dmr_t3_heur_is_set) {
        opts->dmr_t3_heuristic_fill = (uint8_t)(cfg->dmr_t3_heur_enable ? 1 : 0);
    }
}

extern "C" const char*
dsd_neo_env_get(const char* name) {
    return name ? getenv(name) : NULL;
}

/* Runtime control for C4FM clock assist (0=off, 1=EL, 2=MM). */
extern "C" void
dsd_neo_set_c4fm_clk(int mode) {
    if (!g_config_inited) {
        g_config = dsdneoRuntimeConfig{};
        g_config_inited = 1;
    }
    if (mode >= 0) {
        if (mode > 2) {
            mode = 0;
        }
        g_config.c4fm_clk_is_set = 1;
        g_config.c4fm_clk_mode = mode;
    }
}

extern "C" int
dsd_neo_get_c4fm_clk(void) {
    if (!g_config_inited) {
        return 0;
    }
    return g_config.c4fm_clk_is_set ? g_config.c4fm_clk_mode : 0;
}

extern "C" void
dsd_neo_set_c4fm_clk_sync(int enable) {
    if (!g_config_inited) {
        g_config = dsdneoRuntimeConfig{};
        g_config_inited = 1;
    }
    g_config.c4fm_clk_sync_is_set = 1;
    g_config.c4fm_clk_sync = enable ? 1 : 0;
}

extern "C" int
dsd_neo_get_c4fm_clk_sync(void) {
    if (!g_config_inited) {
        return 0;
    }
    return g_config.c4fm_clk_sync_is_set ? (g_config.c4fm_clk_sync ? 1 : 0) : 0;
}
