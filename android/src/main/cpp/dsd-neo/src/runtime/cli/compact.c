// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/cli.h>

#include <string.h>

int
dsd_cli_compact_args(int argc, char** argv) {
    if (argc <= 0 || argv == NULL) {
        return 0;
    }

    // Remove recognized long options so the short-option getopt() only
    // sees remaining tokens; keep argv[0] as program name.
    int w = 1;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (arg == NULL) {
            break;
        }
        if (strcmp(arg, "--auto-ppm") == 0) {
            continue;
        }
        if (strcmp(arg, "--rtltcp-autotune") == 0) {
            continue;
        }
        if (strcmp(arg, "--input-volume") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--input-level-warn-db") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--auto-ppm-snr") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--enc-lockout") == 0) {
            continue;
        }
        if (strcmp(arg, "--enc-follow") == 0) {
            continue;
        }
        if (strcmp(arg, "--no-p25p2-soft") == 0) {
            continue;
        }
        if (strcmp(arg, "--no-p25p1-soft-voice") == 0) {
            continue;
        }
        if (strcmp(arg, "--config") == 0) {
            // Optional path argument (skip only if it doesn't look like another option)
            if (i + 1 < argc && argv[i + 1] != NULL && argv[i + 1][0] != '-') {
                i++;
            }
            continue;
        }
        if (strncmp(arg, "--config=", 9) == 0) {
            continue;
        }
        if (strcmp(arg, "--no-config") == 0) {
            continue;
        }
        if (strcmp(arg, "--print-config") == 0) {
            continue;
        }
        if (strcmp(arg, "--interactive-setup") == 0) {
            continue;
        }
        if (strcmp(arg, "--dump-config-template") == 0) {
            continue;
        }
        if (strcmp(arg, "--validate-config") == 0) {
            if (i + 1 < argc && argv[i + 1] != NULL && argv[i + 1][0] != '-') {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--strict-config") == 0) {
            continue;
        }
        if (strcmp(arg, "--profile") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--list-profiles") == 0) {
            continue;
        }
        if (strcmp(arg, "--p25-vc-grace") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--p25-min-follow-dwell") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--p25-grant-voice-timeout") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--p25-retune-backoff") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--p25-mac-hold") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--p25-ring-hold") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--p25-cc-grace") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--p25-force-release-extra") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--p25-force-release-margin") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--p25-p1-err-hold-pct") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--p25-p1-err-hold-sec") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--calc-lcn") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--calc-step") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--calc-cc-freq") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--calc-cc-lcn") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (strcmp(arg, "--calc-start-lcn") == 0) {
            if (i + 1 < argc) {
                i++;
            }
            continue;
        }
        argv[w++] = argv[i];
    }
    argv[w] = NULL;
    return w;
}
