// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/bootstrap.h>
#include <dsd-neo/runtime/cli.h>

#include <dsd-neo/core/init.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/file_compat.h>
#include <dsd-neo/platform/posix_compat.h>
#define DSD_NEO_MAIN
#include <dsd-neo/protocol/dmr/dmr_const.h>
#include <dsd-neo/protocol/dstar/dstar_const.h>
#include <dsd-neo/protocol/nxdn/nxdn_const.h>
#include <dsd-neo/protocol/p25/p25p1_const.h>
#include <dsd-neo/protocol/provoice/provoice_const.h>
#include <dsd-neo/protocol/x2tdma/x2tdma_const.h>
#undef DSD_NEO_MAIN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
noCarrier(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

static void
test_redirect_stdout_to_null(void) {
#if defined(_WIN32)
    (void)freopen("NUL", "w", stdout);
#else
    (void)freopen("/dev/null", "w", stdout);
#endif
}

static int
test_help_returns_one_shot_and_does_not_exit(void) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(dsd_opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(dsd_state));
    if (!opts || !state) {
        free(opts);
        free(state);
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    initOpts(opts);
    initState(state);

    char arg0[] = "dsd-neo";
    char arg1[] = "-h";
    char* argv[] = {arg0, arg1, NULL};

    int argc_effective = 0;
    int exit_rc = -1;

    test_redirect_stdout_to_null();
    int rc = dsd_parse_args(2, argv, opts, state, &argc_effective, &exit_rc);
    if (rc != DSD_PARSE_ONE_SHOT) {
        fprintf(stderr, "expected rc=%d, got %d\n", DSD_PARSE_ONE_SHOT, rc);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }
    if (exit_rc != 0) {
        fprintf(stderr, "expected exit_rc=0, got %d\n", exit_rc);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }
    freeState(state);
    free(opts);
    free(state);
    return 0;
}

static int
test_invalid_option_returns_error_and_does_not_exit(void) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(dsd_opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(dsd_state));
    if (!opts || !state) {
        free(opts);
        free(state);
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    initOpts(opts);
    initState(state);

    char arg0[] = "dsd-neo";
    char arg1[] = "-H";
    char arg2[] = "ZZ";
    char* argv[] = {arg0, arg1, arg2, NULL};

    int argc_effective = 0;
    int exit_rc = 0;

    int rc = dsd_parse_args(3, argv, opts, state, &argc_effective, &exit_rc);
    if (rc != DSD_PARSE_ERROR) {
        fprintf(stderr, "expected rc=%d, got %d\n", DSD_PARSE_ERROR, rc);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }
    if (exit_rc != 1) {
        fprintf(stderr, "expected exit_rc=1, got %d\n", exit_rc);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }
    freeState(state);
    free(opts);
    free(state);
    return 0;
}

static int
test_unknown_option_returns_error_and_does_not_exit(void) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(dsd_opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(dsd_state));
    if (!opts || !state) {
        free(opts);
        free(state);
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    initOpts(opts);
    initState(state);

    char arg0[] = "dsd-neo";
    char arg1[] = "-?";
    char* argv[] = {arg0, arg1, NULL};

    int argc_effective = 0;
    int exit_rc = 0;

    test_redirect_stdout_to_null();
    int rc = dsd_parse_args(2, argv, opts, state, &argc_effective, &exit_rc);
    if (rc != DSD_PARSE_ERROR) {
        fprintf(stderr, "expected rc=%d, got %d\n", DSD_PARSE_ERROR, rc);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }
    if (exit_rc != 1) {
        fprintf(stderr, "expected exit_rc=1, got %d\n", exit_rc);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }
    freeState(state);
    free(opts);
    free(state);
    return 0;
}

static int
test_H_loads_aes256_key_for_both_slots(void) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(dsd_opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(dsd_state));
    if (!opts || !state) {
        free(opts);
        free(state);
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    initOpts(opts);
    initState(state);

    char arg0[] = "dsd-neo";
    char arg1[] = "-H";
    char arg2[] = "20029736A5D91042 C923EB0697484433 005EFC58A1905195 E28E9C7836AA2DB8";
    char* argv[] = {arg0, arg1, arg2, NULL};

    int argc_effective = 0;
    int exit_rc = -1;
    int rc = dsd_parse_args(3, argv, opts, state, &argc_effective, &exit_rc);
    if (rc != DSD_PARSE_CONTINUE) {
        fprintf(stderr, "expected rc=%d, got %d (exit_rc=%d)\n", DSD_PARSE_CONTINUE, rc, exit_rc);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    const unsigned long long k1 = 0x20029736A5D91042ULL;
    const unsigned long long k2 = 0xC923EB0697484433ULL;
    const unsigned long long k3 = 0x005EFC58A1905195ULL;
    const unsigned long long k4 = 0xE28E9C7836AA2DB8ULL;

    if (state->A1[0] != k1 || state->A2[0] != k2 || state->A3[0] != k3 || state->A4[0] != k4 || state->A1[1] != k1
        || state->A2[1] != k2 || state->A3[1] != k3 || state->A4[1] != k4) {
        fprintf(stderr,
                "expected A1..A4 to match key segments, got slot0=%016llX %016llX %016llX %016llX slot1=%016llX "
                "%016llX %016llX %016llX\n",
                (unsigned long long)state->A1[0], (unsigned long long)state->A2[0], (unsigned long long)state->A3[0],
                (unsigned long long)state->A4[0], (unsigned long long)state->A1[1], (unsigned long long)state->A2[1],
                (unsigned long long)state->A3[1], (unsigned long long)state->A4[1]);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    if (state->aes_key_loaded[0] != 1 || state->aes_key_loaded[1] != 1) {
        fprintf(stderr, "expected aes_key_loaded[0..1]=1, got %d/%d\n", state->aes_key_loaded[0],
                state->aes_key_loaded[1]);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    const uint8_t expect_bytes[32] = {
        0x20, 0x02, 0x97, 0x36, 0xA5, 0xD9, 0x10, 0x42, 0xC9, 0x23, 0xEB, 0x06, 0x97, 0x48, 0x44, 0x33,
        0x00, 0x5E, 0xFC, 0x58, 0xA1, 0x90, 0x51, 0x95, 0xE2, 0x8E, 0x9C, 0x78, 0x36, 0xAA, 0x2D, 0xB8,
    };
    if (memcmp(state->aes_key, expect_bytes, sizeof(expect_bytes)) != 0) {
        fprintf(stderr, "expected aes_key bytes to match key, got mismatch\n");
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    freeState(state);
    free(opts);
    free(state);
    return 0;
}

static int
test_1_loads_rc4_key_for_both_slots_and_allows_spaces(void) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(dsd_opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(dsd_state));
    if (!opts || !state) {
        free(opts);
        free(state);
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    initOpts(opts);
    initState(state);

    char arg0[] = "dsd-neo";
    char arg1[] = "-1";
    char arg2[] = "12 34 56 78 91";
    char* argv[] = {arg0, arg1, arg2, NULL};

    int argc_effective = 0;
    int exit_rc = -1;
    int rc = dsd_parse_args(3, argv, opts, state, &argc_effective, &exit_rc);
    if (rc != DSD_PARSE_CONTINUE) {
        fprintf(stderr, "expected rc=%d, got %d (exit_rc=%d)\n", DSD_PARSE_CONTINUE, rc, exit_rc);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    const unsigned long long expect = 0x1234567891ULL;
    if (state->R != expect || state->RR != expect) {
        fprintf(stderr, "expected R/RR=%010llX, got %010llX/%010llX\n", expect, state->R, state->RR);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    freeState(state);
    free(opts);
    free(state);
    return 0;
}

static int
test_1_loads_rc4_key_allows_0x_prefix(void) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(dsd_opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(dsd_state));
    if (!opts || !state) {
        free(opts);
        free(state);
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    initOpts(opts);
    initState(state);

    char arg0[] = "dsd-neo";
    char arg1[] = "-1";
    char arg2[] = "0x1234567891";
    char* argv[] = {arg0, arg1, arg2, NULL};

    int argc_effective = 0;
    int exit_rc = -1;
    int rc = dsd_parse_args(3, argv, opts, state, &argc_effective, &exit_rc);
    if (rc != DSD_PARSE_CONTINUE) {
        fprintf(stderr, "expected rc=%d, got %d (exit_rc=%d)\n", DSD_PARSE_CONTINUE, rc, exit_rc);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    const unsigned long long expect = 0x1234567891ULL;
    if (state->R != expect || state->RR != expect) {
        fprintf(stderr, "expected R/RR=%010llX, got %010llX/%010llX\n", expect, state->R, state->RR);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    freeState(state);
    free(opts);
    free(state);
    return 0;
}

static const char*
test_tmp_dir(void) {
    const char* dir = getenv("TMPDIR");
#if DSD_PLATFORM_WIN_NATIVE
    if (!dir || !*dir) {
        dir = getenv("TEMP");
    }
    if (!dir || !*dir) {
        dir = getenv("TMP");
    }
#else
    if (!dir || !*dir) {
        dir = "/tmp";
    }
#endif
    if (!dir || !*dir) {
        dir = ".";
    }
    return dir;
}

static char
test_path_sep(void) {
#if DSD_PLATFORM_WIN_NATIVE
    return '\\';
#else
    return '/';
#endif
}

static int
test_create_temp_ini(char* out_path, size_t out_path_size) {
    if (!out_path || out_path_size == 0) {
        return -1;
    }

    const char sep = test_path_sep();
    const char* tdir = test_tmp_dir();

    char tmpl[1024];
    size_t tdir_len = strlen(tdir);
    if (tdir_len > 0 && (tdir[tdir_len - 1] == '/' || tdir[tdir_len - 1] == '\\')) {
        snprintf(tmpl, sizeof tmpl, "%sdsdneo_bootstrap_XXXXXX", tdir);
    } else {
        snprintf(tmpl, sizeof tmpl, "%s%c%s", tdir, sep, "dsdneo_bootstrap_XXXXXX");
    }

    int fd = dsd_mkstemp(tmpl);
    if (fd < 0) {
        return -1;
    }
    (void)dsd_close(fd);

    if (snprintf(out_path, out_path_size, "%s.ini", tmpl) >= (int)out_path_size) {
        (void)remove(tmpl);
        return -1;
    }

    if (rename(tmpl, out_path) != 0) {
        (void)remove(tmpl);
        return -1;
    }

    FILE* fp = fopen(out_path, "w");
    if (!fp) {
        (void)remove(out_path);
        return -1;
    }

    fputs("version = 1\n"
          "\n"
          "[input]\n"
          "source = \"rtl\"\n"
          "rtl_device = 0\n"
          "rtl_freq = \"100000000\"\n"
          "\n"
          "[trunking]\n"
          "enabled = true\n",
          fp);

    fclose(fp);
    return 0;
}

static int
test_bootstrap_treats_lone_ini_as_config(void) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(dsd_opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(dsd_state));
    if (!opts || !state) {
        free(opts);
        free(state);
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    initOpts(opts);
    initState(state);

    // Make test deterministic: avoid env-config interference and skip bootstrap UI.
    (void)dsd_unsetenv("DSD_NEO_CONFIG");
    (void)dsd_setenv("DSD_NEO_NO_BOOTSTRAP", "1", 1);

    char cfg_path[1024];
    if (test_create_temp_ini(cfg_path, sizeof cfg_path) != 0) {
        fprintf(stderr, "failed to create temp ini\n");
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    char arg0[] = "dsd-neo";
    char* argv[] = {arg0, cfg_path, NULL};

    int argc_effective = 0;
    int exit_rc = -1;
    int rc = dsd_runtime_bootstrap(2, argv, opts, state, &argc_effective, &exit_rc);

    if (rc != DSD_BOOTSTRAP_CONTINUE) {
        fprintf(stderr, "expected rc=%d, got %d (exit_rc=%d)\n", DSD_BOOTSTRAP_CONTINUE, rc, exit_rc);
        (void)remove(cfg_path);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    // Ensure it behaves like "--config <path>" by compacting the effective argc down to argv[0] only.
    if (argc_effective != 1) {
        fprintf(stderr, "expected argc_effective=1, got %d\n", argc_effective);
        (void)remove(cfg_path);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    if (!state->config_autosave_enabled || strcmp(state->config_autosave_path, cfg_path) != 0) {
        fprintf(stderr, "expected config_autosave_path=%s, got %s (enabled=%d)\n", cfg_path,
                state->config_autosave_path, state->config_autosave_enabled);
        (void)remove(cfg_path);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    if (opts->trunk_enable != 1 || opts->p25_trunk != 1) {
        fprintf(stderr, "expected trunking enabled from config, got trunk_enable=%d p25_trunk=%d\n", opts->trunk_enable,
                opts->p25_trunk);
        (void)remove(cfg_path);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    if (strncmp(opts->audio_in_dev, "rtl:", 4) != 0) {
        fprintf(stderr, "expected RTL input from config, got audio_in_dev=%s\n", opts->audio_in_dev);
        (void)remove(cfg_path);
        freeState(state);
        free(opts);
        free(state);
        return 1;
    }

    (void)remove(cfg_path);
    freeState(state);
    free(opts);
    free(state);
    return 0;
}

int
main(void) {
    int rc = 0;
    rc |= test_help_returns_one_shot_and_does_not_exit();
    rc |= test_invalid_option_returns_error_and_does_not_exit();
    rc |= test_unknown_option_returns_error_and_does_not_exit();
    rc |= test_H_loads_aes256_key_for_both_slots();
    rc |= test_1_loads_rc4_key_for_both_slots_and_allows_spaces();
    rc |= test_1_loads_rc4_key_allows_0x_prefix();
    rc |= test_bootstrap_treats_lone_ini_as_config();
    return rc;
}
