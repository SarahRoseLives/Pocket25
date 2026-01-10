// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/cli.h>

#include <stdio.h>
#include <string.h>

static int
test_config_without_path_does_not_consume_next_arg(void) {
    char arg0[] = "dsd-neo";
    char arg1[] = "--config";
    char arg2[] = "-fi";
    char* argv[] = {arg0, arg1, arg2, NULL};

    int new_argc = dsd_cli_compact_args(3, argv);
    if (new_argc != 2) {
        fprintf(stderr, "expected new_argc=2, got %d\n", new_argc);
        return 1;
    }
    if (argv[1] == NULL || strcmp(argv[1], "-fi") != 0) {
        fprintf(stderr, "expected argv[1] to be \"-fi\", got \"%s\"\n", argv[1] ? argv[1] : "(null)");
        return 1;
    }
    return 0;
}

static int
test_config_with_path_consumes_only_path(void) {
    char arg0[] = "dsd-neo";
    char arg1[] = "--config";
    char arg2[] = "config.ini";
    char arg3[] = "-fi";
    char* argv[] = {arg0, arg1, arg2, arg3, NULL};

    int new_argc = dsd_cli_compact_args(4, argv);
    if (new_argc != 2) {
        fprintf(stderr, "expected new_argc=2, got %d\n", new_argc);
        return 1;
    }
    if (argv[1] == NULL || strcmp(argv[1], "-fi") != 0) {
        fprintf(stderr, "expected argv[1] to be \"-fi\", got \"%s\"\n", argv[1] ? argv[1] : "(null)");
        return 1;
    }
    return 0;
}

static int
test_config_equals_form_is_removed(void) {
    char arg0[] = "dsd-neo";
    char arg1[] = "--config=config.ini";
    char arg2[] = "-fi";
    char* argv[] = {arg0, arg1, arg2, NULL};

    int new_argc = dsd_cli_compact_args(3, argv);
    if (new_argc != 2) {
        fprintf(stderr, "expected new_argc=2, got %d\n", new_argc);
        return 1;
    }
    if (argv[1] == NULL || strcmp(argv[1], "-fi") != 0) {
        fprintf(stderr, "expected argv[1] to be \"-fi\", got \"%s\"\n", argv[1] ? argv[1] : "(null)");
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    rc |= test_config_without_path_does_not_consume_next_arg();
    rc |= test_config_with_path_consumes_only_path();
    rc |= test_config_equals_form_is_removed();
    return rc;
}
