// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Unit tests for config path expansion (~, $VAR, ${VAR}).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/runtime/config.h>

#include "test_support.h"

#define setenv   dsd_test_setenv
#define unsetenv dsd_test_unsetenv

static int
test_tilde_expansion(void) {
    char buf[512];
    const char* home = dsd_test_home_dir();
    if (!home || !*home) {
        fprintf(stderr, "SKIP: home dir not set\n");
        return 0;
    }

    // Test ~/path expansion
    int rc = dsd_config_expand_path("~/foo/bar", buf, sizeof(buf));
    if (rc != 0) {
        fprintf(stderr, "FAIL: dsd_config_expand_path returned %d for ~/foo/bar\n", rc);
        return 1;
    }

    char expected[512];
    snprintf(expected, sizeof(expected), "%s/foo/bar", home);
    if (strcmp(buf, expected) != 0) {
        fprintf(stderr, "FAIL: expected '%s', got '%s'\n", expected, buf);
        return 1;
    }

    // Test ~ alone
    rc = dsd_config_expand_path("~", buf, sizeof(buf));
    if (rc != 0 || strcmp(buf, home) != 0) {
        fprintf(stderr, "FAIL: ~ alone should expand to HOME\n");
        return 1;
    }

    return 0;
}

static int
test_env_var_expansion(void) {
    char buf[512];

    // Set a test variable
    setenv("DSD_TEST_VAR", "test_value", 1);

    // Test $VAR form
    int rc = dsd_config_expand_path("/path/$DSD_TEST_VAR/file", buf, sizeof(buf));
    if (rc != 0) {
        fprintf(stderr, "FAIL: dsd_config_expand_path returned %d for $VAR\n", rc);
        return 1;
    }
    if (strcmp(buf, "/path/test_value/file") != 0) {
        fprintf(stderr, "FAIL: $VAR expansion failed, got '%s'\n", buf);
        return 1;
    }

    // Test ${VAR} form
    rc = dsd_config_expand_path("/path/${DSD_TEST_VAR}/file", buf, sizeof(buf));
    if (rc != 0) {
        fprintf(stderr, "FAIL: dsd_config_expand_path returned %d for ${VAR}\n", rc);
        return 1;
    }
    if (strcmp(buf, "/path/test_value/file") != 0) {
        fprintf(stderr, "FAIL: ${VAR} expansion failed, got '%s'\n", buf);
        return 1;
    }

    unsetenv("DSD_TEST_VAR");
    return 0;
}

static int
test_missing_var_expansion(void) {
    char buf[512];

    // Unset any existing variable
    unsetenv("DSD_NONEXISTENT_VAR");

    // Missing variable should expand to empty string
    int rc = dsd_config_expand_path("/path/$DSD_NONEXISTENT_VAR/file", buf, sizeof(buf));
    if (rc != 0) {
        fprintf(stderr, "FAIL: dsd_config_expand_path returned %d for missing var\n", rc);
        return 1;
    }
    if (strcmp(buf, "/path//file") != 0) {
        fprintf(stderr, "FAIL: missing var should expand to empty, got '%s'\n", buf);
        return 1;
    }

    return 0;
}

static int
test_literal_dollar_sign(void) {
    char buf[512];

    // $ followed by non-identifier character should be literal
    int rc = dsd_config_expand_path("/path/$/file", buf, sizeof(buf));
    if (rc != 0) {
        fprintf(stderr, "FAIL: dsd_config_expand_path returned %d\n", rc);
        return 1;
    }
    if (strcmp(buf, "/path/$/file") != 0) {
        fprintf(stderr, "FAIL: literal $ not preserved, got '%s'\n", buf);
        return 1;
    }

    // Malformed ${...  (no closing brace) should preserve $
    rc = dsd_config_expand_path("/path/${INCOMPLETE", buf, sizeof(buf));
    if (rc != 0) {
        fprintf(stderr, "FAIL: dsd_config_expand_path returned %d for malformed\n", rc);
        return 1;
    }
    if (strcmp(buf, "/path/${INCOMPLETE") != 0) {
        fprintf(stderr, "FAIL: malformed ${... not preserved, got '%s'\n", buf);
        return 1;
    }

    return 0;
}

static int
test_no_expansion(void) {
    char buf[512];

    // Path without special characters should pass through
    int rc = dsd_config_expand_path("/usr/local/etc/config.ini", buf, sizeof(buf));
    if (rc != 0) {
        fprintf(stderr, "FAIL: dsd_config_expand_path returned %d\n", rc);
        return 1;
    }
    if (strcmp(buf, "/usr/local/etc/config.ini") != 0) {
        fprintf(stderr, "FAIL: plain path not preserved, got '%s'\n", buf);
        return 1;
    }

    return 0;
}

static int
test_combined_expansion(void) {
    char buf[512];
    const char* home = dsd_test_home_dir();
    if (!home || !*home) {
        fprintf(stderr, "SKIP: home dir not set\n");
        return 0;
    }

    char home_copy[512];
    snprintf(home_copy, sizeof(home_copy), "%s", home);

    setenv("DSD_TEST_DIR", "configs", 1);

    // Combine ~ and $VAR
    int rc = dsd_config_expand_path("~/$DSD_TEST_DIR/test.ini", buf, sizeof(buf));
    if (rc != 0) {
        fprintf(stderr, "FAIL: dsd_config_expand_path returned %d\n", rc);
        return 1;
    }

    char expected[512];
    snprintf(expected, sizeof(expected), "%s/configs/test.ini", home_copy);
    if (strcmp(buf, expected) != 0) {
        fprintf(stderr, "FAIL: combined expansion failed, expected '%s', got '%s'\n", expected, buf);
        return 1;
    }

    unsetenv("DSD_TEST_DIR");
    return 0;
}

static int
test_buffer_overflow_protection(void) {
    char small_buf[16];

    // Set a long value that would overflow the buffer
    setenv("DSD_LONG_VAR", "this_is_a_very_long_value_that_will_overflow", 1);

    int rc = dsd_config_expand_path("$DSD_LONG_VAR", small_buf, sizeof(small_buf));
    // Should return error due to truncation
    if (rc == 0) {
        fprintf(stderr, "FAIL: should have returned error for overflow\n");
        unsetenv("DSD_LONG_VAR");
        return 1;
    }

    unsetenv("DSD_LONG_VAR");
    return 0;
}

int
main(void) {
    int rc = 0;

    rc |= test_tilde_expansion();
    rc |= test_env_var_expansion();
    rc |= test_missing_var_expansion();
    rc |= test_literal_dollar_sign();
    rc |= test_no_expansion();
    rc |= test_combined_expansion();
    rc |= test_buffer_overflow_protection();

    if (rc == 0) {
        printf("All config_expand tests passed\n");
    }

    return rc;
}
