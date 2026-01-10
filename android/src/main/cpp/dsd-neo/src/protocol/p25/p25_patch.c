// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 regroup/patch tracking utilities
 */

#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>

#include <stdio.h>
#include <time.h>

#define P25_PATCH_TTL_SECONDS 20

static int
find_patch_idx(dsd_state* state, uint16_t sgid) {
    if (!state) {
        return -1;
    }
    for (int i = 0; i < state->p25_patch_count && i < 8; i++) {
        if (state->p25_patch_sgid[i] == sgid) {
            return i;
        }
    }
    return -1;
}

static void
p25_patch_sweep_stale(dsd_state* state) {
    if (!state) {
        return;
    }
    time_t now = time(NULL);
    int w = 0;
    for (int i = 0; i < state->p25_patch_count && i < 8; i++) {
        int keep = 1;
        if (state->p25_patch_last_update[i] > 0 && (now - state->p25_patch_last_update[i]) > P25_PATCH_TTL_SECONDS) {
            keep = 0;
        }
        if (keep) {
            if (w != i) {
                state->p25_patch_sgid[w] = state->p25_patch_sgid[i];
                state->p25_patch_is_patch[w] = state->p25_patch_is_patch[i];
                state->p25_patch_active[w] = state->p25_patch_active[i];
                state->p25_patch_last_update[w] = state->p25_patch_last_update[i];
                // Carry over membership and optional K/ALG/SSN context
                state->p25_patch_wgid_count[w] = state->p25_patch_wgid_count[i];
                state->p25_patch_wuid_count[w] = state->p25_patch_wuid_count[i];
                for (int k = 0; k < 8; k++) {
                    state->p25_patch_wgid[w][k] = state->p25_patch_wgid[i][k];
                    state->p25_patch_wuid[w][k] = state->p25_patch_wuid[i][k];
                }
                state->p25_patch_key[w] = state->p25_patch_key[i];
                state->p25_patch_alg[w] = state->p25_patch_alg[i];
                state->p25_patch_ssn[w] = state->p25_patch_ssn[i];
                state->p25_patch_key_valid[w] = state->p25_patch_key_valid[i];
            }
            w++;
        }
    }
    state->p25_patch_count = w;
}

void
p25_patch_update(dsd_state* state, int sgid, int is_patch, int active) {
    if (!state || sgid <= 0 || sgid > 0xFFFF) {
        return;
    }
    time_t now = time(NULL);
    // Try to find existing entry
    for (int i = 0; i < state->p25_patch_count && i < 8; i++) {
        if (state->p25_patch_sgid[i] == (uint16_t)sgid) {
            state->p25_patch_is_patch[i] = is_patch ? 1 : 0;
            state->p25_patch_active[i] = active ? 1 : 0;
            state->p25_patch_last_update[i] = now;
            return;
        }
    }
    // Append if space and activating (or even if inactive, we still record timestamp)
    int idx = state->p25_patch_count;
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= 8) {
        // Replace the stalest entry
        int repl = 0;
        time_t oldest = state->p25_patch_last_update[0];
        for (int i = 1; i < 8; i++) {
            if (state->p25_patch_last_update[i] < oldest) {
                oldest = state->p25_patch_last_update[i];
                repl = i;
            }
        }
        idx = repl;
    } else {
        state->p25_patch_count = idx + 1;
    }
    state->p25_patch_sgid[idx] = (uint16_t)sgid;
    state->p25_patch_is_patch[idx] = is_patch ? 1 : 0;
    state->p25_patch_active[idx] = active ? 1 : 0;
    state->p25_patch_last_update[idx] = now;
    state->p25_patch_wgid_count[idx] = 0;
    state->p25_patch_wuid_count[idx] = 0;
    state->p25_patch_key_valid[idx] = 0;
}

int
p25_patch_compose_summary(const dsd_state* state_in, char* out, size_t cap) {
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!state_in) {
        return 0;
    }
    // Copy to temp and sweep stale to avoid modifying caller's state
    dsd_state* state = (dsd_state*)state_in;
    p25_patch_sweep_stale(state);
    char buf[128] = {0};
    int n = 0;
    for (int i = 0; i < state->p25_patch_count && i < 8; i++) {
        if (!state->p25_patch_active[i]) {
            continue;
        }
        if (!state->p25_patch_is_patch[i]) {
            continue; // show patches only (not simulselects)
        }
        if (n == 0) {
            n += snprintf(buf + n, sizeof(buf) - n, "P: %03u", state->p25_patch_sgid[i]);
        } else {
            n += snprintf(buf + n, sizeof(buf) - n, ",%03u", state->p25_patch_sgid[i]);
        }
        if (n >= (int)sizeof(buf) - 8) {
            break;
        }
    }
    if (n <= 0) {
        return 0;
    }
    // Copy to output
    int w = snprintf(out, cap, "%s", buf);
    return (w > 0) ? w : 0;
}

void
p25_patch_add_wgid(dsd_state* state, int sgid, int wgid) {
    if (!state || sgid <= 0 || sgid > 0xFFFF || wgid <= 0 || wgid > 0xFFFF) {
        return;
    }
    int idx = find_patch_idx(state, (uint16_t)sgid);
    if (idx < 0) {
        // Create entry as active patch by default
        p25_patch_update(state, sgid, 1, 1);
        idx = find_patch_idx(state, (uint16_t)sgid);
        if (idx < 0) {
            return;
        }
    }
    // Dedup
    uint8_t cnt = state->p25_patch_wgid_count[idx];
    for (int i = 0; i < cnt && i < 8; i++) {
        if (state->p25_patch_wgid[idx][i] == (uint16_t)wgid) {
            return;
        }
    }
    if (cnt < 8) {
        state->p25_patch_wgid[idx][cnt] = (uint16_t)wgid;
        state->p25_patch_wgid_count[idx] = cnt + 1;
    }
}

void
p25_patch_add_wuid(dsd_state* state, int sgid, uint32_t wuid) {
    if (!state || sgid <= 0 || sgid > 0xFFFF || wuid == 0) {
        return;
    }
    int idx = find_patch_idx(state, (uint16_t)sgid);
    if (idx < 0) {
        p25_patch_update(state, sgid, 1, 1);
        idx = find_patch_idx(state, (uint16_t)sgid);
        if (idx < 0) {
            return;
        }
    }
    uint8_t cnt = state->p25_patch_wuid_count[idx];
    for (int i = 0; i < cnt && i < 8; i++) {
        if (state->p25_patch_wuid[idx][i] == wuid) {
            return;
        }
    }
    if (cnt < 8) {
        state->p25_patch_wuid[idx][cnt] = wuid;
        state->p25_patch_wuid_count[idx] = cnt + 1;
    }
}

int
p25_patch_compose_details(const dsd_state* state_in, char* out, size_t cap) {
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!state_in) {
        return 0;
    }
    dsd_state* state = (dsd_state*)state_in;
    p25_patch_sweep_stale(state);
    int n = 0;
    for (int i = 0; i < state->p25_patch_count && i < 8; i++) {
        if (!state->p25_patch_active[i]) {
            continue;
        }
        char t = state->p25_patch_is_patch[i] ? 'P' : 'S';
        uint16_t sg = state->p25_patch_sgid[i];
        if (n > 0) {
            n += snprintf(out + n, cap > (size_t)n ? cap - (size_t)n : 0, "; ");
        }
        n += snprintf(out + n, cap > (size_t)n ? cap - (size_t)n : 0, "SG%03u[%c]", sg, t);
        uint8_t wgc = state->p25_patch_wgid_count[i];
        uint8_t wuc = state->p25_patch_wuid_count[i];
        if (wgc > 0) {
            if (wgc <= 3) {
                n += snprintf(out + n, cap > (size_t)n ? cap - (size_t)n : 0, " WG:");
                for (int k = 0; k < wgc && k < 3; k++) {
                    n += snprintf(out + n, cap > (size_t)n ? cap - (size_t)n : 0, k == 0 ? "%04u" : ",%04u",
                                  state->p25_patch_wgid[i][k]);
                }
            } else {
                n += snprintf(out + n, cap > (size_t)n ? cap - (size_t)n : 0, " WG:%u(%04u,%04u+)", wgc,
                              state->p25_patch_wgid[i][0], state->p25_patch_wgid[i][1]);
            }
        } else if (wuc > 0) {
            n += snprintf(out + n, cap > (size_t)n ? cap - (size_t)n : 0, " U:%u", wuc);
        }
        // Optional crypt context: print only fields that are present
        if (state->p25_patch_key[i] || state->p25_patch_alg[i] || state->p25_patch_ssn[i]) {
            if (state->p25_patch_key[i]) {
                n += snprintf(out + n, cap > (size_t)n ? cap - (size_t)n : 0, " K:%04X",
                              state->p25_patch_key[i] & 0xFFFF);
            }
            if (state->p25_patch_alg[i]) {
                n +=
                    snprintf(out + n, cap > (size_t)n ? cap - (size_t)n : 0, " A:%02X", state->p25_patch_alg[i] & 0xFF);
            }
            if (state->p25_patch_ssn[i]) {
                n +=
                    snprintf(out + n, cap > (size_t)n ? cap - (size_t)n : 0, " S:%02u", state->p25_patch_ssn[i] & 0x1F);
            }
        }
        if (n >= (int)cap - 8) {
            break;
        }
    }
    return n;
}

void
p25_patch_remove_wgid(dsd_state* state, int sgid, int wgid) {
    if (!state || sgid <= 0 || wgid <= 0) {
        return;
    }
    int idx = find_patch_idx(state, (uint16_t)sgid);
    if (idx < 0) {
        return;
    }
    uint8_t cnt = state->p25_patch_wgid_count[idx];
    for (int i = 0; i < cnt && i < 8; i++) {
        if (state->p25_patch_wgid[idx][i] == (uint16_t)wgid) {
            // swap with last
            if (i != cnt - 1) {
                state->p25_patch_wgid[idx][i] = state->p25_patch_wgid[idx][cnt - 1];
            }
            state->p25_patch_wgid_count[idx] = cnt - 1;
            break;
        }
    }
    if (state->p25_patch_wgid_count[idx] == 0 && state->p25_patch_wuid_count[idx] == 0) {
        state->p25_patch_active[idx] = 0;
    }
}

void
p25_patch_remove_wuid(dsd_state* state, int sgid, uint32_t wuid) {
    if (!state || sgid <= 0 || wuid == 0) {
        return;
    }
    int idx = find_patch_idx(state, (uint16_t)sgid);
    if (idx < 0) {
        return;
    }
    uint8_t cnt = state->p25_patch_wuid_count[idx];
    for (int i = 0; i < cnt && i < 8; i++) {
        if (state->p25_patch_wuid[idx][i] == wuid) {
            if (i != cnt - 1) {
                state->p25_patch_wuid[idx][i] = state->p25_patch_wuid[idx][cnt - 1];
            }
            state->p25_patch_wuid_count[idx] = cnt - 1;
            break;
        }
    }
    if (state->p25_patch_wgid_count[idx] == 0 && state->p25_patch_wuid_count[idx] == 0) {
        state->p25_patch_active[idx] = 0;
    }
}

void
p25_patch_clear_sg(dsd_state* state, int sgid) {
    if (!state || sgid <= 0) {
        return;
    }
    int idx = find_patch_idx(state, (uint16_t)sgid);
    if (idx < 0) {
        return;
    }
    state->p25_patch_wgid_count[idx] = 0;
    state->p25_patch_wuid_count[idx] = 0;
    state->p25_patch_active[idx] = 0;
}

void
p25_patch_set_kas(dsd_state* state, int sgid, int key, int alg, int ssn) {
    if (!state || sgid <= 0) {
        return;
    }
    int idx = find_patch_idx(state, (uint16_t)sgid);
    if (idx < 0) {
        p25_patch_update(state, sgid, 1, 1);
        idx = find_patch_idx(state, (uint16_t)sgid);
        if (idx < 0) {
            return;
        }
    }
    if (key >= 0) {
        state->p25_patch_key[idx] = (uint16_t)key;
        state->p25_patch_key_valid[idx] = 1;
    }
    if (alg >= 0) {
        state->p25_patch_alg[idx] = (uint8_t)alg;
    }
    if (ssn >= 0) {
        state->p25_patch_ssn[idx] = (uint8_t)(ssn & 0x1F);
    }
}

// Return 1 if the given talkgroup (assumed WGID) is a member of an active
// regroup/patch whose policy key has been explicitly signaled as 0 (clear).
// Returns 0 otherwise.
int
p25_patch_tg_key_is_clear(const dsd_state* state, int tg) {
    if (!state || tg <= 0) {
        return 0;
    }
    // Copy pointer (const) usage only
    const dsd_state* s = state;
    time_t now = time(NULL);
    for (int i = 0; i < s->p25_patch_count && i < 8; i++) {
        if (!s->p25_patch_active[i]) {
            continue;
        }
        // Ignore stale entries defensively (should be swept by caller periodically)
        if (s->p25_patch_last_update[i] > 0 && (now - s->p25_patch_last_update[i]) > P25_PATCH_TTL_SECONDS) {
            continue;
        }
        uint8_t cnt = s->p25_patch_wgid_count[i];
        for (int k = 0; k < cnt && k < 8; k++) {
            if (s->p25_patch_wgid[i][k] == (uint16_t)tg) {
                if (s->p25_patch_key_valid[i] && s->p25_patch_key[i] == 0) {
                    return 1; // explicit KEY=0000 policy for this SG/WG
                }
                return 0; // membership found but key not clear (or unknown)
            }
        }
    }
    return 0;
}

// Return 1 if the given SGID has an explicitly signaled KEY of 0 (clear) and
// is currently active. Returns 0 otherwise.
int
p25_patch_sg_key_is_clear(const dsd_state* state, int sgid) {
    if (!state || sgid <= 0) {
        return 0;
    }
    const dsd_state* s = state;
    time_t now = time(NULL);
    for (int i = 0; i < s->p25_patch_count && i < 8; i++) {
        if (!s->p25_patch_active[i]) {
            continue;
        }
        if (s->p25_patch_sgid[i] != (uint16_t)sgid) {
            continue;
        }
        if (s->p25_patch_last_update[i] > 0 && (now - s->p25_patch_last_update[i]) > P25_PATCH_TTL_SECONDS) {
            continue;
        }
        return (s->p25_patch_key_valid[i] && s->p25_patch_key[i] == 0) ? 1 : 0;
    }
    return 0;
}
