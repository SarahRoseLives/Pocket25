// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Control-channel candidate cache and neighbor helpers.
 * Keeps this logic separate from the trunking state machine so it can be
 * reused by tests and UI code without pulling in tuning policy.
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/protocol/p25/p25_cc_candidates.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

int
p25_cc_add_candidate(dsd_state* state, long freq_hz, int bump_added) {
    if (!state || freq_hz == 0) {
        return 0;
    }
    if (freq_hz == state->p25_cc_freq) {
        return 0;
    }
    return dsd_trunk_cc_candidates_add(state, freq_hz, bump_added);
}

int
p25_cc_build_cache_path(const dsd_state* state, char* out, size_t out_len) {
    if (!state || !out || out_len == 0) {
        return 0;
    }
    if (state->p2_wacn == 0 || state->p2_sysid == 0) {
        return 0; // require system identity
    }

    char path[1024] = {0};
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    const char* root = (cfg && cfg->cache_dir[0] != '\0') ? cfg->cache_dir : ".dsdneo_cache";
    snprintf(path, sizeof(path), "%s", root);

    struct stat st;
    if (stat(path, &st) != 0) {
        (void)dsd_mkdir(path, 0700);
    }

    int n = 0;
    if (state->p2_rfssid > 0 && state->p2_siteid > 0) {
        n = snprintf(out, out_len, "%s/p25_cc_%05llX_%03llX_R%03llu_S%03llu.txt", path, state->p2_wacn, state->p2_sysid,
                     state->p2_rfssid, state->p2_siteid);
    } else {
        n = snprintf(out, out_len, "%s/p25_cc_%05llX_%03llX.txt", path, state->p2_wacn, state->p2_sysid);
    }
    return (n > 0 && (size_t)n < out_len);
}

void
p25_cc_try_load_cache(dsd_opts* opts, dsd_state* state) {
    if (!state || state->p25_cc_cache_loaded) {
        return;
    }
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int enable = cfg ? cfg->cc_cache_enable : 1;
    if (!enable) {
        state->p25_cc_cache_loaded = 1;
        return;
    }

    char fpath[1024];
    if (!p25_cc_build_cache_path(state, fpath, sizeof(fpath))) {
        return;
    }
    FILE* fp = fopen(fpath, "r");
    if (!fp) {
        state->p25_cc_cache_loaded = 1;
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char* end = NULL;
        long f = strtol(line, &end, 10);
        if (end == line) {
            continue;
        }
        (void)p25_cc_add_candidate(state, f, 0);
    }
    fclose(fp);
    state->p25_cc_cache_loaded = 1;

    const dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_peek(state);
    const int count = (cc && cc->count > 0 && cc->count <= DSD_TRUNK_CC_CANDIDATES_MAX) ? cc->count : 0;
    if (opts && opts->verbose > 0 && count > 0) {
        fprintf(stderr, "\n  P25 SM: Loaded %d CC candidates from cache\n", count);
    }
}

void
p25_cc_persist_cache(dsd_opts* opts, dsd_state* state) {
    if (!state) {
        return;
    }
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int enable = cfg ? cfg->cc_cache_enable : 1;
    if (!enable) {
        return;
    }

    char fpath[1024];
    if (!p25_cc_build_cache_path(state, fpath, sizeof(fpath))) {
        return;
    }
    FILE* fp = fopen(fpath, "w");
    if (!fp) {
        if (opts && opts->verbose > 1) {
            fprintf(stderr, "\n  P25 SM: Failed to open CC cache for write: %s (errno=%d)\n", fpath, errno);
        }
        return;
    }

    const dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_peek(state);
    const int count = (cc && cc->count > 0 && cc->count <= DSD_TRUNK_CC_CANDIDATES_MAX) ? cc->count : 0;
    for (int i = 0; i < count; i++) {
        if (cc->candidates[i] != 0) {
            fprintf(fp, "%ld\n", cc->candidates[i]);
        }
    }
    fclose(fp);
}

#define P25_NB_TTL_SEC ((time_t)30 * 60)

void
p25_nb_add(dsd_state* state, long freq) {
    if (!state || freq <= 0) {
        return;
    }
    for (int i = 0; i < state->p25_nb_count && i < 32; i++) {
        if (state->p25_nb_freq[i] == freq) {
            state->p25_nb_last_seen[i] = time(NULL);
            return;
        }
    }
    int idx = state->p25_nb_count;
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= 32) {
        int repl = 0;
        time_t oldest = state->p25_nb_last_seen[0];
        for (int i = 1; i < 32; i++) {
            if (state->p25_nb_last_seen[i] < oldest) {
                oldest = state->p25_nb_last_seen[i];
                repl = i;
            }
        }
        idx = repl;
    } else {
        state->p25_nb_count = idx + 1;
    }
    state->p25_nb_freq[idx] = freq;
    state->p25_nb_last_seen[idx] = time(NULL);
}

void
p25_nb_tick(dsd_state* state) {
    if (!state) {
        return;
    }
    time_t now = time(NULL);
    int w = 0;
    for (int i = 0; i < state->p25_nb_count && i < 32; i++) {
        long f = state->p25_nb_freq[i];
        time_t last = state->p25_nb_last_seen[i];
        int keep = (f != 0) && (last == 0 || (now - last) <= P25_NB_TTL_SEC);
        if (keep) {
            if (w != i) {
                state->p25_nb_freq[w] = state->p25_nb_freq[i];
                state->p25_nb_last_seen[w] = state->p25_nb_last_seen[i];
            }
            w++;
        }
    }
    for (int i = w; i < state->p25_nb_count && i < 32; i++) {
        state->p25_nb_freq[i] = 0;
        state->p25_nb_last_seen[i] = 0;
    }
    state->p25_nb_count = w;
}
