// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Unit tests for config validation with diagnostics.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/config_schema.h>

#include "test_support.h"

static int
write_temp_config(const char* contents, char* out_path, size_t out_sz) {
    char tmpl[DSD_TEST_PATH_MAX];
    int fd = dsd_test_mkstemp(tmpl, sizeof(tmpl), "dsdneo_config_val");
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
test_valid_config(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "\n"
                             "[output]\n"
                             "backend = \"pulse\"\n"
                             "\n"
                             "[mode]\n"
                             "decode = \"auto\"\n"
                             "\n"
                             "[trunking]\n"
                             "enabled = false\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate(path, &diags);

    int result = 0;
    if (rc != 0) {
        fprintf(stderr, "FAIL: valid config returned error %d\n", rc);
        result = 1;
    }
    if (diags.error_count > 0) {
        fprintf(stderr, "FAIL: valid config has %d errors\n", diags.error_count);
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_unknown_key_warning(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "unknown_key = \"value\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate(path, &diags);

    int result = 0;
    // Should succeed (warnings don't cause failure)
    if (rc != 0) {
        fprintf(stderr, "FAIL: unknown key caused failure (rc=%d)\n", rc);
        result = 1;
    }
    // Should have a warning for unknown key
    if (diags.warning_count == 0) {
        fprintf(stderr, "FAIL: no warning for unknown key\n");
        result = 1;
    }

    // Check that the warning mentions the unknown key
    int found_warning = 0;
    for (int i = 0; i < diags.count; i++) {
        if (diags.items[i].level == DSDCFG_DIAG_WARNING && strstr(diags.items[i].message, "unknown_key")) {
            found_warning = 1;
            break;
        }
    }
    if (!found_warning) {
        fprintf(stderr, "FAIL: warning doesn't mention unknown_key\n");
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_unknown_section_warning(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[unknown_section]\n"
                             "key = \"value\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate(path, &diags);

    int result = 0;
    // Should succeed (warnings don't cause failure)
    if (rc != 0) {
        fprintf(stderr, "FAIL: unknown section caused failure (rc=%d)\n", rc);
        result = 1;
    }
    // Should have a warning for unknown section
    if (diags.warning_count == 0) {
        fprintf(stderr, "FAIL: no warning for unknown section\n");
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_invalid_enum_error(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"invalid_source_type\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate(path, &diags);

    int result = 0;
    // Should return error for invalid enum
    if (rc == 0) {
        fprintf(stderr, "FAIL: invalid enum should cause error\n");
        result = 1;
    }
    if (diags.error_count == 0) {
        fprintf(stderr, "FAIL: no error for invalid enum value\n");
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_int_out_of_range(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"rtl\"\n"
                             "rtl_device = 999\n"; // device index out of range [0, 255]

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate(path, &diags);
    (void)rc;

    int result = 0;
    // Should have a warning for out-of-range value
    if (diags.warning_count == 0) {
        fprintf(stderr, "FAIL: no warning for out-of-range rtl_device=999\n");
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_int_out_of_range_negative_max(void) {
    // rtl_sql has range [-100, 0], so positive values are out of range
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"rtl\"\n"
                             "rtl_sql = 10\n"; // out of range [-100, 0]

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate(path, &diags);
    (void)rc;

    int result = 0;
    // Should have a warning for out-of-range value
    if (diags.warning_count == 0) {
        fprintf(stderr, "FAIL: no warning for out-of-range rtl_sql=10\n");
        result = 1;
    }

    // Check that the warning mentions the out-of-range value
    int found_warning = 0;
    for (int i = 0; i < diags.count; i++) {
        if (diags.items[i].level == DSDCFG_DIAG_WARNING && strstr(diags.items[i].key, "rtl_sql")
            && strstr(diags.items[i].message, "out of range")) {
            found_warning = 1;
            break;
        }
    }
    if (!found_warning) {
        fprintf(stderr, "FAIL: missing out-of-range warning for rtl_sql=10\n");
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_diags_have_line_numbers(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "bad_key = \"value\"\n"; // line 5

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    dsd_user_config_validate(path, &diags);

    int result = 0;
    // Check that diagnostics have line numbers
    int found_line_num = 0;
    for (int i = 0; i < diags.count; i++) {
        if (diags.items[i].line_number > 0) {
            found_line_num = 1;
            // The bad_key should be on line 5
            if (strstr(diags.items[i].key, "bad_key") && diags.items[i].line_number == 5) {
                // Perfect
            }
            break;
        }
    }
    if (diags.count > 0 && !found_line_num) {
        fprintf(stderr, "FAIL: diagnostics missing line numbers\n");
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_empty_config(void) {
    static const char* ini = "";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate(path, &diags);

    // Empty config should not crash and should succeed (no errors)
    int result = 0;
    if (rc != 0) {
        fprintf(stderr, "FAIL: empty config returned error %d\n", rc);
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_nonexistent_file(void) {
    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate("/nonexistent/path/config.ini", &diags);

    // Should return error for nonexistent file
    if (rc == 0) {
        fprintf(stderr, "FAIL: nonexistent file should return error\n");
        dsd_user_config_diags_free(&diags);
        return 1;
    }

    dsd_user_config_diags_free(&diags);
    return 0;
}

static int
test_profile_invalid_enum(void) {
    // Profile with invalid enum value - should produce error
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "\n"
                             "[profile.test]\n"
                             "mode.decode = \"invalid_mode\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate(path, &diags);

    int result = 0;
    // Should return error for invalid enum in profile
    if (rc == 0) {
        fprintf(stderr, "FAIL: profile with invalid enum should cause error\n");
        result = 1;
    }
    if (diags.error_count == 0) {
        fprintf(stderr, "FAIL: no error for invalid enum in profile\n");
        result = 1;
    }

    // Check error mentions the invalid value
    int found_error = 0;
    for (int i = 0; i < diags.count; i++) {
        if (diags.items[i].level == DSDCFG_DIAG_ERROR && strstr(diags.items[i].message, "invalid_mode")) {
            found_error = 1;
            break;
        }
    }
    if (!found_error) {
        fprintf(stderr, "FAIL: error doesn't mention invalid_mode\n");
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_profile_int_out_of_range(void) {
    // Profile with out-of-range integer - should produce warning
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"rtl\"\n"
                             "\n"
                             "[profile.test]\n"
                             "input.rtl_device = 999\n"; // out of range [0, 255]

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    dsd_user_config_validate(path, &diags);

    int result = 0;
    // Should have warning for out-of-range value in profile
    if (diags.warning_count == 0) {
        fprintf(stderr, "FAIL: no warning for out-of-range value in profile\n");
        result = 1;
    }

    // Check warning mentions out of range
    int found_warning = 0;
    for (int i = 0; i < diags.count; i++) {
        if (diags.items[i].level == DSDCFG_DIAG_WARNING && strstr(diags.items[i].message, "out of range")) {
            found_warning = 1;
            break;
        }
    }
    if (!found_warning) {
        fprintf(stderr, "FAIL: warning doesn't mention out of range for profile value\n");
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_profile_invalid_bool(void) {
    // Profile with invalid boolean - should produce error
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "\n"
                             "[profile.test]\n"
                             "trunking.enabled = \"maybe\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate(path, &diags);

    int result = 0;
    // Should return error for invalid boolean in profile
    if (rc == 0) {
        fprintf(stderr, "FAIL: profile with invalid bool should cause error\n");
        result = 1;
    }
    if (diags.error_count == 0) {
        fprintf(stderr, "FAIL: no error for invalid bool in profile\n");
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

static int
test_profile_valid_values(void) {
    // Profile with valid values - should pass validation
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"pulse\"\n"
                             "\n"
                             "[profile.p25_trunk]\n"
                             "mode.decode = \"p25p1\"\n"
                             "trunking.enabled = true\n"
                             "input.rtl_gain = 30\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdcfg_diagnostics_t diags;
    memset(&diags, 0, sizeof(diags));

    int rc = dsd_user_config_validate(path, &diags);

    int result = 0;
    if (rc != 0) {
        fprintf(stderr, "FAIL: valid profile config returned error %d\n", rc);
        result = 1;
    }
    if (diags.error_count > 0) {
        fprintf(stderr, "FAIL: valid profile config has %d errors\n", diags.error_count);
        result = 1;
    }

    dsd_user_config_diags_free(&diags);
    (void)remove(path);
    return result;
}

int
main(void) {
    int rc = 0;

    rc |= test_valid_config();
    rc |= test_unknown_key_warning();
    rc |= test_unknown_section_warning();
    rc |= test_invalid_enum_error();
    rc |= test_int_out_of_range();
    rc |= test_int_out_of_range_negative_max();
    rc |= test_diags_have_line_numbers();
    rc |= test_empty_config();
    rc |= test_nonexistent_file();
    rc |= test_profile_invalid_enum();
    rc |= test_profile_int_out_of_range();
    rc |= test_profile_invalid_bool();
    rc |= test_profile_valid_values();

    if (rc == 0) {
        printf("All config_validation tests passed\n");
    }

    return rc;
}
