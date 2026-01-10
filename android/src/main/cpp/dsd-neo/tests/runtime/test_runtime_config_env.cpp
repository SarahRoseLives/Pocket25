// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Runtime config environment parsing smoke tests.
 *
 * Validates that selected env-driven knobs are parsed into the typed runtime
 * config with expected defaults and range checks.
 */

#include <dsd-neo/runtime/config.h>

#include <dsd-neo/core/opts.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

#define setenv   dsd_test_setenv
#define unsetenv dsd_test_unsetenv

static int
expect_int_eq(int actual, int expected, int rc, const char* name) {
    if (actual != expected) {
        fprintf(stderr, "FAIL(%d): %s expected %d got %d\n", rc, name, expected, actual);
        return rc;
    }
    return 0;
}

static int
expect_long_eq(long actual, long expected, int rc, const char* name) {
    if (actual != expected) {
        fprintf(stderr, "FAIL(%d): %s expected %ld got %ld\n", rc, name, expected, actual);
        return rc;
    }
    return 0;
}

static int
expect_double_close(double actual, double expected, double tol, int rc, const char* name) {
    if (fabs(actual - expected) > tol) {
        fprintf(stderr, "FAIL(%d): %s expected %.9g got %.9g (tol=%.9g)\n", rc, name, expected, actual, tol);
        return rc;
    }
    return 0;
}

static int
expect_str_eq(const char* actual, const char* expected, int rc, const char* name) {
    if (!actual || !expected || strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL(%d): %s expected '%s' got '%s'\n", rc, name, expected ? expected : "(null)",
                actual ? actual : "(null)");
        return rc;
    }
    return 0;
}

static int
expect(int cond, int rc, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL(%d): %s\n", rc, msg);
        return rc;
    }
    return 0;
}

static void
unset_all_runtime_env(void) {
    const char* vars[] = {
        "DSD_NEO_AUDIO_LPF",
        "DSD_NEO_AUTO_PPM",
        "DSD_NEO_AUTO_PPM_FREEZE",
        "DSD_NEO_AUTO_PPM_PWR_DB",
        "DSD_NEO_AUTO_PPM_SNR_DB",
        "DSD_NEO_AUTO_PPM_ZEROLOCK_HZ",
        "DSD_NEO_AUTO_PPM_ZEROLOCK_PPM",
        "DSD_NEO_C4FM_CLK",
        "DSD_NEO_C4FM_CLK_SYNC",
        "DSD_NEO_CACHE_DIR",
        "DSD_NEO_CC_CACHE",
        "DSD_NEO_CHANNEL_LPF",
        "DSD_NEO_COMBINE_ROT",
        "DSD_NEO_CONFIG",
        "DSD_NEO_COSTAS_BW",
        "DSD_NEO_COSTAS_DAMPING",
        "DSD_NEO_CPU_DEMOD",
        "DSD_NEO_CPU_DONGLE",
        "DSD_NEO_CPU_USB",
        "DSD_NEO_CQPSK",
        "DSD_NEO_CQPSK_SYNC_INV",
        "DSD_NEO_CQPSK_SYNC_NEG",
        "DSD_NEO_DEBUG_CQPSK",
        "DSD_NEO_DEBUG_SYNC",
        "DSD_NEO_DEEMPH",
        "DSD_NEO_DISABLE_FS4_SHIFT",
        "DSD_NEO_DMR_GRANT_TIMEOUT",
        "DSD_NEO_DMR_HANGTIME",
        "DSD_NEO_DMR_T3_CALC_CSV",
        "DSD_NEO_DMR_T3_CC_FREQ",
        "DSD_NEO_DMR_T3_CC_LCN",
        "DSD_NEO_DMR_T3_HEUR",
        "DSD_NEO_DMR_T3_START_LCN",
        "DSD_NEO_DMR_T3_STEP_HZ",
        "DSD_NEO_FLL",
        "DSD_NEO_FLL_ALPHA",
        "DSD_NEO_FLL_BETA",
        "DSD_NEO_FLL_DEADBAND",
        "DSD_NEO_FLL_SLEW",
        "DSD_NEO_FM_AGC",
        "DSD_NEO_FM_AGC_ALPHA_DOWN",
        "DSD_NEO_FM_AGC_ALPHA_UP",
        "DSD_NEO_FM_AGC_MIN",
        "DSD_NEO_FM_AGC_TARGET",
        "DSD_NEO_FM_LIMITER",
        "DSD_NEO_FTZ_DAZ",
        "DSD_NEO_INPUT_VOLUME",
        "DSD_NEO_INPUT_WARN_DB",
        "DSD_NEO_IQ_DC_BLOCK",
        "DSD_NEO_IQ_DC_SHIFT",
        "DSD_NEO_MT",
        "DSD_NEO_NO_BOOTSTRAP",
        "DSD_NEO_OUTPUT_CLEAR_ON_RETUNE",
        "DSD_NEO_P25_CC_GRACE",
        "DSD_NEO_P25_FORCE_RELEASE_EXTRA",
        "DSD_NEO_P25_FORCE_RELEASE_MARGIN",
        "DSD_NEO_P25_GRANT_TIMEOUT",
        "DSD_NEO_P25_GRANT_VOICE_TO",
        "DSD_NEO_P25_HANGTIME",
        "DSD_NEO_P25_MAC_HOLD",
        "DSD_NEO_P25_MIN_FOLLOW_DWELL",
        "DSD_NEO_P25P1_ERR_HOLD_PCT",
        "DSD_NEO_P25P1_ERR_HOLD_S",
        "DSD_NEO_P25P1_SOFT_ERASURE_THRESH",
        "DSD_NEO_P25P2_SOFT_ERASURE_THRESH",
        "DSD_NEO_P25_RETUNE_BACKOFF",
        "DSD_NEO_P25_RING_HOLD",
        "DSD_NEO_P25_VC_GRACE",
        "DSD_NEO_P25_VOICE_HOLD",
        "DSD_NEO_P25_WD_MS",
        "DSD_NEO_PDU_JSON",
        "DSD_NEO_RESAMP",
        "DSD_NEO_RETUNE_DRAIN_MS",
        "DSD_NEO_RIGCTL_RCVTIMEO",
        "DSD_NEO_RTL_AGC",
        "DSD_NEO_RTL_DIRECT",
        "DSD_NEO_RTL_IF_GAINS",
        "DSD_NEO_RTL_OFFSET_TUNING",
        "DSD_NEO_RTL_TESTMODE",
        "DSD_NEO_RTL_XTAL_HZ",
        "DSD_NEO_RT_PRIO_DEMOD",
        "DSD_NEO_RT_PRIO_DONGLE",
        "DSD_NEO_RT_PRIO_USB",
        "DSD_NEO_RT_SCHED",
        "DSD_NEO_SNR_SQL_DB",
        "DSD_NEO_SYNC_WARMSTART",
        "DSD_NEO_TCP_AUTOTUNE",
        "DSD_NEO_TCP_BUFSZ",
        "DSD_NEO_TCPIN_BACKOFF_MS",
        "DSD_NEO_TCP_MAX_TIMEOUTS",
        "DSD_NEO_TCP_PREBUF_MS",
        "DSD_NEO_TCP_RCVBUF",
        "DSD_NEO_TCP_RCVTIMEO",
        "DSD_NEO_TCP_STATS",
        "DSD_NEO_TCP_WAITALL",
        "DSD_NEO_TED",
        "DSD_NEO_TED_FORCE",
        "DSD_NEO_TED_GAIN",
        "DSD_NEO_TUNER_AUTOGAIN",
        "DSD_NEO_TUNER_AUTOGAIN_INBAND_RATIO",
        "DSD_NEO_TUNER_AUTOGAIN_PROBE_MS",
        "DSD_NEO_TUNER_AUTOGAIN_SEED_DB",
        "DSD_NEO_TUNER_AUTOGAIN_SPEC_SNR_DB",
        "DSD_NEO_TUNER_AUTOGAIN_UP_PERSIST",
        "DSD_NEO_TUNER_AUTOGAIN_UP_STEP_DB",
        "DSD_NEO_TUNER_BW_HZ",
        "DSD_NEO_TUNER_XTAL_HZ",
        "DSD_NEO_UPSAMPLE_FP",
        "DSD_NEO_WINDOW_FREEZE",
        NULL,
    };

    for (int i = 0; vars[i] != NULL; i++) {
        unsetenv(vars[i]);
    }
}

static int
expect_backoff(int is_set, int value, int rc_base) {
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = 0;
    rc = expect(cfg != NULL, rc_base + 0, "dsd_neo_get_config returned NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect(cfg->tcpin_backoff_ms_is_set == is_set, rc_base + 1, "tcpin_backoff_ms_is_set mismatch");
    if (rc != 0) {
        return rc;
    }
    rc = expect(cfg->tcpin_backoff_ms == value, rc_base + 2, "tcpin_backoff_ms mismatch");
    if (rc != 0) {
        return rc;
    }
    return 0;
}

static int
test_tcp_rcvtimeo_ms(void) {
    unsetenv("DSD_NEO_TCP_RCVTIMEO");
    dsd_neo_config_init(NULL);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 100, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_rcvtimeo_is_set, 0, 101, "tcp_rcvtimeo_is_set (default)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_rcvtimeo_ms, 2000, 102, "tcp_rcvtimeo_ms (default)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TCP_RCVTIMEO", "100", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->tcp_rcvtimeo_is_set, 1, 110, "tcp_rcvtimeo_is_set (100)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_rcvtimeo_ms, 100, 111, "tcp_rcvtimeo_ms (100)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TCP_RCVTIMEO", "99", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->tcp_rcvtimeo_is_set, 0, 120, "tcp_rcvtimeo_is_set (99)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_rcvtimeo_ms, 2000, 121, "tcp_rcvtimeo_ms (99)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_TCP_RCVTIMEO");
    return 0;
}

static int
test_tcp_rcvbuf_bytes(void) {
    unsetenv("DSD_NEO_TCP_RCVBUF");
    dsd_neo_config_init(NULL);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 200, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_rcvbuf_is_set, 0, 201, "tcp_rcvbuf_is_set (default)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_rcvbuf_bytes, 4 * 1024 * 1024, 202, "tcp_rcvbuf_bytes (default)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TCP_RCVBUF", "12345", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->tcp_rcvbuf_is_set, 1, 210, "tcp_rcvbuf_is_set (12345)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_rcvbuf_bytes, 12345, 211, "tcp_rcvbuf_bytes (12345)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_TCP_RCVBUF");
    return 0;
}

static int
test_tcp_autotune_enable(void) {
    unsetenv("DSD_NEO_TCP_AUTOTUNE");
    dsd_neo_config_init(NULL);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 300, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_autotune_is_set, 0, 301, "tcp_autotune_is_set (default)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_autotune_enable, 0, 302, "tcp_autotune_enable (default)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TCP_AUTOTUNE", "1", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->tcp_autotune_is_set, 1, 310, "tcp_autotune_is_set (1)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_autotune_enable, 1, 311, "tcp_autotune_enable (1)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TCP_AUTOTUNE", "0", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->tcp_autotune_is_set, 1, 320, "tcp_autotune_is_set (0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_autotune_enable, 0, 321, "tcp_autotune_enable (0)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_TCP_AUTOTUNE");
    return 0;
}

static int
test_rtl_direct_mode(void) {
    unsetenv("DSD_NEO_RTL_DIRECT");
    dsd_neo_config_init(NULL);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 400, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_direct_is_set, 0, 401, "rtl_direct_is_set (default)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_RTL_DIRECT", "I", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->rtl_direct_is_set, 1, 410, "rtl_direct_is_set (I)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_direct_mode, 1, 411, "rtl_direct_mode (I)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_RTL_DIRECT", "Q", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->rtl_direct_is_set, 1, 420, "rtl_direct_is_set (Q)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_direct_mode, 2, 421, "rtl_direct_mode (Q)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_RTL_DIRECT", "bogus", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->rtl_direct_is_set, 1, 430, "rtl_direct_is_set (bogus)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_direct_mode, 0, 431, "rtl_direct_mode (bogus)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_RTL_DIRECT");
    return 0;
}

static int
test_tuner_bw_hz(void) {
    unsetenv("DSD_NEO_TUNER_BW_HZ");
    dsd_neo_config_init(NULL);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 500, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_bw_hz_is_set, 0, 501, "tuner_bw_hz_is_set (default)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TUNER_BW_HZ", "auto", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->tuner_bw_hz_is_set, 1, 510, "tuner_bw_hz_is_set (auto)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_bw_hz, 0, 511, "tuner_bw_hz (auto)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TUNER_BW_HZ", "20000000", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->tuner_bw_hz_is_set, 1, 520, "tuner_bw_hz_is_set (20000000)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_bw_hz, 20000000, 521, "tuner_bw_hz (20000000)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TUNER_BW_HZ", "20000001", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->tuner_bw_hz_is_set, 0, 530, "tuner_bw_hz_is_set (20000001)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_TUNER_BW_HZ");
    return 0;
}

static int
test_p25_watchdog_ms(void) {
    unsetenv("DSD_NEO_P25_WD_MS");
    dsd_neo_config_init(NULL);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 600, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_wd_ms_is_set, 0, 601, "p25_wd_ms_is_set (default)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_wd_ms, 0, 602, "p25_wd_ms (default)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_P25_WD_MS", "20", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->p25_wd_ms_is_set, 1, 610, "p25_wd_ms_is_set (20)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_wd_ms, 20, 611, "p25_wd_ms (20)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_P25_WD_MS", "19", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->p25_wd_ms_is_set, 0, 620, "p25_wd_ms_is_set (19)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_wd_ms, 0, 621, "p25_wd_ms (19)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_P25_WD_MS");
    return 0;
}

static int
test_dmr_t3_heur_apply(void) {
    dsd_opts opts;
    memset(&opts, 0, sizeof opts);
    opts.dmr_t3_heuristic_fill = 7; /* sentinel */

    unsetenv("DSD_NEO_DMR_T3_HEUR");
    dsd_neo_config_init(NULL);
    dsd_apply_runtime_config_to_opts(dsd_neo_get_config(), &opts, NULL);
    int rc = expect_int_eq(opts.dmr_t3_heuristic_fill, 7, 700, "dmr_t3_heuristic_fill unchanged when unset");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_DMR_T3_HEUR", "1", 1);
    dsd_neo_config_init(NULL);
    dsd_apply_runtime_config_to_opts(dsd_neo_get_config(), &opts, NULL);
    rc = expect_int_eq(opts.dmr_t3_heuristic_fill, 1, 710, "dmr_t3_heuristic_fill (1)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_DMR_T3_HEUR", "0", 1);
    dsd_neo_config_init(NULL);
    dsd_apply_runtime_config_to_opts(dsd_neo_get_config(), &opts, NULL);
    rc = expect_int_eq(opts.dmr_t3_heuristic_fill, 0, 720, "dmr_t3_heuristic_fill (0)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_DMR_T3_HEUR");
    return 0;
}

static int
test_cache_dir_default(void) {
    const char* prev_home = dsd_neo_env_get("HOME");
    char saved_home[1024];
    saved_home[0] = '\0';
    if (prev_home && *prev_home) {
        snprintf(saved_home, sizeof saved_home, "%s", prev_home);
        saved_home[sizeof saved_home - 1] = '\0';
    }

    setenv("HOME", "/tmp/dsdneo_test_home", 1);
    unsetenv("DSD_NEO_CACHE_DIR");
    dsd_neo_config_init(NULL);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 800, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_str_eq(cfg->cache_dir, "/tmp/dsdneo_test_home/.cache/dsd-neo", 801, "cache_dir default");
    if (rc != 0) {
        return rc;
    }

    if (saved_home[0]) {
        setenv("HOME", saved_home, 1);
    } else {
        unsetenv("HOME");
    }
    unsetenv("DSD_NEO_CACHE_DIR");
    return 0;
}

static int
test_cache_dir_override(void) {
    unsetenv("DSD_NEO_CACHE_DIR");
    setenv("DSD_NEO_CACHE_DIR", "/tmp/dsdneo_cache_override", 1);
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 900, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cache_dir_is_set, 1, 901, "cache_dir_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_str_eq(cfg->cache_dir, "/tmp/dsdneo_cache_override", 902, "cache_dir override");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_CACHE_DIR");
    return 0;
}

static int
test_config_path_env(void) {
    unsetenv("DSD_NEO_CONFIG");
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 910, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->config_path_is_set, 0, 911, "config_path_is_set (default)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_CONFIG", "/tmp/dsdneo_test.ini", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->config_path_is_set, 1, 912, "config_path_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_str_eq(cfg->config_path, "/tmp/dsdneo_test.ini", 913, "config_path");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_CONFIG");
    return 0;
}

static int
test_cc_cache_env(void) {
    unsetenv("DSD_NEO_CC_CACHE");
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 920, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cc_cache_is_set, 0, 921, "cc_cache_is_set (default)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cc_cache_enable, 1, 922, "cc_cache_enable (default)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_CC_CACHE", "0", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->cc_cache_is_set, 1, 930, "cc_cache_is_set (0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cc_cache_enable, 0, 931, "cc_cache_enable (0)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_CC_CACHE", "1", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->cc_cache_is_set, 1, 940, "cc_cache_is_set (1)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cc_cache_enable, 1, 941, "cc_cache_enable (1)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_CC_CACHE");
    return 0;
}

static int
test_rt_sched_affinity_env(void) {
    setenv("DSD_NEO_RT_SCHED", "1", 1);
    setenv("DSD_NEO_RT_PRIO_USB", "80", 1);
    setenv("DSD_NEO_RT_PRIO_DONGLE", "81", 1);
    setenv("DSD_NEO_RT_PRIO_DEMOD", "82", 1);
    setenv("DSD_NEO_CPU_USB", "1", 1);
    setenv("DSD_NEO_CPU_DONGLE", "2", 1);
    setenv("DSD_NEO_CPU_DEMOD", "3", 1);
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 950, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rt_sched_is_set, 1, 951, "rt_sched_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rt_sched_enable, 1, 952, "rt_sched_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rt_prio_usb_is_set, 1, 953, "rt_prio_usb_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rt_prio_usb, 80, 954, "rt_prio_usb");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rt_prio_dongle_is_set, 1, 955, "rt_prio_dongle_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rt_prio_dongle, 81, 956, "rt_prio_dongle");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rt_prio_demod_is_set, 1, 957, "rt_prio_demod_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rt_prio_demod, 82, 958, "rt_prio_demod");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cpu_usb_is_set, 1, 959, "cpu_usb_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cpu_usb, 1, 960, "cpu_usb");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cpu_dongle_is_set, 1, 961, "cpu_dongle_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cpu_dongle, 2, 962, "cpu_dongle");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cpu_demod_is_set, 1, 963, "cpu_demod_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cpu_demod, 3, 964, "cpu_demod");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_RT_SCHED", "0", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->rt_sched_enable, 0, 970, "rt_sched_enable (0)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_RT_PRIO_USB", "0", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->rt_prio_usb_is_set, 0, 971, "rt_prio_usb_is_set (0)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_CPU_USB", "-1", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->cpu_usb_is_set, 0, 972, "cpu_usb_is_set (-1)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_RT_SCHED");
    unsetenv("DSD_NEO_RT_PRIO_USB");
    unsetenv("DSD_NEO_RT_PRIO_DONGLE");
    unsetenv("DSD_NEO_RT_PRIO_DEMOD");
    unsetenv("DSD_NEO_CPU_USB");
    unsetenv("DSD_NEO_CPU_DONGLE");
    unsetenv("DSD_NEO_CPU_DEMOD");
    return 0;
}

static int
test_bootstrap_debug_env(void) {
    setenv("DSD_NEO_FTZ_DAZ", "1", 1);
    setenv("DSD_NEO_NO_BOOTSTRAP", "1", 1);
    setenv("DSD_NEO_DEBUG_SYNC", "1", 1);
    setenv("DSD_NEO_DEBUG_CQPSK", "1", 1);
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 980, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->ftz_daz_is_set, 1, 981, "ftz_daz_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->ftz_daz_enable, 1, 982, "ftz_daz_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->no_bootstrap_is_set, 1, 983, "no_bootstrap_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->no_bootstrap_enable, 1, 984, "no_bootstrap_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->debug_sync_is_set, 1, 985, "debug_sync_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->debug_sync_enable, 1, 986, "debug_sync_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->debug_cqpsk_is_set, 1, 987, "debug_cqpsk_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->debug_cqpsk_enable, 1, 988, "debug_cqpsk_enable");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_FTZ_DAZ", "0", 1);
    setenv("DSD_NEO_NO_BOOTSTRAP", "0", 1);
    setenv("DSD_NEO_DEBUG_SYNC", "0", 1);
    setenv("DSD_NEO_DEBUG_CQPSK", "0", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->ftz_daz_enable, 0, 990, "ftz_daz_enable (0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->no_bootstrap_enable, 0, 991, "no_bootstrap_enable (0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->debug_sync_enable, 0, 992, "debug_sync_enable (0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->debug_cqpsk_enable, 0, 993, "debug_cqpsk_enable (0)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_FTZ_DAZ");
    unsetenv("DSD_NEO_NO_BOOTSTRAP");
    unsetenv("DSD_NEO_DEBUG_SYNC");
    unsetenv("DSD_NEO_DEBUG_CQPSK");
    return 0;
}

static int
test_cqpsk_sync_env(void) {
    unsetenv("DSD_NEO_CQPSK");
    dsd_neo_config_init(NULL);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 1000, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cqpsk_is_set, 0, 1001, "cqpsk_is_set (default)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_CQPSK", "1", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->cqpsk_is_set, 1, 1010, "cqpsk_is_set (1)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cqpsk_enable, 1, 1011, "cqpsk_enable (1)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_CQPSK", "0", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->cqpsk_is_set, 1, 1020, "cqpsk_is_set (0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cqpsk_enable, 0, 1021, "cqpsk_enable (0)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_CQPSK", "bogus", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->cqpsk_is_set, 0, 1030, "cqpsk_is_set (bogus)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_CQPSK_SYNC_INV", "1", 1);
    setenv("DSD_NEO_CQPSK_SYNC_NEG", "0", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->cqpsk_sync_inv_is_set, 1, 1040, "cqpsk_sync_inv_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cqpsk_sync_inv, 1, 1041, "cqpsk_sync_inv");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cqpsk_sync_neg_is_set, 1, 1042, "cqpsk_sync_neg_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->cqpsk_sync_neg, 0, 1043, "cqpsk_sync_neg");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_CQPSK");
    unsetenv("DSD_NEO_CQPSK_SYNC_INV");
    unsetenv("DSD_NEO_CQPSK_SYNC_NEG");
    return 0;
}

static int
test_sync_warmstart_env(void) {
    unsetenv("DSD_NEO_SYNC_WARMSTART");
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 1050, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->sync_warmstart_is_set, 0, 1051, "sync_warmstart_is_set (default)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->sync_warmstart_enable, 1, 1052, "sync_warmstart_enable (default)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_SYNC_WARMSTART", "0", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->sync_warmstart_is_set, 1, 1060, "sync_warmstart_is_set (0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->sync_warmstart_enable, 0, 1061, "sync_warmstart_enable (0)");
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_SYNC_WARMSTART", "1", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->sync_warmstart_is_set, 1, 1070, "sync_warmstart_is_set (1)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->sync_warmstart_enable, 1, 1071, "sync_warmstart_enable (1)");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_SYNC_WARMSTART");
    return 0;
}

static int
test_protocol_env_knobs(void) {
    setenv("DSD_NEO_DMR_HANGTIME", "3.5", 1);
    setenv("DSD_NEO_DMR_GRANT_TIMEOUT", "5.5", 1);

    setenv("DSD_NEO_P25_HANGTIME", "3.0", 1);
    setenv("DSD_NEO_P25_GRANT_TIMEOUT", "4.0", 1);
    setenv("DSD_NEO_P25_CC_GRACE", "6.0", 1);
    setenv("DSD_NEO_P25_VC_GRACE", "1.0", 1);
    setenv("DSD_NEO_P25_RING_HOLD", "1.5", 1);
    setenv("DSD_NEO_P25_MAC_HOLD", "2.5", 1);
    setenv("DSD_NEO_P25_VOICE_HOLD", "1.0", 1);

    setenv("DSD_NEO_P25_MIN_FOLLOW_DWELL", "1.0", 1);
    setenv("DSD_NEO_P25_GRANT_VOICE_TO", "2.0", 1);
    setenv("DSD_NEO_P25_RETUNE_BACKOFF", "3.0", 1);
    setenv("DSD_NEO_P25_FORCE_RELEASE_EXTRA", "4.0", 1);
    setenv("DSD_NEO_P25_FORCE_RELEASE_MARGIN", "5.0", 1);
    setenv("DSD_NEO_P25P1_ERR_HOLD_PCT", "6.0", 1);
    setenv("DSD_NEO_P25P1_ERR_HOLD_S", "7.0", 1);

    setenv("DSD_NEO_P25P1_SOFT_ERASURE_THRESH", "100", 1);
    setenv("DSD_NEO_P25P2_SOFT_ERASURE_THRESH", "101", 1);

    setenv("DSD_NEO_INPUT_VOLUME", "2", 1);
    setenv("DSD_NEO_INPUT_WARN_DB", "-10.0", 1);

    dsd_neo_config_init(NULL);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 1100, "cfg NULL");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->dmr_hangtime_is_set, 1, 1101, "dmr_hangtime_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->dmr_hangtime_s, 3.5, 1e-9, 1102, "dmr_hangtime_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->dmr_grant_timeout_is_set, 1, 1103, "dmr_grant_timeout_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->dmr_grant_timeout_s, 5.5, 1e-9, 1104, "dmr_grant_timeout_s");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->p25_hangtime_is_set, 1, 1110, "p25_hangtime_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_hangtime_s, 3.0, 1e-9, 1111, "p25_hangtime_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_grant_timeout_is_set, 1, 1112, "p25_grant_timeout_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_grant_timeout_s, 4.0, 1e-9, 1113, "p25_grant_timeout_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_cc_grace_is_set, 1, 1114, "p25_cc_grace_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_cc_grace_s, 6.0, 1e-9, 1115, "p25_cc_grace_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_vc_grace_is_set, 1, 1116, "p25_vc_grace_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_vc_grace_s, 1.0, 1e-9, 1117, "p25_vc_grace_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_ring_hold_is_set, 1, 1118, "p25_ring_hold_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_ring_hold_s, 1.5, 1e-9, 1119, "p25_ring_hold_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_mac_hold_is_set, 1, 1120, "p25_mac_hold_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_mac_hold_s, 2.5, 1e-9, 1121, "p25_mac_hold_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_voice_hold_is_set, 1, 1122, "p25_voice_hold_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_voice_hold_s, 1.0, 1e-9, 1123, "p25_voice_hold_s");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->p25_min_follow_dwell_is_set, 1, 1130, "p25_min_follow_dwell_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_min_follow_dwell_s, 1.0, 1e-9, 1131, "p25_min_follow_dwell_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_grant_voice_to_is_set, 1, 1132, "p25_grant_voice_to_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_grant_voice_to_s, 2.0, 1e-9, 1133, "p25_grant_voice_to_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_retune_backoff_is_set, 1, 1134, "p25_retune_backoff_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_retune_backoff_s, 3.0, 1e-9, 1135, "p25_retune_backoff_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_force_release_extra_is_set, 1, 1136, "p25_force_release_extra_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_force_release_extra_s, 4.0, 1e-9, 1137, "p25_force_release_extra_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25_force_release_margin_is_set, 1, 1138, "p25_force_release_margin_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25_force_release_margin_s, 5.0, 1e-9, 1139, "p25_force_release_margin_s");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25p1_err_hold_pct_is_set, 1, 1140, "p25p1_err_hold_pct_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25p1_err_hold_pct, 6.0, 1e-9, 1141, "p25p1_err_hold_pct");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25p1_err_hold_s_is_set, 1, 1142, "p25p1_err_hold_s_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->p25p1_err_hold_s, 7.0, 1e-9, 1143, "p25p1_err_hold_s");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->p25p1_soft_erasure_thresh_is_set, 1, 1150, "p25p1_soft_erasure_thresh_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25p1_soft_erasure_thresh, 100, 1151, "p25p1_soft_erasure_thresh");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25p2_soft_erasure_thresh_is_set, 1, 1152, "p25p2_soft_erasure_thresh_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25p2_soft_erasure_thresh, 101, 1153, "p25p2_soft_erasure_thresh");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->input_volume_is_set, 1, 1160, "input_volume_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->input_volume_multiplier, 2, 1161, "input_volume_multiplier");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->input_warn_db_is_set, 1, 1162, "input_warn_db_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->input_warn_db, -10.0, 1e-9, 1163, "input_warn_db");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_DMR_HANGTIME");
    unsetenv("DSD_NEO_DMR_GRANT_TIMEOUT");

    unsetenv("DSD_NEO_P25_HANGTIME");
    unsetenv("DSD_NEO_P25_GRANT_TIMEOUT");
    unsetenv("DSD_NEO_P25_CC_GRACE");
    unsetenv("DSD_NEO_P25_VC_GRACE");
    unsetenv("DSD_NEO_P25_RING_HOLD");
    unsetenv("DSD_NEO_P25_MAC_HOLD");
    unsetenv("DSD_NEO_P25_VOICE_HOLD");

    unsetenv("DSD_NEO_P25_MIN_FOLLOW_DWELL");
    unsetenv("DSD_NEO_P25_GRANT_VOICE_TO");
    unsetenv("DSD_NEO_P25_RETUNE_BACKOFF");
    unsetenv("DSD_NEO_P25_FORCE_RELEASE_EXTRA");
    unsetenv("DSD_NEO_P25_FORCE_RELEASE_MARGIN");
    unsetenv("DSD_NEO_P25P1_ERR_HOLD_PCT");
    unsetenv("DSD_NEO_P25P1_ERR_HOLD_S");

    unsetenv("DSD_NEO_P25P1_SOFT_ERASURE_THRESH");
    unsetenv("DSD_NEO_P25P2_SOFT_ERASURE_THRESH");

    unsetenv("DSD_NEO_INPUT_VOLUME");
    unsetenv("DSD_NEO_INPUT_WARN_DB");

    /* Invalid ranges are ignored (retain defaults) */
    setenv("DSD_NEO_DMR_HANGTIME", "10.1", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->dmr_hangtime_is_set, 0, 1200, "dmr_hangtime_is_set (10.1)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->dmr_hangtime_s, 2.0, 1e-9, 1201, "dmr_hangtime_s default");
    if (rc != 0) {
        return rc;
    }
    unsetenv("DSD_NEO_DMR_HANGTIME");

    setenv("DSD_NEO_INPUT_VOLUME", "17", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->input_volume_is_set, 0, 1202, "input_volume_is_set (17)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->input_volume_multiplier, 1, 1203, "input_volume_multiplier default");
    if (rc != 0) {
        return rc;
    }
    unsetenv("DSD_NEO_INPUT_VOLUME");

    setenv("DSD_NEO_P25P1_SOFT_ERASURE_THRESH", "256", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->p25p1_soft_erasure_thresh_is_set, 0, 1204, "p25p1_soft_erasure_thresh_is_set (256)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->p25p1_soft_erasure_thresh, 64, 1205, "p25p1_soft_erasure_thresh default");
    if (rc != 0) {
        return rc;
    }
    unsetenv("DSD_NEO_P25P1_SOFT_ERASURE_THRESH");

    return 0;
}

static int
test_dmr_t3_tools_env(void) {
    setenv("DSD_NEO_DMR_T3_CALC_CSV", "/tmp/dsdneo_t3.csv", 1);
    setenv("DSD_NEO_DMR_T3_STEP_HZ", "12500", 1);
    setenv("DSD_NEO_DMR_T3_CC_LCN", "10", 1);
    setenv("DSD_NEO_DMR_T3_START_LCN", "11", 1);
    setenv("DSD_NEO_DMR_T3_CC_FREQ", "851.0", 1);
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 1250, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->dmr_t3_calc_csv_is_set, 1, 1251, "dmr_t3_calc_csv_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_str_eq(cfg->dmr_t3_calc_csv, "/tmp/dsdneo_t3.csv", 1252, "dmr_t3_calc_csv");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->dmr_t3_step_hz_is_set, 1, 1253, "dmr_t3_step_hz_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_long_eq(cfg->dmr_t3_step_hz, 12500L, 1254, "dmr_t3_step_hz");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->dmr_t3_cc_lcn_is_set, 1, 1255, "dmr_t3_cc_lcn_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_long_eq(cfg->dmr_t3_cc_lcn, 10L, 1256, "dmr_t3_cc_lcn");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->dmr_t3_start_lcn_is_set, 1, 1257, "dmr_t3_start_lcn_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_long_eq(cfg->dmr_t3_start_lcn, 11L, 1258, "dmr_t3_start_lcn");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->dmr_t3_cc_freq_is_set, 1, 1259, "dmr_t3_cc_freq_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_long_eq(cfg->dmr_t3_cc_freq_hz, 851000000L, 1260, "dmr_t3_cc_freq_hz");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_DMR_T3_CALC_CSV");
    unsetenv("DSD_NEO_DMR_T3_STEP_HZ");
    unsetenv("DSD_NEO_DMR_T3_CC_LCN");
    unsetenv("DSD_NEO_DMR_T3_START_LCN");
    unsetenv("DSD_NEO_DMR_T3_CC_FREQ");

    setenv("DSD_NEO_DMR_T3_STEP_HZ", "0", 1);
    setenv("DSD_NEO_DMR_T3_CC_FREQ", "0", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->dmr_t3_step_hz_is_set, 0, 1270, "dmr_t3_step_hz_is_set (0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_long_eq(cfg->dmr_t3_step_hz, 0L, 1271, "dmr_t3_step_hz default");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->dmr_t3_cc_freq_is_set, 0, 1272, "dmr_t3_cc_freq_is_set (0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_long_eq(cfg->dmr_t3_cc_freq_hz, 0L, 1273, "dmr_t3_cc_freq_hz default");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_DMR_T3_STEP_HZ");
    unsetenv("DSD_NEO_DMR_T3_CC_FREQ");
    return 0;
}

static int
test_tcp_misc_env(void) {
    setenv("DSD_NEO_TCP_BUFSZ", "8192", 1);
    setenv("DSD_NEO_TCP_WAITALL", "1", 1);
    setenv("DSD_NEO_TCP_STATS", "1", 1);
    setenv("DSD_NEO_TCP_MAX_TIMEOUTS", "7", 1);
    setenv("DSD_NEO_RIGCTL_RCVTIMEO", "2500", 1);
    setenv("DSD_NEO_TCP_PREBUF_MS", "500", 1);
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 1300, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_bufsz_is_set, 1, 1301, "tcp_bufsz_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_bufsz_bytes, 8192, 1302, "tcp_bufsz_bytes");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_waitall_is_set, 1, 1303, "tcp_waitall_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_waitall_enable, 1, 1304, "tcp_waitall_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_stats_is_set, 1, 1305, "tcp_stats_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_stats_enable, 1, 1306, "tcp_stats_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_max_timeouts_is_set, 1, 1307, "tcp_max_timeouts_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_max_timeouts, 7, 1308, "tcp_max_timeouts");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rigctl_rcvtimeo_is_set, 1, 1309, "rigctl_rcvtimeo_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rigctl_rcvtimeo_ms, 2500, 1310, "rigctl_rcvtimeo_ms");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_prebuf_ms_is_set, 1, 1311, "tcp_prebuf_ms_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_prebuf_ms, 500, 1312, "tcp_prebuf_ms");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_TCP_BUFSZ");
    unsetenv("DSD_NEO_TCP_WAITALL");
    unsetenv("DSD_NEO_TCP_STATS");
    unsetenv("DSD_NEO_TCP_MAX_TIMEOUTS");
    unsetenv("DSD_NEO_RIGCTL_RCVTIMEO");
    unsetenv("DSD_NEO_TCP_PREBUF_MS");

    setenv("DSD_NEO_TCP_BUFSZ", "4096", 1);
    setenv("DSD_NEO_TCP_MAX_TIMEOUTS", "0", 1);
    setenv("DSD_NEO_RIGCTL_RCVTIMEO", "99", 1);
    setenv("DSD_NEO_TCP_PREBUF_MS", "4", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->tcp_bufsz_is_set, 0, 1320, "tcp_bufsz_is_set (4096)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_max_timeouts_is_set, 0, 1321, "tcp_max_timeouts_is_set (0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_max_timeouts, 3, 1322, "tcp_max_timeouts default");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rigctl_rcvtimeo_is_set, 0, 1323, "rigctl_rcvtimeo_is_set (99)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rigctl_rcvtimeo_ms, 1500, 1324, "rigctl_rcvtimeo_ms default");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_prebuf_ms_is_set, 0, 1325, "tcp_prebuf_ms_is_set (4)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tcp_prebuf_ms, 1000, 1326, "tcp_prebuf_ms default");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_TCP_BUFSZ");
    unsetenv("DSD_NEO_TCP_MAX_TIMEOUTS");
    unsetenv("DSD_NEO_RIGCTL_RCVTIMEO");
    unsetenv("DSD_NEO_TCP_PREBUF_MS");
    return 0;
}

static int
test_rtl_misc_env(void) {
    setenv("DSD_NEO_RTL_AGC", "0", 1);
    setenv("DSD_NEO_RTL_OFFSET_TUNING", "0", 1);
    setenv("DSD_NEO_RTL_XTAL_HZ", "28800000", 1);
    setenv("DSD_NEO_TUNER_XTAL_HZ", "28800001", 1);
    setenv("DSD_NEO_RTL_TESTMODE", "1", 1);
    setenv("DSD_NEO_RTL_IF_GAINS", "20,30", 1);
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 1350, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_agc_is_set, 1, 1351, "rtl_agc_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_agc_enable, 0, 1352, "rtl_agc_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_offset_tuning_is_set, 1, 1353, "rtl_offset_tuning_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_offset_tuning_enable, 0, 1354, "rtl_offset_tuning_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_xtal_hz_is_set, 1, 1355, "rtl_xtal_hz_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_xtal_hz, 28800000, 1356, "rtl_xtal_hz");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_xtal_hz_is_set, 1, 1357, "tuner_xtal_hz_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_xtal_hz, 28800001, 1358, "tuner_xtal_hz");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_testmode_is_set, 1, 1359, "rtl_testmode_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_testmode_enable, 1, 1360, "rtl_testmode_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->rtl_if_gains_is_set, 1, 1361, "rtl_if_gains_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_str_eq(cfg->rtl_if_gains, "20,30", 1362, "rtl_if_gains");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_RTL_AGC");
    unsetenv("DSD_NEO_RTL_OFFSET_TUNING");
    unsetenv("DSD_NEO_RTL_XTAL_HZ");
    unsetenv("DSD_NEO_TUNER_XTAL_HZ");
    unsetenv("DSD_NEO_RTL_TESTMODE");
    unsetenv("DSD_NEO_RTL_IF_GAINS");

    setenv("DSD_NEO_RTL_XTAL_HZ", "0", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->rtl_xtal_hz_is_set, 0, 1370, "rtl_xtal_hz_is_set (0)");
    if (rc != 0) {
        return rc;
    }
    unsetenv("DSD_NEO_RTL_XTAL_HZ");
    return 0;
}

static int
test_tuner_autogain_env(void) {
    setenv("DSD_NEO_TUNER_AUTOGAIN", "1", 1);
    setenv("DSD_NEO_TUNER_AUTOGAIN_PROBE_MS", "5000", 1);
    setenv("DSD_NEO_TUNER_AUTOGAIN_SEED_DB", "20.0", 1);
    setenv("DSD_NEO_TUNER_AUTOGAIN_SPEC_SNR_DB", "5.0", 1);
    setenv("DSD_NEO_TUNER_AUTOGAIN_INBAND_RATIO", "0.5", 1);
    setenv("DSD_NEO_TUNER_AUTOGAIN_UP_STEP_DB", "2.0", 1);
    setenv("DSD_NEO_TUNER_AUTOGAIN_UP_PERSIST", "3", 1);
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 1400, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_is_set, 1, 1401, "tuner_autogain_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_enable, 1, 1402, "tuner_autogain_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_probe_ms_is_set, 1, 1403, "tuner_autogain_probe_ms_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_probe_ms, 5000, 1404, "tuner_autogain_probe_ms");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_seed_db_is_set, 1, 1405, "tuner_autogain_seed_db_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->tuner_autogain_seed_db, 20.0, 1e-9, 1406, "tuner_autogain_seed_db");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_spec_snr_db_is_set, 1, 1407, "tuner_autogain_spec_snr_db_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->tuner_autogain_spec_snr_db, 5.0, 1e-9, 1408, "tuner_autogain_spec_snr_db");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_inband_ratio_is_set, 1, 1409, "tuner_autogain_inband_ratio_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->tuner_autogain_inband_ratio, 0.5, 1e-9, 1410, "tuner_autogain_inband_ratio");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_up_step_db_is_set, 1, 1411, "tuner_autogain_up_step_db_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->tuner_autogain_up_step_db, 2.0, 1e-9, 1412, "tuner_autogain_up_step_db");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_up_persist_is_set, 1, 1413, "tuner_autogain_up_persist_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_up_persist, 3, 1414, "tuner_autogain_up_persist");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_TUNER_AUTOGAIN");
    unsetenv("DSD_NEO_TUNER_AUTOGAIN_PROBE_MS");
    unsetenv("DSD_NEO_TUNER_AUTOGAIN_SEED_DB");
    unsetenv("DSD_NEO_TUNER_AUTOGAIN_SPEC_SNR_DB");
    unsetenv("DSD_NEO_TUNER_AUTOGAIN_INBAND_RATIO");
    unsetenv("DSD_NEO_TUNER_AUTOGAIN_UP_STEP_DB");
    unsetenv("DSD_NEO_TUNER_AUTOGAIN_UP_PERSIST");

    setenv("DSD_NEO_TUNER_AUTOGAIN_PROBE_MS", "-1", 1);
    setenv("DSD_NEO_TUNER_AUTOGAIN_INBAND_RATIO", "0.05", 1);
    setenv("DSD_NEO_TUNER_AUTOGAIN_UP_STEP_DB", "0.5", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->tuner_autogain_probe_ms_is_set, 0, 1420, "tuner_autogain_probe_ms_is_set (-1)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_probe_ms, 3000, 1421, "tuner_autogain_probe_ms default");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_inband_ratio_is_set, 0, 1422, "tuner_autogain_inband_ratio_is_set (0.05)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->tuner_autogain_inband_ratio, 0.60, 1e-9, 1423, "tuner_autogain_inband_ratio default");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->tuner_autogain_up_step_db_is_set, 0, 1424, "tuner_autogain_up_step_db_is_set (0.5)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->tuner_autogain_up_step_db, 3.0, 1e-9, 1425, "tuner_autogain_up_step_db default");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_TUNER_AUTOGAIN_PROBE_MS");
    unsetenv("DSD_NEO_TUNER_AUTOGAIN_INBAND_RATIO");
    unsetenv("DSD_NEO_TUNER_AUTOGAIN_UP_STEP_DB");
    return 0;
}

static int
test_auto_ppm_env(void) {
    setenv("DSD_NEO_AUTO_PPM", "1", 1);
    setenv("DSD_NEO_AUTO_PPM_SNR_DB", "10.0", 1);
    setenv("DSD_NEO_AUTO_PPM_PWR_DB", "-50.0", 1);
    setenv("DSD_NEO_AUTO_PPM_ZEROLOCK_PPM", "1.0", 1);
    setenv("DSD_NEO_AUTO_PPM_ZEROLOCK_HZ", "100", 1);
    setenv("DSD_NEO_AUTO_PPM_FREEZE", "0", 1);
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 1450, "cfg NULL");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_is_set, 1, 1451, "auto_ppm_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_enable, 1, 1452, "auto_ppm_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_snr_db_is_set, 1, 1453, "auto_ppm_snr_db_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->auto_ppm_snr_db, 10.0, 1e-9, 1454, "auto_ppm_snr_db");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_pwr_db_is_set, 1, 1455, "auto_ppm_pwr_db_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->auto_ppm_pwr_db, -50.0, 1e-9, 1456, "auto_ppm_pwr_db");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_zerolock_ppm_is_set, 1, 1457, "auto_ppm_zerolock_ppm_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->auto_ppm_zerolock_ppm, 1.0, 1e-9, 1458, "auto_ppm_zerolock_ppm");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_zerolock_hz_is_set, 1, 1459, "auto_ppm_zerolock_hz_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_zerolock_hz, 100, 1460, "auto_ppm_zerolock_hz");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_freeze_is_set, 1, 1461, "auto_ppm_freeze_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_freeze_enable, 0, 1462, "auto_ppm_freeze_enable");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_AUTO_PPM");
    unsetenv("DSD_NEO_AUTO_PPM_SNR_DB");
    unsetenv("DSD_NEO_AUTO_PPM_PWR_DB");
    unsetenv("DSD_NEO_AUTO_PPM_ZEROLOCK_PPM");
    unsetenv("DSD_NEO_AUTO_PPM_ZEROLOCK_HZ");
    unsetenv("DSD_NEO_AUTO_PPM_FREEZE");

    setenv("DSD_NEO_AUTO_PPM_PWR_DB", "1.0", 1);
    setenv("DSD_NEO_AUTO_PPM_ZEROLOCK_HZ", "9", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->auto_ppm_pwr_db_is_set, 0, 1470, "auto_ppm_pwr_db_is_set (1.0)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->auto_ppm_pwr_db, -80.0, 1e-9, 1471, "auto_ppm_pwr_db default");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_zerolock_hz_is_set, 0, 1472, "auto_ppm_zerolock_hz_is_set (9)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->auto_ppm_zerolock_hz, 60, 1473, "auto_ppm_zerolock_hz default");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_AUTO_PPM_PWR_DB");
    unsetenv("DSD_NEO_AUTO_PPM_ZEROLOCK_HZ");
    return 0;
}

static int
test_dsp_misc_env(void) {
    setenv("DSD_NEO_COMBINE_ROT", "0", 1);
    setenv("DSD_NEO_UPSAMPLE_FP", "0", 1);
    setenv("DSD_NEO_RESAMP", "96000", 1);
    setenv("DSD_NEO_FLL", "1", 1);
    setenv("DSD_NEO_FLL_ALPHA", "0.01", 1);
    setenv("DSD_NEO_FLL_BETA", "0.001", 1);
    setenv("DSD_NEO_FLL_DEADBAND", "0.004", 1);
    setenv("DSD_NEO_FLL_SLEW", "0.003", 1);
    setenv("DSD_NEO_COSTAS_BW", "0.02", 1);
    setenv("DSD_NEO_COSTAS_DAMPING", "0.7", 1);
    setenv("DSD_NEO_TED", "1", 1);
    setenv("DSD_NEO_TED_GAIN", "0.06", 1);
    setenv("DSD_NEO_TED_FORCE", "1", 1);
    setenv("DSD_NEO_C4FM_CLK", "mm", 1);
    setenv("DSD_NEO_C4FM_CLK_SYNC", "1", 1);
    setenv("DSD_NEO_DEEMPH", "75", 1);
    setenv("DSD_NEO_AUDIO_LPF", "5000", 1);
    setenv("DSD_NEO_MT", "1", 1);
    setenv("DSD_NEO_DISABLE_FS4_SHIFT", "1", 1);
    setenv("DSD_NEO_OUTPUT_CLEAR_ON_RETUNE", "1", 1);
    setenv("DSD_NEO_RETUNE_DRAIN_MS", "100", 1);
    setenv("DSD_NEO_WINDOW_FREEZE", "1", 1);
    setenv("DSD_NEO_PDU_JSON", "1", 1);
    setenv("DSD_NEO_SNR_SQL_DB", "15", 1);
    setenv("DSD_NEO_FM_AGC", "1", 1);
    setenv("DSD_NEO_FM_AGC_TARGET", "0.5", 1);
    setenv("DSD_NEO_FM_AGC_MIN", "0.1", 1);
    setenv("DSD_NEO_FM_AGC_ALPHA_UP", "0.2", 1);
    setenv("DSD_NEO_FM_AGC_ALPHA_DOWN", "0.8", 1);
    setenv("DSD_NEO_FM_LIMITER", "1", 1);
    setenv("DSD_NEO_IQ_DC_BLOCK", "1", 1);
    setenv("DSD_NEO_IQ_DC_SHIFT", "13", 1);
    setenv("DSD_NEO_CHANNEL_LPF", "1", 1);
    dsd_neo_config_init(NULL);

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int rc = expect(cfg != NULL, 1500, "cfg NULL");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->combine_rot_is_set, 1, 1501, "combine_rot_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->combine_rot, 0, 1502, "combine_rot");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->upsample_fp_is_set, 1, 1503, "upsample_fp_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->upsample_fp, 0, 1504, "upsample_fp");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->resamp_is_set, 1, 1505, "resamp_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->resamp_disable, 0, 1506, "resamp_disable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->resamp_target_hz, 96000, 1507, "resamp_target_hz");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fll_is_set, 1, 1508, "fll_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fll_enable, 1, 1509, "fll_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fll_alpha_is_set, 1, 1510, "fll_alpha_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->fll_alpha, 0.01, 1e-6, 1511, "fll_alpha");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fll_beta_is_set, 1, 1512, "fll_beta_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->fll_beta, 0.001, 1e-6, 1513, "fll_beta");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fll_deadband_is_set, 1, 1514, "fll_deadband_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->fll_deadband, 0.004, 1e-6, 1515, "fll_deadband");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fll_slew_is_set, 1, 1516, "fll_slew_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->fll_slew_max, 0.003, 1e-6, 1517, "fll_slew_max");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->costas_bw_is_set, 1, 1520, "costas_bw_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->costas_loop_bw, 0.02, 1e-9, 1521, "costas_loop_bw");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->costas_damping_is_set, 1, 1522, "costas_damping_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->costas_damping, 0.7, 1e-9, 1523, "costas_damping");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->ted_is_set, 1, 1530, "ted_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->ted_enable, 1, 1531, "ted_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->ted_gain_is_set, 1, 1532, "ted_gain_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->ted_gain, 0.06, 1e-6, 1533, "ted_gain");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->ted_force_is_set, 1, 1534, "ted_force_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->ted_force, 1, 1535, "ted_force");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->c4fm_clk_is_set, 1, 1540, "c4fm_clk_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->c4fm_clk_mode, 2, 1541, "c4fm_clk_mode");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->c4fm_clk_sync_is_set, 1, 1542, "c4fm_clk_sync_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->c4fm_clk_sync, 1, 1543, "c4fm_clk_sync");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->deemph_is_set, 1, 1550, "deemph_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->deemph_mode, DSD_NEO_DEEMPH_75, 1551, "deemph_mode");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->audio_lpf_is_set, 1, 1560, "audio_lpf_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->audio_lpf_disable, 0, 1561, "audio_lpf_disable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->audio_lpf_cutoff_hz, 5000, 1562, "audio_lpf_cutoff_hz");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->mt_is_set, 1, 1570, "mt_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->mt_enable, 1, 1571, "mt_enable");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->fs4_shift_disable_is_set, 1, 1580, "fs4_shift_disable_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fs4_shift_disable, 1, 1581, "fs4_shift_disable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->output_clear_on_retune_is_set, 1, 1582, "output_clear_on_retune_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->output_clear_on_retune, 1, 1583, "output_clear_on_retune");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->retune_drain_ms_is_set, 1, 1584, "retune_drain_ms_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->retune_drain_ms, 100, 1585, "retune_drain_ms");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->window_freeze_is_set, 1, 1590, "window_freeze_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->window_freeze, 1, 1591, "window_freeze");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->pdu_json_is_set, 1, 1592, "pdu_json_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->pdu_json_enable, 1, 1593, "pdu_json_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->snr_sql_is_set, 1, 1594, "snr_sql_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->snr_sql_db, 15, 1595, "snr_sql_db");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->fm_agc_is_set, 1, 1600, "fm_agc_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fm_agc_enable, 1, 1601, "fm_agc_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fm_agc_target_is_set, 1, 1602, "fm_agc_target_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->fm_agc_target_rms, 0.5, 1e-6, 1603, "fm_agc_target_rms");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fm_agc_min_is_set, 1, 1604, "fm_agc_min_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->fm_agc_min_rms, 0.1, 1e-6, 1605, "fm_agc_min_rms");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fm_agc_alpha_up_is_set, 1, 1606, "fm_agc_alpha_up_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->fm_agc_alpha_up, 0.2, 1e-6, 1607, "fm_agc_alpha_up");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fm_agc_alpha_down_is_set, 1, 1608, "fm_agc_alpha_down_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_double_close(cfg->fm_agc_alpha_down, 0.8, 1e-6, 1609, "fm_agc_alpha_down");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->fm_limiter_is_set, 1, 1610, "fm_limiter_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->fm_limiter_enable, 1, 1611, "fm_limiter_enable");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->iq_dc_block_is_set, 1, 1620, "iq_dc_block_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->iq_dc_block_enable, 1, 1621, "iq_dc_block_enable");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->iq_dc_shift_is_set, 1, 1622, "iq_dc_shift_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->iq_dc_shift, 13, 1623, "iq_dc_shift");
    if (rc != 0) {
        return rc;
    }

    rc = expect_int_eq(cfg->channel_lpf_is_set, 1, 1630, "channel_lpf_is_set");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->channel_lpf_enable, 1, 1631, "channel_lpf_enable");
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_COMBINE_ROT");
    unsetenv("DSD_NEO_UPSAMPLE_FP");
    unsetenv("DSD_NEO_RESAMP");
    unsetenv("DSD_NEO_FLL");
    unsetenv("DSD_NEO_FLL_ALPHA");
    unsetenv("DSD_NEO_FLL_BETA");
    unsetenv("DSD_NEO_FLL_DEADBAND");
    unsetenv("DSD_NEO_FLL_SLEW");
    unsetenv("DSD_NEO_COSTAS_BW");
    unsetenv("DSD_NEO_COSTAS_DAMPING");
    unsetenv("DSD_NEO_TED");
    unsetenv("DSD_NEO_TED_GAIN");
    unsetenv("DSD_NEO_TED_FORCE");
    unsetenv("DSD_NEO_C4FM_CLK");
    unsetenv("DSD_NEO_C4FM_CLK_SYNC");
    unsetenv("DSD_NEO_DEEMPH");
    unsetenv("DSD_NEO_AUDIO_LPF");
    unsetenv("DSD_NEO_MT");
    unsetenv("DSD_NEO_DISABLE_FS4_SHIFT");
    unsetenv("DSD_NEO_OUTPUT_CLEAR_ON_RETUNE");
    unsetenv("DSD_NEO_RETUNE_DRAIN_MS");
    unsetenv("DSD_NEO_WINDOW_FREEZE");
    unsetenv("DSD_NEO_PDU_JSON");
    unsetenv("DSD_NEO_SNR_SQL_DB");
    unsetenv("DSD_NEO_FM_AGC");
    unsetenv("DSD_NEO_FM_AGC_TARGET");
    unsetenv("DSD_NEO_FM_AGC_MIN");
    unsetenv("DSD_NEO_FM_AGC_ALPHA_UP");
    unsetenv("DSD_NEO_FM_AGC_ALPHA_DOWN");
    unsetenv("DSD_NEO_FM_LIMITER");
    unsetenv("DSD_NEO_IQ_DC_BLOCK");
    unsetenv("DSD_NEO_IQ_DC_SHIFT");
    unsetenv("DSD_NEO_CHANNEL_LPF");

    setenv("DSD_NEO_RESAMP", "off", 1);
    setenv("DSD_NEO_AUDIO_LPF", "off", 1);
    dsd_neo_config_init(NULL);
    cfg = dsd_neo_get_config();
    rc = expect_int_eq(cfg->resamp_is_set, 1, 1650, "resamp_is_set (off)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->resamp_disable, 1, 1651, "resamp_disable (off)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->audio_lpf_is_set, 1, 1652, "audio_lpf_is_set (off)");
    if (rc != 0) {
        return rc;
    }
    rc = expect_int_eq(cfg->audio_lpf_disable, 1, 1653, "audio_lpf_disable (off)");
    if (rc != 0) {
        return rc;
    }
    unsetenv("DSD_NEO_RESAMP");
    unsetenv("DSD_NEO_AUDIO_LPF");

    return 0;
}

int
main(void) {
    unset_all_runtime_env();

    /* Default: unset -> 300ms */
    unsetenv("DSD_NEO_TCPIN_BACKOFF_MS");
    dsd_neo_config_init(NULL);
    int rc = expect_backoff(0, 300, 10);
    if (rc != 0) {
        return rc;
    }

    /* In range -> accepted */
    setenv("DSD_NEO_TCPIN_BACKOFF_MS", "1000", 1);
    dsd_neo_config_init(NULL);
    rc = expect_backoff(1, 1000, 20);
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TCPIN_BACKOFF_MS", "50", 1);
    dsd_neo_config_init(NULL);
    rc = expect_backoff(1, 50, 30);
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TCPIN_BACKOFF_MS", "5000", 1);
    dsd_neo_config_init(NULL);
    rc = expect_backoff(1, 5000, 40);
    if (rc != 0) {
        return rc;
    }

    /* Out of range -> ignored (default) */
    setenv("DSD_NEO_TCPIN_BACKOFF_MS", "49", 1);
    dsd_neo_config_init(NULL);
    rc = expect_backoff(0, 300, 50);
    if (rc != 0) {
        return rc;
    }

    setenv("DSD_NEO_TCPIN_BACKOFF_MS", "5001", 1);
    dsd_neo_config_init(NULL);
    rc = expect_backoff(0, 300, 60);
    if (rc != 0) {
        return rc;
    }

    /* Empty string -> treated as unset */
    setenv("DSD_NEO_TCPIN_BACKOFF_MS", "", 1);
    dsd_neo_config_init(NULL);
    rc = expect_backoff(0, 300, 70);
    if (rc != 0) {
        return rc;
    }

    unsetenv("DSD_NEO_TCPIN_BACKOFF_MS");

    rc = test_tcp_rcvtimeo_ms();
    if (rc != 0) {
        return rc;
    }
    rc = test_tcp_rcvbuf_bytes();
    if (rc != 0) {
        return rc;
    }
    rc = test_tcp_autotune_enable();
    if (rc != 0) {
        return rc;
    }
    rc = test_rtl_direct_mode();
    if (rc != 0) {
        return rc;
    }
    rc = test_tuner_bw_hz();
    if (rc != 0) {
        return rc;
    }
    rc = test_p25_watchdog_ms();
    if (rc != 0) {
        return rc;
    }
    rc = test_dmr_t3_heur_apply();
    if (rc != 0) {
        return rc;
    }
    rc = test_cache_dir_default();
    if (rc != 0) {
        return rc;
    }
    rc = test_cache_dir_override();
    if (rc != 0) {
        return rc;
    }
    rc = test_config_path_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_cc_cache_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_rt_sched_affinity_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_bootstrap_debug_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_cqpsk_sync_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_sync_warmstart_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_protocol_env_knobs();
    if (rc != 0) {
        return rc;
    }
    rc = test_dmr_t3_tools_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_tcp_misc_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_rtl_misc_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_tuner_autogain_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_auto_ppm_env();
    if (rc != 0) {
        return rc;
    }
    rc = test_dsp_misc_env();
    if (rc != 0) {
        return rc;
    }

    return 0;
}
