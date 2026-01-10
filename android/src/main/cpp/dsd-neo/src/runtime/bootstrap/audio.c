// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/platform/audio.h>
#include <dsd-neo/runtime/cli.h>
#include <dsd-neo/runtime/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
trim_newline(char* s) {
    if (!s) {
        return;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static int
prompt_int(const char* q, int def_val, int min_val, int max_val) {
    char buf[64];
    fprintf(stderr, "%s [%d]: ", q, def_val);
    if (!fgets(buf, sizeof buf, stdin)) {
        return def_val;
    }
    trim_newline(buf);
    if (buf[0] == '\0') {
        return def_val;
    }
    char* end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf) {
        return def_val;
    }
    if (v < min_val) {
        v = min_val;
    }
    if (v > max_val) {
        v = max_val;
    }
    return (int)v;
}

void
dsd_bootstrap_choose_audio_output(dsd_opts* opts) {
    dsd_audio_device ins[16];
    dsd_audio_device outs[16];
    int n_out = 0;

    if (dsd_audio_enumerate_devices(ins, outs, 16) < 0) {
        LOG_WARNING("Audio device query failed; using default output.\n");
        snprintf(opts->audio_out_dev, sizeof opts->audio_out_dev, "%s", "pulse");
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (outs[i].initialized) {
            n_out++;
        } else {
            break;
        }
    }

    fprintf(stderr, "\nOutput Sinks:\n");
    fprintf(stderr, "  0) Default\n");
    for (int i = 0; i < n_out; i++) {
        fprintf(stderr, "  %d) %s (%s)\n", i + 1, outs[i].name, outs[i].description);
    }
    int sel_out = prompt_int("Select output sink", 0, 0, n_out);
    if (sel_out <= 0) {
        snprintf(opts->audio_out_dev, sizeof opts->audio_out_dev, "%s", "pulse");
    } else {
        const char* name = outs[sel_out - 1].name;
        snprintf(opts->audio_out_dev, sizeof opts->audio_out_dev, "pulse:%s", name);
    }
}

void
dsd_bootstrap_choose_audio_input(dsd_opts* opts) {
    dsd_audio_device ins[16];
    dsd_audio_device outs[16];
    int n_in = 0;

    if (dsd_audio_enumerate_devices(ins, outs, 16) < 0) {
        LOG_WARNING("Audio device query failed; using default input.\n");
        snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", "pulse");
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (ins[i].initialized) {
            n_in++;
        } else {
            break;
        }
    }

    fprintf(stderr, "\nInput Sources:\n");
    fprintf(stderr, "  0) Default\n");
    for (int i = 0; i < n_in; i++) {
        fprintf(stderr, "  %d) %s (%s)\n", i + 1, ins[i].name, ins[i].description);
    }
    int sel_in = prompt_int("Select input source", 0, 0, n_in);
    if (sel_in <= 0) {
        snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", "pulse");
    } else {
        const char* name = ins[sel_in - 1].name;
        snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "pulse:%s", name);
    }
}
