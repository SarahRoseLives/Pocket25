// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Unit tests for config profile support.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/runtime/config.h>

#include "test_support.h"

static int
write_temp_config(const char* contents, char* out_path, size_t out_sz) {
    char tmpl[DSD_TEST_PATH_MAX];
    int fd = dsd_test_mkstemp(tmpl, sizeof(tmpl), "dsdneo_config_prof");
    if (fd < 0) {
        fprintf(stderr, "dsd_test_mkstemp failed: %s\n", strerror(errno));
        return 1;
    }
    size_t len = strlen(contents);
    ssize_t wr = dsd_write(fd, contents, len);
    if (wr < 0 || (size_t)wr != len) {
        fprintf(stderr, "write failed: %s\n", strerror(errno));
        (void)dsd_close(fd);
        (void)remove(tmpl);
        return 1;
    }
    (void)dsd_close(fd);
    snprintf(out_path, out_sz, "%s", tmpl);
    out_path[out_sz - 1] = '\0';
    return 0;
}

static int
test_load_without_profile(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "\n"
                             "[mode]\n"
                             "decode = \"auto\"\n"
                             "\n"
                             "[profile.test]\n"
                             "mode.decode = \"dmr\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdneoUserConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    // Load without profile - should get base config
    int rc = dsd_user_config_load_profile(path, NULL, &cfg);

    int result = 0;
    if (rc != 0) {
        fprintf(stderr, "FAIL: load without profile failed (rc=%d)\n", rc);
        result = 1;
    }

    // Should have decode = auto (base config)
    if (cfg.decode_mode != DSDCFG_MODE_AUTO) {
        fprintf(stderr, "FAIL: expected auto mode, got %d\n", cfg.decode_mode);
        result = 1;
    }

    (void)remove(path);
    return result;
}

static int
test_load_with_profile_override(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "\n"
                             "[mode]\n"
                             "decode = \"auto\"\n"
                             "\n"
                             "[profile.dmr_mode]\n"
                             "mode.decode = \"dmr\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdneoUserConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    // Load with profile - should override decode mode
    int rc = dsd_user_config_load_profile(path, "dmr_mode", &cfg);

    int result = 0;
    if (rc != 0) {
        fprintf(stderr, "FAIL: load with profile failed (rc=%d)\n", rc);
        result = 1;
    }

    // Should have decode = dmr (from profile)
    if (cfg.decode_mode != DSDCFG_MODE_DMR) {
        fprintf(stderr, "FAIL: profile did not override mode, expected DMR, got %d\n", cfg.decode_mode);
        result = 1;
    }

    // Input source should still be pulse (from base)
    if (cfg.input_source != DSDCFG_INPUT_PULSE) {
        fprintf(stderr, "FAIL: base config lost, expected pulse input, got %d\n", cfg.input_source);
        result = 1;
    }

    (void)remove(path);
    return result;
}

static int
test_profile_multiple_overrides(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "\n"
                             "[output]\n"
                             "backend = \"pulse\"\n"
                             "ncurses_ui = false\n"
                             "\n"
                             "[mode]\n"
                             "decode = \"auto\"\n"
                             "\n"
                             "[trunking]\n"
                             "enabled = false\n"
                             "\n"
                             "[profile.p25_trunk]\n"
                             "mode.decode = \"p25p1\"\n"
                             "trunking.enabled = true\n"
                             "output.ncurses_ui = true\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdneoUserConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    int rc = dsd_user_config_load_profile(path, "p25_trunk", &cfg);

    int result = 0;
    if (rc != 0) {
        fprintf(stderr, "FAIL: load with p25_trunk profile failed (rc=%d)\n", rc);
        result = 1;
    }

    // Check all overrides
    if (cfg.decode_mode != DSDCFG_MODE_P25P1) {
        fprintf(stderr, "FAIL: expected p25p1 mode, got %d\n", cfg.decode_mode);
        result = 1;
    }
    if (!cfg.trunk_enabled) {
        fprintf(stderr, "FAIL: trunking should be enabled\n");
        result = 1;
    }
    if (!cfg.ncurses_ui) {
        fprintf(stderr, "FAIL: ncurses_ui should be enabled\n");
        result = 1;
    }

    // Check base values not overridden
    if (cfg.input_source != DSDCFG_INPUT_PULSE) {
        fprintf(stderr, "FAIL: input source should still be pulse\n");
        result = 1;
    }
    if (cfg.output_backend != DSDCFG_OUTPUT_PULSE) {
        fprintf(stderr, "FAIL: output backend should still be pulse\n");
        result = 1;
    }

    (void)remove(path);
    return result;
}

static int
test_unknown_profile(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "\n"
                             "[profile.existing]\n"
                             "mode.decode = \"dmr\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdneoUserConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    // Load with non-existent profile
    int rc = dsd_user_config_load_profile(path, "nonexistent", &cfg);

    int result = 0;
    // Should fail for unknown profile
    if (rc == 0) {
        fprintf(stderr, "FAIL: unknown profile should return error\n");
        result = 1;
    }

    (void)remove(path);
    return result;
}

static int
test_list_profiles(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "\n"
                             "[profile.alpha]\n"
                             "mode.decode = \"dmr\"\n"
                             "\n"
                             "[profile.beta]\n"
                             "mode.decode = \"p25p1\"\n"
                             "\n"
                             "[profile.gamma]\n"
                             "mode.decode = \"ysf\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    const char* names[16];
    char names_buf[256];
    int count = dsd_user_config_list_profiles(path, names, names_buf, sizeof(names_buf), 16);

    int result = 0;
    if (count != 3) {
        fprintf(stderr, "FAIL: expected 3 profiles, got %d\n", count);
        result = 1;
    }

    // Check that all profiles are listed
    int found_alpha = 0, found_beta = 0, found_gamma = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], "alpha") == 0) {
            found_alpha = 1;
        }
        if (strcmp(names[i], "beta") == 0) {
            found_beta = 1;
        }
        if (strcmp(names[i], "gamma") == 0) {
            found_gamma = 1;
        }
    }
    if (!found_alpha || !found_beta || !found_gamma) {
        fprintf(stderr, "FAIL: missing profiles in list (alpha=%d, beta=%d, gamma=%d)\n", found_alpha, found_beta,
                found_gamma);
        result = 1;
    }

    (void)remove(path);
    return result;
}

static int
test_list_profiles_empty(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    const char* names[16];
    char names_buf[256];
    int count = dsd_user_config_list_profiles(path, names, names_buf, sizeof(names_buf), 16);

    int result = 0;
    if (count != 0) {
        fprintf(stderr, "FAIL: expected 0 profiles, got %d\n", count);
        result = 1;
    }

    (void)remove(path);
    return result;
}

static int
test_profile_rtl_settings(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "\n"
                             "[profile.rtl_scan]\n"
                             "input.source = \"rtl\"\n"
                             "input.rtl_device = 0\n"
                             "input.rtl_freq = \"851.375M\"\n"
                             "input.rtl_gain = 30\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdneoUserConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    int rc = dsd_user_config_load_profile(path, "rtl_scan", &cfg);

    int result = 0;
    if (rc != 0) {
        fprintf(stderr, "FAIL: load with rtl_scan profile failed (rc=%d)\n", rc);
        result = 1;
    }

    if (cfg.input_source != DSDCFG_INPUT_RTL) {
        fprintf(stderr, "FAIL: expected rtl source, got %d\n", cfg.input_source);
        result = 1;
    }
    if (cfg.rtl_device != 0) {
        fprintf(stderr, "FAIL: expected rtl_device 0, got %d\n", cfg.rtl_device);
        result = 1;
    }
    if (strcmp(cfg.rtl_freq, "851.375M") != 0) {
        fprintf(stderr, "FAIL: expected rtl_freq 851.375M, got %s\n", cfg.rtl_freq);
        result = 1;
    }
    if (cfg.rtl_gain != 30) {
        fprintf(stderr, "FAIL: expected rtl_gain 30, got %d\n", cfg.rtl_gain);
        result = 1;
    }

    (void)remove(path);
    return result;
}

static int
test_include_directive(void) {
    /* Create included file first */
    static const char* included_ini = "version = 1\n"
                                      "\n"
                                      "[input]\n"
                                      "source = \"rtl\"\n"
                                      "rtl_device = 2\n"
                                      "rtl_gain = 25\n"
                                      "\n"
                                      "[mode]\n"
                                      "decode = \"dmr\"\n";

    char included_path[DSD_TEST_PATH_MAX];
    if (write_temp_config(included_ini, included_path, sizeof included_path) != 0) {
        return 1;
    }

    /* Create main config that includes the first file */
    char main_ini[512];
    snprintf(main_ini, sizeof main_ini,
             "include = \"%s\"\n"
             "version = 1\n"
             "\n"
             "[output]\n"
             "ncurses_ui = true\n",
             included_path);

    char main_path[DSD_TEST_PATH_MAX];
    if (write_temp_config(main_ini, main_path, sizeof main_path) != 0) {
        (void)remove(included_path);
        return 1;
    }

    dsdneoUserConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    int rc = dsd_user_config_load_profile(main_path, NULL, &cfg);

    int result = 0;
    if (rc != 0) {
        fprintf(stderr, "FAIL: load with include failed (rc=%d)\n", rc);
        result = 1;
    }

    /* Values from included file should be present */
    if (cfg.input_source != DSDCFG_INPUT_RTL) {
        fprintf(stderr, "FAIL: include: expected rtl source, got %d\n", cfg.input_source);
        result = 1;
    }
    if (cfg.rtl_device != 2) {
        fprintf(stderr, "FAIL: include: expected rtl_device 2, got %d\n", cfg.rtl_device);
        result = 1;
    }
    if (cfg.rtl_gain != 25) {
        fprintf(stderr, "FAIL: include: expected rtl_gain 25, got %d\n", cfg.rtl_gain);
        result = 1;
    }
    if (cfg.decode_mode != DSDCFG_MODE_DMR) {
        fprintf(stderr, "FAIL: include: expected dmr mode, got %d\n", cfg.decode_mode);
        result = 1;
    }

    /* Values from main file should also be present */
    if (!cfg.ncurses_ui) {
        fprintf(stderr, "FAIL: include: ncurses_ui should be true from main config\n");
        result = 1;
    }

    (void)remove(main_path);
    (void)remove(included_path);
    return result;
}

static int
test_include_override(void) {
    /* Create included file with base values */
    static const char* included_ini = "version = 1\n"
                                      "\n"
                                      "[input]\n"
                                      "source = \"rtl\"\n"
                                      "rtl_gain = 20\n"
                                      "\n"
                                      "[mode]\n"
                                      "decode = \"auto\"\n";

    char included_path[DSD_TEST_PATH_MAX];
    if (write_temp_config(included_ini, included_path, sizeof included_path) != 0) {
        return 1;
    }

    /* Create main config that overrides some values */
    char main_ini[512];
    snprintf(main_ini, sizeof main_ini,
             "include = \"%s\"\n"
             "version = 1\n"
             "\n"
             "[input]\n"
             "rtl_gain = 35\n"
             "\n"
             "[mode]\n"
             "decode = \"p25p1\"\n",
             included_path);

    char main_path[DSD_TEST_PATH_MAX];
    if (write_temp_config(main_ini, main_path, sizeof main_path) != 0) {
        (void)remove(included_path);
        return 1;
    }

    dsdneoUserConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    int rc = dsd_user_config_load_profile(main_path, NULL, &cfg);

    int result = 0;
    if (rc != 0) {
        fprintf(stderr, "FAIL: load with include override failed (rc=%d)\n", rc);
        result = 1;
    }

    /* source should come from included file (not overridden) */
    if (cfg.input_source != DSDCFG_INPUT_RTL) {
        fprintf(stderr, "FAIL: include override: expected rtl source, got %d\n", cfg.input_source);
        result = 1;
    }

    /* rtl_gain should be overridden by main file */
    if (cfg.rtl_gain != 35) {
        fprintf(stderr, "FAIL: include override: expected rtl_gain 35, got %d\n", cfg.rtl_gain);
        result = 1;
    }

    /* decode should be overridden by main file */
    if (cfg.decode_mode != DSDCFG_MODE_P25P1) {
        fprintf(stderr, "FAIL: include override: expected p25p1 mode, got %d\n", cfg.decode_mode);
        result = 1;
    }

    (void)remove(main_path);
    (void)remove(included_path);
    return result;
}

int
main(void) {
    int rc = 0;

    rc |= test_load_without_profile();
    rc |= test_load_with_profile_override();
    rc |= test_profile_multiple_overrides();
    rc |= test_unknown_profile();
    rc |= test_list_profiles();
    rc |= test_list_profiles_empty();
    rc |= test_profile_rtl_settings();
    rc |= test_include_directive();
    rc |= test_include_override();

    if (rc == 0) {
        printf("All config_profile tests passed\n");
    }

    return rc;
}
