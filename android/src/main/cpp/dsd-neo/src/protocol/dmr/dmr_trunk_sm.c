// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 *
 * DMR Tier III trunking state machine - event-driven, tick-based.
 */

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/dmr/dmr_trunk_sm.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>

#include <stdio.h>
#include <string.h>

void dmr_reset_blocks(dsd_opts* opts, dsd_state* state);

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

static inline double
now_monotonic(void) {
    return dsd_time_now_monotonic_s();
}

static void
sm_log(dsd_opts* opts, const char* tag) {
    if (opts && opts->verbose > 1 && tag) {
        fprintf(stderr, "\n[DMR SM] %s\n", tag);
    }
}

static long
resolve_freq(const dsd_state* state, long freq_hz, int lpcn) {
    if (freq_hz > 0) {
        return freq_hz;
    }
    if (state && lpcn > 0 && lpcn < 0xFFFF) {
        return state->trunk_chan_map[lpcn];
    }
    return 0;
}

static int
lpcn_is_trusted(dsd_opts* opts, dsd_state* state, int lpcn) {
    if (!state || lpcn <= 0 || lpcn >= 0x1000) {
        return 1;
    }
    uint8_t trust = state->dmr_lcn_trust[lpcn];
    int on_cc = (state->trunk_cc_freq != 0 && opts && opts->trunk_is_tuned == 0);
    if (trust < 2 && !on_cc) {
        if (opts && opts->verbose > 0) {
            fprintf(stderr, "\n  DMR SM: block tune LPCN=%d (untrusted off-CC)\n", lpcn);
        }
        return 0;
    }
    return 1;
}

static void
set_state(dmr_sm_ctx_t* ctx, dsd_opts* opts, dmr_sm_state_e new_state, const char* reason) {
    if (!ctx || ctx->state == new_state) {
        return;
    }
    dmr_sm_state_e old = ctx->state;
    ctx->state = new_state;

    if (opts && opts->verbose > 0) {
        fprintf(stderr, "\n[DMR SM] %s -> %s (%s)\n", dmr_sm_state_name(old), dmr_sm_state_name(new_state),
                reason ? reason : "");
    }
}

/* ============================================================================
 * Release to CC
 * ============================================================================ */

static void
do_release(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const char* reason) {
    if (!ctx) {
        return;
    }

    sm_log(opts, reason);

    for (int s = 0; s < 2; s++) {
        ctx->slots[s].voice_active = 0;
        ctx->slots[s].last_active_m = 0.0;
        ctx->slots[s].tg = 0;
    }

    ctx->vc_freq_hz = 0;
    ctx->vc_lpcn = 0;
    ctx->vc_tg = 0;
    ctx->vc_src = 0;
    ctx->t_tune_m = 0.0;
    ctx->t_voice_m = 0.0;

    if (opts) {
        opts->trunk_is_tuned = 0;
    }
    if (state) {
        state->trunk_vc_freq[0] = 0;
        state->trunk_vc_freq[1] = 0;
        state->p25_sm_release_count++;
    }

    dsd_trunk_tuning_hook_return_to_cc(opts, state);

    set_state(ctx, opts, DMR_SM_ON_CC, reason);
}

/* ============================================================================
 * Event Handlers
 * ============================================================================ */

static void
handle_grant(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const dmr_sm_event_t* ev) {
    if (!ctx || !ev || !opts || !state) {
        return;
    }

    if (opts->trunk_enable != 1) {
        return;
    }
    if (state->trunk_cc_freq == 0) {
        return;
    }

    long freq = resolve_freq(state, ev->freq_hz, ev->lpcn);
    if (freq <= 0) {
        sm_log(opts, "grant-no-freq");
        return;
    }

    if (ev->freq_hz <= 0 && ev->lpcn > 0) {
        if (!lpcn_is_trusted(opts, state, ev->lpcn)) {
            return;
        }
    }

    if (ctx->state == DMR_SM_TUNED && ctx->vc_freq_hz == freq) {
        sm_log(opts, "grant-same-freq");
        return;
    }

    double now_m = now_monotonic();

    ctx->vc_freq_hz = freq;
    ctx->vc_lpcn = ev->lpcn;
    ctx->vc_tg = ev->tg;
    ctx->vc_src = ev->src;
    ctx->t_tune_m = now_m;
    ctx->t_voice_m = 0.0;

    for (int s = 0; s < 2; s++) {
        ctx->slots[s].voice_active = 0;
        ctx->slots[s].last_active_m = 0.0;
        ctx->slots[s].tg = 0;
    }

    dmr_reset_blocks(opts, state);
    dsd_trunk_tuning_hook_tune_to_freq(opts, state, freq, 0); // DMR: no TED SPS override

    state->last_t3_tune_time_m = now_m;
    state->p25_sm_tune_count++;

    if (opts->verbose > 0) {
        fprintf(stderr, "\n  DMR SM: Tune VC freq=%.6lf MHz\n", (double)freq / 1000000.0);
    }

    set_state(ctx, opts, DMR_SM_TUNED, "grant");
}

static void
handle_voice_sync(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, int slot) {
    if (!ctx) {
        return;
    }

    double now_m = now_monotonic();
    int s = (slot >= 0 && slot <= 1) ? slot : 0;

    ctx->slots[s].voice_active = 1;
    ctx->slots[s].last_active_m = now_m;
    ctx->t_voice_m = now_m;

    if (state) {
        state->last_vc_sync_time_m = now_m;
    }

    sm_log(opts, "voice-sync");
}

static void
handle_data_sync(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, int slot) {
    if (!ctx || !opts) {
        return;
    }

    if (opts->trunk_tune_data_calls != 1) {
        return;
    }

    double now_m = now_monotonic();
    int s = (slot >= 0 && slot <= 1) ? slot : 0;

    ctx->slots[s].last_active_m = now_m;
    ctx->t_voice_m = now_m;

    if (state) {
        state->last_vc_sync_time_m = now_m;
    }

    sm_log(opts, "data-sync");
}

static void
handle_release(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, int slot) {
    if (!ctx) {
        return;
    }

    if (ctx->state != DMR_SM_TUNED) {
        return;
    }

    if (state && state->trunk_sm_force_release != 0) {
        state->trunk_sm_force_release = 0;
        do_release(ctx, opts, state, "release-forced");
        return;
    }

    if (slot < 0 || slot > 1) {
        // Clear both slots for channel-wide release
        ctx->slots[0].voice_active = 0;
        ctx->slots[1].voice_active = 0;
    } else {
        ctx->slots[slot].voice_active = 0;
    }

    sm_log(opts, "release-requested");
}

static void
handle_cc_sync(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state) {
    if (!ctx) {
        return;
    }
    UNUSED(state);

    ctx->t_cc_sync_m = now_monotonic();

    if (ctx->state == DMR_SM_IDLE || ctx->state == DMR_SM_HUNTING) {
        set_state(ctx, opts, DMR_SM_ON_CC, "cc-sync");
    }
}

/* ============================================================================
 * Public API - Core State Machine
 * ============================================================================ */

const char*
dmr_sm_state_name(dmr_sm_state_e state) {
    switch (state) {
        case DMR_SM_IDLE: return "IDLE";
        case DMR_SM_ON_CC: return "ON_CC";
        case DMR_SM_TUNED: return "TUNED";
        case DMR_SM_HUNTING: return "HUNT";
        default: return "?";
    }
}

void
dmr_sm_init_ctx(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state) {
    if (!ctx) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();

    ctx->hangtime_s = 2.0;
    ctx->grant_timeout_s = 4.0;
    ctx->cc_grace_s = 2.0;

    // Honor user hangtime setting, including zero for immediate release
    if (opts && opts->trunk_hangtime >= 0.0) {
        ctx->hangtime_s = opts->trunk_hangtime;
    }

    if (cfg && cfg->dmr_hangtime_is_set) {
        ctx->hangtime_s = cfg->dmr_hangtime_s;
    }
    if (cfg && cfg->dmr_grant_timeout_is_set) {
        ctx->grant_timeout_s = cfg->dmr_grant_timeout_s;
    }

    if (state && state->trunk_cc_freq != 0) {
        ctx->state = DMR_SM_ON_CC;
        ctx->t_cc_sync_m = now_monotonic();
    } else {
        ctx->state = DMR_SM_IDLE;
    }

    ctx->initialized = 1;
}

void
dmr_sm_event(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const dmr_sm_event_t* ev) {
    if (!ctx || !ev) {
        return;
    }

    if (!ctx->initialized) {
        dmr_sm_init_ctx(ctx, opts, state);
    }

    switch (ev->type) {
        case DMR_SM_EV_GRANT: handle_grant(ctx, opts, state, ev); break;
        case DMR_SM_EV_VOICE_SYNC: handle_voice_sync(ctx, opts, state, ev->slot); break;
        case DMR_SM_EV_DATA_SYNC: handle_data_sync(ctx, opts, state, ev->slot); break;
        case DMR_SM_EV_RELEASE: handle_release(ctx, opts, state, ev->slot); break;
        case DMR_SM_EV_CC_SYNC: handle_cc_sync(ctx, opts, state); break;
        case DMR_SM_EV_SYNC_LOST: break;
    }
}

void
dmr_sm_tick_ctx(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state) {
    if (!ctx) {
        return;
    }

    if (!ctx->initialized) {
        dmr_sm_init_ctx(ctx, opts, state);
    }

    double now_m = now_monotonic();
    double hangtime = ctx->hangtime_s;
    double grant_timeout = ctx->grant_timeout_s;
    double cc_grace = ctx->cc_grace_s;

    switch (ctx->state) {
        case DMR_SM_IDLE: break;

        case DMR_SM_ON_CC:
            if (ctx->t_cc_sync_m > 0.0) {
                double dt_cc = now_m - ctx->t_cc_sync_m;
                if (dt_cc > cc_grace) {
                    set_state(ctx, opts, DMR_SM_HUNTING, "cc-lost");
                }
            }
            break;

        case DMR_SM_TUNED: {
            // Clear voice_active for slots that haven't received sync recently.
            // DMR voice frames arrive every ~60ms; use 200ms as a generous threshold.
            const double voice_stale_threshold = 0.2;
            for (int s = 0; s < 2; s++) {
                if (ctx->slots[s].voice_active && ctx->slots[s].last_active_m > 0.0) {
                    double dt_slot = now_m - ctx->slots[s].last_active_m;
                    if (dt_slot > voice_stale_threshold) {
                        ctx->slots[s].voice_active = 0;
                    }
                }
            }

            int has_voice = ctx->slots[0].voice_active || ctx->slots[1].voice_active;

            if (has_voice) {
                ctx->t_voice_m = now_m;
            } else if (ctx->t_voice_m > 0.0) {
                double dt_voice = now_m - ctx->t_voice_m;
                if (dt_voice >= hangtime) {
                    do_release(ctx, opts, state, "hangtime-expired");
                }
            } else {
                if (ctx->t_tune_m > 0.0) {
                    double dt_tune = now_m - ctx->t_tune_m;
                    if (dt_tune >= grant_timeout) {
                        do_release(ctx, opts, state, "grant-timeout");
                    }
                }
            }
            break;
        }

        case DMR_SM_HUNTING: break;
    }
}

/* ============================================================================
 * Global Singleton
 * ============================================================================ */

static dmr_sm_ctx_t g_dmr_sm_ctx;
static int g_dmr_sm_initialized = 0;

dmr_sm_ctx_t*
dmr_sm_get_ctx(void) {
    if (!g_dmr_sm_initialized) {
        dmr_sm_init_ctx(&g_dmr_sm_ctx, NULL, NULL);
        g_dmr_sm_initialized = 1;
    }
    return &g_dmr_sm_ctx;
}

/* ============================================================================
 * Convenience Emit Functions
 * ============================================================================ */

void
dmr_sm_emit(dsd_opts* opts, dsd_state* state, const dmr_sm_event_t* ev) {
    dmr_sm_event(dmr_sm_get_ctx(), opts, state, ev);
}

void
dmr_sm_emit_voice_sync(dsd_opts* opts, dsd_state* state, int slot) {
    dmr_sm_event_t ev = dmr_sm_ev_voice_sync(slot);
    dmr_sm_event(dmr_sm_get_ctx(), opts, state, &ev);
}

void
dmr_sm_emit_data_sync(dsd_opts* opts, dsd_state* state, int slot) {
    dmr_sm_event_t ev = dmr_sm_ev_data_sync(slot);
    dmr_sm_event(dmr_sm_get_ctx(), opts, state, &ev);
}

void
dmr_sm_emit_release(dsd_opts* opts, dsd_state* state, int slot) {
    dmr_sm_event_t ev = dmr_sm_ev_release(slot);
    dmr_sm_event(dmr_sm_get_ctx(), opts, state, &ev);
}

void
dmr_sm_emit_cc_sync(dsd_opts* opts, dsd_state* state) {
    dmr_sm_event_t ev = dmr_sm_ev_cc_sync();
    dmr_sm_event(dmr_sm_get_ctx(), opts, state, &ev);
}

void
dmr_sm_emit_group_grant(dsd_opts* opts, dsd_state* state, long freq_hz, int lpcn, int tg, int src) {
    dmr_sm_event_t ev = dmr_sm_ev_group_grant(freq_hz, lpcn, tg, src);
    dmr_sm_event(dmr_sm_get_ctx(), opts, state, &ev);
}

void
dmr_sm_emit_indiv_grant(dsd_opts* opts, dsd_state* state, long freq_hz, int lpcn, int dst, int src) {
    dmr_sm_event_t ev = dmr_sm_ev_indiv_grant(freq_hz, lpcn, dst, src);
    dmr_sm_event(dmr_sm_get_ctx(), opts, state, &ev);
}

void
dmr_sm_init(dsd_opts* opts, dsd_state* state) {
    // Reset global flag to allow re-initialization with real opts/state.
    // This ensures user configuration (e.g., trunk_hangtime) is applied
    // even if the singleton was previously auto-initialized with NULLs.
    g_dmr_sm_initialized = 0;
    dmr_sm_init_ctx(dmr_sm_get_ctx(), opts, state);
}

void
dmr_sm_tick(dsd_opts* opts, dsd_state* state) {
    dmr_sm_tick_ctx(dmr_sm_get_ctx(), opts, state);
}

/* ============================================================================
 * Neighbor/CC Candidate Management
 * ============================================================================ */

void
dmr_sm_on_neighbor_update(dsd_opts* opts, dsd_state* state, const long* freqs, int count) {
    if (!state || !freqs || count <= 0) {
        return;
    }
    UNUSED(opts);

    for (int i = 0; i < count; i++) {
        long f = freqs[i];
        if (f == 0 || f == state->trunk_cc_freq) {
            continue;
        }
        (void)dsd_trunk_cc_candidates_add(state, f, 1);
    }
}

int
dmr_sm_next_cc_candidate(dsd_state* state, long* out_freq) {
    return dsd_trunk_cc_candidates_next(state, now_monotonic(), out_freq);
}
