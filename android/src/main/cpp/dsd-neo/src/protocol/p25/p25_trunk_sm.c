// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 *
 * Unified P25 trunking state machine.
 */

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/platform/atomic_compat.h>
#include <dsd-neo/protocol/p25/p25_cc_candidates.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>
#include <dsd-neo/protocol/p25/p25_sm_ui.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/p25_optional_hooks.h>
#include <dsd-neo/runtime/p25_p2_audio_ring.h>
#include <dsd-neo/runtime/rtl_stream_metrics_hooks.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

// Forward declaration for do_release (used by handle_enc)
static void do_release(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const char* reason);

// Serialize release-to-CC operations to avoid duplicate retunes when multiple
// threads (watchdog tick + decoder) request release concurrently.
static atomic_int g_p25_sm_release_lock = 0;

static inline double
now_monotonic(void) {
    return dsd_time_now_monotonic_s();
}

// Determine if channel is TDMA based on IDEN hints
static inline int
is_tdma_channel(const dsd_state* state, int channel) {
    if (!state) {
        return 0;
    }
    int iden = (channel >> 12) & 0xF;
    if (iden >= 0 && iden < 16) {
        int explicit_hint = (iden >= 0 && iden < 16) ? state->p25_chan_tdma_explicit[iden] : 0;
        // Honor explicit IDEN hints first: 2 = TDMA, 1 = FDMA.
        if (explicit_hint == 2) {
            return 1;
        }
        if (explicit_hint == 1) {
            return 0;
        }

        int is_tdma = (state->p25_chan_tdma[iden] & 0x1) ? 1 : 0;
        // Fall back to system-level TDMA knowledge when the IDEN does not carry
        // an explicit TDMA/FDMA declaration. This covers systems with P25p1
        // CQPSK control channels that have not sent IDEN_UP_TDMA yet, preventing
        // Phase 2 grants from being treated as FDMA and avoiding SPS mismatch on VC hops.
        if (!is_tdma && state->p25_sys_is_tdma == 1) {
            is_tdma = 1;
        }
        return is_tdma;
    }
    return 0;
}

static inline int
channel_slot(const dsd_state* state, int channel) {
    return is_tdma_channel(state, channel) ? ((channel & 1) ? 1 : 0) : -1;
}

// Compute TED SPS based on actual demodulator output rate (accounts for resampler).
static inline int
p25_ted_sps_for_bw(const dsd_opts* opts, int sym_rate_hz) {
    /* Query actual demodulator output rate first (accounts for any active resampler).
     * Falls back to rtl_dsp_bw_khz if RTL stream is unavailable or returns 0. */
    int demod_rate = (int)dsd_rtl_stream_metrics_hook_output_rate_hz();
    return dsd_opts_compute_sps_rate(opts, sym_rate_hz, demod_rate);
}

// Compute TED SPS for control channel based on CC type.
static inline int
cc_ted_sps(const dsd_opts* opts, const dsd_state* state) {
    int sym_rate = (state && state->p25_cc_is_tdma == 1) ? 6000 : 4800;
    return p25_ted_sps_for_bw(opts, sym_rate);
}

// Log status tag for debugging
static void
sm_log(dsd_opts* opts, dsd_state* state, const char* tag) {
    if (!opts || opts->verbose < 1 || !tag) {
        return;
    }
    if (state) {
        snprintf(state->p25_sm_last_reason, sizeof(state->p25_sm_last_reason), "%s", tag);
        state->p25_sm_last_reason_time = time(NULL);
        int idx = state->p25_sm_tag_head % 8;
        snprintf(state->p25_sm_tags[idx], sizeof(state->p25_sm_tags[idx]), "%s", tag);
        state->p25_sm_tag_time[idx] = state->p25_sm_last_reason_time;
        state->p25_sm_tag_head++;
        if (state->p25_sm_tag_count < 8) {
            state->p25_sm_tag_count++;
        }
    }
    if (opts->verbose > 1) {
        fprintf(stderr, "\n[P25 SM] %s\n", tag);
    }
}

// Set state with logging
static void
set_state(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, p25_sm_state_e new_state, const char* reason) {
    if (!ctx || ctx->state == new_state) {
        return;
    }
    p25_sm_state_e old = ctx->state;
    ctx->state = new_state;

    // Update state->p25_sm_mode for UI - direct 1:1 mapping
    if (state) {
        switch (new_state) {
            case P25_SM_IDLE: state->p25_sm_mode = DSD_P25_SM_MODE_UNKNOWN; break;
            case P25_SM_ON_CC: state->p25_sm_mode = DSD_P25_SM_MODE_ON_CC; break;
            case P25_SM_TUNED: state->p25_sm_mode = DSD_P25_SM_MODE_ON_VC; break;
            case P25_SM_HUNTING: state->p25_sm_mode = DSD_P25_SM_MODE_HUNTING; break;
        }
    }

    if (opts && opts->verbose > 0) {
        fprintf(stderr, "\n[P25 SM] %s -> %s (%s)\n", p25_sm_state_name(old), p25_sm_state_name(new_state),
                reason ? reason : "");
    }
    sm_log(opts, state, reason);
}

/* ============================================================================
 * Grant Filtering (preserve existing policy logic)
 * ============================================================================ */

// Service option bit helpers
#define SVC_IS_DATA(svc) (((svc) & 0x10) != 0)
#define SVC_IS_ENC(svc)  (((svc) & 0x40) != 0)

// Check if TG is blocked in group array (mode "DE" or "B")
static int
tg_is_blocked(const dsd_state* state, int tg) {
    if (!state || tg <= 0) {
        return 0;
    }
    for (unsigned int i = 0; i < state->group_tally; i++) {
        if (state->group_array[i].groupNumber == (unsigned long)tg) {
            const char* m = state->group_array[i].groupMode;
            return (m[0] == 'D' && m[1] == 'E') || (m[0] == 'B' && m[1] == '\0');
        }
    }
    return 0;
}

static int
grant_allowed(dsd_opts* opts, dsd_state* state, const p25_sm_event_t* ev) {
    if (!opts || !state || !ev) {
        return 0;
    }

    int svc = ev->svc_bits;
    int tg = ev->tg;
    int is_indiv = !ev->is_group;

    // Fast path: TG hold check (integer compare)
    if (state->tg_hold != 0) {
        if (is_indiv || (uint32_t)tg != state->tg_hold) {
            sm_log(opts, state, is_indiv ? "indiv-blocked-hold" : "grant-blocked-hold");
            return 0;
        }
    }

    // Data call policy (bit check)
    if (SVC_IS_DATA(svc) && opts->trunk_tune_data_calls == 0) {
        sm_log(opts, state, is_indiv ? "indiv-blocked-data" : "grant-blocked-data");
        return 0;
    }

    // Individual (private) call gating
    if (is_indiv) {
        if (opts->trunk_tune_private_calls == 0) {
            sm_log(opts, state, "indiv-blocked-private");
            return 0;
        }
        if (SVC_IS_ENC(svc) && opts->trunk_tune_enc_calls == 0) {
            sm_log(opts, state, "indiv-blocked-enc");
            return 0;
        }
        return 1;
    }

    // Group grant gating
    if (opts->trunk_tune_group_calls == 0) {
        sm_log(opts, state, "grant-blocked-group");
        return 0;
    }

    // Encryption policy with patch override
    if (SVC_IS_ENC(svc) && opts->trunk_tune_enc_calls == 0) {
        if (!p25_patch_tg_key_is_clear(state, tg) && !p25_patch_sg_key_is_clear(state, tg)) {
            sm_log(opts, state, "grant-blocked-enc");
            p25_emit_enc_lockout_once(opts, state, 0, tg, svc);
            return 0;
        }
        sm_log(opts, state, "enc-override-clear");
    }

    // Group list mode check (string compare - rare path)
    if (tg_is_blocked(state, tg)) {
        sm_log(opts, state, "grant-blocked-mode");
        return 0;
    }

    // Track RID<->TG mapping
    if (ev->src > 0 && tg > 0) {
        p25_ga_add(state, (uint32_t)ev->src, (uint16_t)tg);
    }

    return 1;
}

/* ============================================================================
 * Event Handlers
 * ============================================================================ */

static void
handle_grant(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const p25_sm_event_t* ev) {
    if (!ctx || !ev || !opts || !state) {
        return;
    }

    // Check grant policy
    if (!grant_allowed(opts, state, ev)) {
        return;
    }

    // Compute frequency from channel
    long freq = process_channel_to_freq(opts, state, ev->channel);
    if (freq == 0) {
        sm_log(opts, state, "grant-no-freq");
        return;
    }

    // Skip if already tuned to same frequency AND same TG (avoid bounce on duplicate grants)
    // Different TG/call type should still trigger a new tune
    if (ctx->state == P25_SM_TUNED && ctx->vc_freq_hz == freq && ctx->vc_tg == ev->tg) {
        sm_log(opts, state, "grant-same-freq");
        return;
    }

    double now_m = now_monotonic();

    // Store VC context
    ctx->vc_freq_hz = freq;
    ctx->vc_channel = ev->channel;
    ctx->vc_tg = ev->tg;
    ctx->vc_src = ev->src;
    ctx->vc_is_tdma = is_tdma_channel(state, ev->channel);
    ctx->t_tune_m = now_m;
    ctx->t_voice_m = 0.0;

    // Clear slot activity
    for (int s = 0; s < 2; s++) {
        ctx->slots[s].voice_active = 0;
        ctx->slots[s].last_active_m = 0.0;
        ctx->slots[s].algid = 0;
        ctx->slots[s].keyid = 0;
        ctx->slots[s].tg = 0;
    }

    // Set symbol timing and modulation based on channel type.
    // Use dynamic SPS computation based on configured DSP bandwidth.
    int ted_sps;
    if (ctx->vc_is_tdma) {
        ted_sps = p25_ted_sps_for_bw(opts, 6000);
        state->samplesPerSymbol = ted_sps;
        state->symbolCenter = dsd_opts_symbol_center(ted_sps);
        state->p25_p2_active_slot = channel_slot(state, ev->channel);
        // P25P2 TDMA always uses CQPSK modulation - force QPSK mode
        // to prevent modulation auto-detect from flapping to C4FM
        state->rf_mod = 1;
    } else {
        ted_sps = p25_ted_sps_for_bw(opts, 4800);
        state->samplesPerSymbol = ted_sps;
        state->symbolCenter = dsd_opts_symbol_center(ted_sps);
        state->p25_p2_active_slot = -1;
    }

    // Tune to VC with TED SPS determined by channel type
    dsd_trunk_tuning_hook_tune_to_freq(opts, state, freq, ted_sps);
    ctx->tune_count++;
    ctx->grant_count++;

    // Optional debug: log TDMA grant context when sync debug is enabled.
    {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        const int debug_sync = (cfg && cfg->debug_sync_enable) ? 1 : 0;
        if (debug_sync && ctx->vc_is_tdma) {
            fprintf(stderr,
                    "[P25-SM] TDMA grant: ch=0x%04X freq=%ld slot=%d rf_mod=%d sps=%d center=%d ted_sps=%d "
                    "tune_count=%u grant_count=%u\n",
                    ev->channel & 0xFFFF, freq, state->p25_p2_active_slot, state->rf_mod, state->samplesPerSymbol,
                    state->symbolCenter, ted_sps, ctx->tune_count, ctx->grant_count);
        }
    }

    if (state) {
        state->p25_sm_tune_count++;
    }

    set_state(ctx, opts, state, P25_SM_TUNED, "grant");
}

// Helper to check if slot has decryption capability
static int
slot_can_decrypt(const dsd_state* state, int slot, int algid) {
    if (!state || algid == 0 || algid == 0x80) {
        return 1; // Clear or no encryption
    }
    unsigned long long key = (slot == 0) ? state->R : state->RR;
    int aes_loaded = state->aes_key_loaded[slot];
    if ((algid == 0xAA || algid == 0x81 || algid == 0x9F) && key != 0) {
        return 1;
    }
    if ((algid == 0x84 || algid == 0x89) && aes_loaded == 1) {
        return 1;
    }
    return 0;
}

static void
handle_voice_start(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, int slot, const char* why) {
    if (!ctx) {
        return;
    }

    double now_m = now_monotonic();
    int s = (slot >= 0 && slot <= 1) ? slot : 0;

    // Update slot activity
    ctx->slots[s].voice_active = 1;
    ctx->slots[s].last_active_m = now_m;

    // NOTE: Audio gating is managed by MAC_PTT/MAC_ACTIVE handlers in xcch.c,
    // ENC event handler, and ESS processing in frame.c.
    // This event just marks voice as active for state machine timing purposes.

    ctx->t_voice_m = now_m;
    sm_log(opts, state, why);
}

static void
handle_voice_end(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, int slot, const char* why, int is_explicit_end) {
    if (!ctx) {
        return;
    }

    int s = (slot >= 0 && slot <= 1) ? slot : 0;

    // Mark voice inactive but keep last_active_m for hangtime tracking
    ctx->slots[s].voice_active = 0;

    // NOTE: Audio gating is managed by MAC_END/MAC_IDLE handlers in xcch.c
    // which set p25_p2_audio_allowed[slot] = 0.

    sm_log(opts, state, why);

    // For explicit call termination (MAC_END_PTT or TDU), check if we should
    // release immediately rather than waiting for hangtime. This matches P25P1
    // behavior where LCW 0x4F (Call Termination) triggers immediate release.
    if (is_explicit_end && ctx->state == P25_SM_TUNED && opts && state) {
        // Explicitly clear audio gating for this slot - the call is terminated.
        // This is done here because xcch.c sets p25_p2_audio_allowed AFTER calling
        // p25_sm_emit_end(), so we need to clear it here to avoid false "active" state.
        state->p25_p2_audio_allowed[s] = 0;

        // For explicit end (MAC_END_PTT), this slot is done. Don't wait for ring buffer
        // to drain - the audio output will continue playing buffered samples while we
        // return to CC. The ring buffer check is only relevant for hangtime-based release.

        // For TDMA (P25P2), check the other slot - but only if it ever had call activity
        // during this VC tune. If last_active_m == 0, the other slot never received
        // PTT/ACTIVE, so we shouldn't wait for it.
        int other = s ^ 1;
        int other_ever_active = (ctx->slots[other].last_active_m > 0.0);
        int other_slot_active = 0;
        if (ctx->vc_is_tdma && other_ever_active) {
            // Other slot had activity - check if it's still actively in a call
            // (voice_active set by PTT/ACTIVE, cleared by END/IDLE)
            other_slot_active = ctx->slots[other].voice_active;
        }

        // Release if: FDMA, OR other slot never had a call, OR other slot also ended
        int can_release = !ctx->vc_is_tdma || !other_ever_active || !other_slot_active;

        if (can_release) {
            // All active slots terminated - release immediately like P25P1 Call Termination
            do_release(ctx, opts, state, "call-end");
        }
    }
}

static void
handle_cc_sync(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state) {
    if (!ctx) {
        return;
    }
    ctx->t_cc_sync_m = now_monotonic();

    if (ctx->state == P25_SM_IDLE || ctx->state == P25_SM_HUNTING) {
        set_state(ctx, opts, state, P25_SM_ON_CC, "cc-sync");
    }
}

static void
handle_enc(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const p25_sm_event_t* ev) {
    if (!ctx || !ev || !opts || !state) {
        return;
    }

    int slot = (ev->slot >= 0 && ev->slot <= 1) ? ev->slot : 0;
    int algid = ev->algid;
    int tg = ev->tg;

    // Store encryption params in slot context
    ctx->slots[slot].algid = algid;
    ctx->slots[slot].keyid = ev->keyid;
    ctx->slots[slot].tg = tg;

    // Skip lockout processing if encryption lockout is disabled
    if (opts->trunk_tune_enc_calls != 0) {
        // Still update audio gating based on decryptability
        state->p25_p2_audio_allowed[slot] = slot_can_decrypt(state, slot, algid) ? 1 : 0;
        return;
    }

    // Skip if we're not currently tuned to a voice channel
    if (ctx->state != P25_SM_TUNED) {
        return;
    }

    // Skip if stream is clear or we have a key
    if (slot_can_decrypt(state, slot, algid)) {
        state->p25_p2_audio_allowed[slot] = 1;
        return;
    }

    // Single-indication lockout: trigger immediately on encrypted stream
    sm_log(opts, state, "enc-lockout");

    // Mark TG as encrypted in group array
    if (tg > 0) {
        int idx = -1;
        int was_de = 0;
        for (unsigned int i = 0; i < state->group_tally; i++) {
            if (state->group_array[i].groupNumber == (unsigned long)tg) {
                idx = (int)i;
                break;
            }
        }
        if (idx >= 0) {
            was_de = (strcmp(state->group_array[idx].groupMode, "DE") == 0);
            if (!was_de) {
                snprintf(state->group_array[idx].groupMode, sizeof state->group_array[idx].groupMode, "%s", "DE");
            }
        } else if (state->group_tally < (unsigned)(sizeof(state->group_array) / sizeof(state->group_array[0]))) {
            state->group_array[state->group_tally].groupNumber = (uint32_t)tg;
            snprintf(state->group_array[state->group_tally].groupMode,
                     sizeof state->group_array[state->group_tally].groupMode, "%s", "DE");
            snprintf(state->group_array[state->group_tally].groupName,
                     sizeof state->group_array[state->group_tally].groupName, "%s", "ENC LO");
            state->group_tally++;
        }

        // Emit lockout event (once per TG)
        if (idx < 0 || !was_de) {
            p25_emit_enc_lockout_once(opts, state, (uint8_t)slot, tg, 0x40);
        }
    }

    // Gate audio for this slot
    state->p25_p2_audio_allowed[slot] = 0;
    p25_p2_audio_ring_reset(state, slot);

    // Clear voice activity indicator to prevent audio routing logic from
    // treating this locked-out slot as having active voice
    if (slot == 0) {
        state->dmrburstL = 0;
        // Reset voice counters to prevent stale state from affecting later calls
        state->fourv_counter[0] = 0;
        state->voice_counter[0] = 0;
        state->DMRvcL = 0;
        state->dropL = 256;
    } else {
        state->dmrburstR = 0;
        // Reset voice counters to prevent stale state from affecting later calls
        state->fourv_counter[1] = 0;
        state->voice_counter[1] = 0;
        state->DMRvcR = 0;
        state->dropR = 256;
    }

    // Check if opposite slot is active - only release if both slots are quiet
    int other = slot ^ 1;
    int other_active = ctx->slots[other].voice_active || state->p25_p2_audio_allowed[other]
                       || (state->p25_p2_audio_ring_count[other] > 0);

    if (!other_active) {
        do_release(ctx, opts, state, "enc-lockout");
    } else {
        sm_log(opts, state, "enc-lockout-slot-only");
    }
}

/* ============================================================================
 * Release to CC
 * ============================================================================ */

static void
do_release(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const char* reason) {
    if (!ctx) {
        return;
    }

    // Avoid double-return-to-CC thrash if multiple callers attempt to release at
    // nearly the same time (e.g., explicit call termination + watchdog tick).
    int expected = 0;
    if (!atomic_compare_exchange_strong(&g_p25_sm_release_lock, &expected, 1)) {
        return;
    }

    // Only do a hardware return-to-CC when we are (or believe we are) tuned to
    // a voice channel. This makes repeated release requests idempotent.
    int opts_tuned = (opts && (opts->p25_is_tuned == 1 || opts->trunk_is_tuned == 1)) ? 1 : 0;
    if (ctx->state != P25_SM_TUNED && !opts_tuned) {
        if (state) {
            state->p25_sm_force_release = 0;
        }
        atomic_store(&g_p25_sm_release_lock, 0);
        return;
    }

    // Clear any pending forced-release request; we're handling teardown now.
    if (state) {
        state->p25_sm_force_release = 0;
    }

    sm_log(opts, state, reason);

    // Clear all slot state
    for (int s = 0; s < 2; s++) {
        ctx->slots[s].voice_active = 0;
        ctx->slots[s].last_active_m = 0.0;
        ctx->slots[s].algid = 0;
        ctx->slots[s].keyid = 0;
        ctx->slots[s].tg = 0;
    }

    // Clear VC context
    ctx->vc_freq_hz = 0;
    ctx->vc_channel = 0;
    ctx->vc_tg = 0;
    ctx->vc_src = 0;
    ctx->t_tune_m = 0.0;
    ctx->t_voice_m = 0.0;

    ctx->release_count++;
    ctx->cc_return_count++;

    // P25p2 short-call robustness: flush any partially buffered audio before
    // clearing slot gates and retuning, so short transmissions that end before
    // a full superframe still produce audible output.
    if (ctx->vc_is_tdma && opts && state) {
        dsd_p25_optional_hook_p25p2_flush_partial_audio(opts, state);
    }

    // Clear legacy state fields
    if (state) {
        state->p25_p2_audio_allowed[0] = 0;
        state->p25_p2_audio_allowed[1] = 0;
        state->p25_p2_active_slot = -1;
        state->p25_vc_freq[0] = 0;
        state->p25_vc_freq[1] = 0;
        state->trunk_vc_freq[0] = 0;
        state->trunk_vc_freq[1] = 0;
        // Clear encryption state
        state->payload_algid = 0;
        state->payload_algidR = 0;
        state->payload_keyid = 0;
        state->payload_keyidR = 0;
        state->payload_miP = 0;
        state->payload_miN = 0;
        // Update release counter
        state->p25_sm_release_count++;
    }

    // Return to CC
    dsd_trunk_tuning_hook_return_to_cc(opts, state);

    // Transition to ON_CC state
    set_state(ctx, opts, state, P25_SM_ON_CC, "release->cc");

    atomic_store(&g_p25_sm_release_lock, 0);
}

/* ============================================================================
 * CC Hunting Helpers
 * ============================================================================ */

// Default hunting interval: try a new candidate every 5 seconds (aligned with op25 CC_HUNT_TIME)
#define CC_HUNT_INTERVAL_S 5.0

// Get next CC candidate (with cooldown check)
static int
next_cc_candidate(dsd_state* state, long* out_freq, double now_m) {
    return dsd_trunk_cc_candidates_next(state, now_m, out_freq);
}

// Get next LCN frequency from user-provided list
static int
next_lcn_freq(dsd_state* state, long* out_freq) {
    if (!state || !out_freq || state->lcn_freq_count <= 0) {
        return 0;
    }
    if (state->lcn_freq_roll >= state->lcn_freq_count) {
        state->lcn_freq_roll = 0;
    }
    // Skip duplicates
    if (state->lcn_freq_roll > 0) {
        long prev = state->trunk_lcn_freq[state->lcn_freq_roll - 1];
        long cur = state->trunk_lcn_freq[state->lcn_freq_roll];
        if (prev == cur) {
            state->lcn_freq_roll++;
            if (state->lcn_freq_roll >= state->lcn_freq_count) {
                state->lcn_freq_roll = 0;
            }
        }
    }
    long f = (state->lcn_freq_roll < state->lcn_freq_count) ? state->trunk_lcn_freq[state->lcn_freq_roll] : 0;
    state->lcn_freq_roll++;
    if (f != 0) {
        *out_freq = f;
        return 1;
    }
    return 0;
}

// Try tuning to next CC candidate or LCN freq
static void
try_next_cc(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, double now_m) {
    if (!ctx || !state) {
        return;
    }
    long cand = 0;
    int sps = cc_ted_sps(opts, state);

    // First try discovered CC candidates (if preference enabled)
    if (opts && opts->p25_prefer_candidates == 1 && next_cc_candidate(state, &cand, now_m)) {
        dsd_trunk_tuning_hook_tune_to_cc(opts, state, cand, sps);
        state->p25_cc_eval_freq = cand;
        state->p25_cc_eval_start_m = now_m;
        ctx->t_cc_sync_m = now_m; // Reset grace timer
        set_state(ctx, opts, state, P25_SM_ON_CC, "hunt-cand");
        sm_log(opts, state, "hunt-cand-tune");
        return;
    }

    // Fall back to user-provided LCN list
    if (next_lcn_freq(state, &cand)) {
        dsd_trunk_tuning_hook_tune_to_cc(opts, state, cand, sps);
        ctx->t_cc_sync_m = now_m; // Reset grace timer
        set_state(ctx, opts, state, P25_SM_ON_CC, "hunt-lcn");
        sm_log(opts, state, "hunt-lcn-tune");
        return;
    }

    // No candidates - stay in HUNTING and wait for CC_SYNC
}

/* ============================================================================
 * Public API - Core State Machine
 * ============================================================================ */

const char*
p25_sm_state_name(p25_sm_state_e state) {
    switch (state) {
        case P25_SM_IDLE: return "IDLE";
        case P25_SM_ON_CC: return "ON_CC";
        case P25_SM_TUNED: return "TUNED";
        case P25_SM_HUNTING: return "HUNT";
        default: return "?";
    }
}

void
p25_sm_init_ctx(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state) {
    if (!ctx) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();

    // Set defaults (aligned with op25 timing parameters)
    ctx->config.hangtime_s = 2.0;      // op25: TGID_HOLD_TIME = 2.0s
    ctx->config.grant_timeout_s = 3.0; // op25: TSYS_HOLD_TIME = 3.0s
    ctx->config.cc_grace_s = 5.0;      // op25: CC_HUNT_TIME = 5.0s

    // Override from opts if available (including zero for immediate release)
    if (opts) {
        if (opts->trunk_hangtime >= 0.0) {
            ctx->config.hangtime_s = opts->trunk_hangtime;
        }
        if (opts->p25_grant_voice_to_s > 0.0) {
            ctx->config.grant_timeout_s = opts->p25_grant_voice_to_s;
        }
    }

    // Override from runtime config (env/config)
    if (cfg && cfg->p25_hangtime_is_set) {
        ctx->config.hangtime_s = cfg->p25_hangtime_s;
    }
    if (cfg && cfg->p25_grant_timeout_is_set) {
        ctx->config.grant_timeout_s = cfg->p25_grant_timeout_s;
    }
    if (cfg && cfg->p25_cc_grace_is_set) {
        ctx->config.cc_grace_s = cfg->p25_cc_grace_s;
    }

    // Set initial state based on CC presence
    if (state && state->p25_cc_freq != 0) {
        ctx->state = P25_SM_ON_CC;
        // Sync CC timestamp from state
        if (state->last_cc_sync_time_m > 0.0) {
            ctx->t_cc_sync_m = state->last_cc_sync_time_m;
        } else {
            ctx->t_cc_sync_m = now_monotonic();
        }
        state->p25_sm_mode = DSD_P25_SM_MODE_ON_CC;
    } else {
        ctx->state = P25_SM_IDLE;
        if (state) {
            state->p25_sm_mode = DSD_P25_SM_MODE_UNKNOWN;
        }
    }

    ctx->initialized = 1;
}

void
p25_sm_event(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const p25_sm_event_t* ev) {
    if (!ctx || !ev) {
        return;
    }

    // Auto-initialize if needed
    if (!ctx->initialized) {
        p25_sm_init_ctx(ctx, opts, state);
    }

    switch (ev->type) {
        case P25_SM_EV_GRANT: handle_grant(ctx, opts, state, ev); break;

        case P25_SM_EV_PTT: handle_voice_start(ctx, opts, state, ev->slot, "ptt"); break;

        case P25_SM_EV_ACTIVE: handle_voice_start(ctx, opts, state, ev->slot, "active"); break;

        case P25_SM_EV_END:
            // MAC_END_PTT is an explicit call termination - trigger immediate release check
            handle_voice_end(ctx, opts, state, ev->slot, "end", 1);
            break;

        case P25_SM_EV_IDLE:
            // MAC_IDLE may occur during brief gaps - use hangtime, not immediate release
            handle_voice_end(ctx, opts, state, ev->slot, "idle", 0);
            break;

        case P25_SM_EV_TDU:
            // P1 terminator - explicit call end, trigger immediate release check
            handle_voice_end(ctx, opts, state, 0, "tdu", 1);
            break;

        case P25_SM_EV_CC_SYNC: handle_cc_sync(ctx, opts, state); break;

        case P25_SM_EV_VC_SYNC:
            // Voice sync - update activity timestamp
            if (ctx->state == P25_SM_TUNED) {
                ctx->t_voice_m = now_monotonic();
            }
            break;

        case P25_SM_EV_SYNC_LOST:
            // Handled in tick based on timeouts
            break;

        case P25_SM_EV_ENC: handle_enc(ctx, opts, state, ev); break;
    }
}

void
p25_sm_tick_ctx(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state) {
    if (!ctx) {
        return;
    }

    // Auto-initialize if needed
    if (!ctx->initialized) {
        p25_sm_init_ctx(ctx, opts, state);
    }

    // Check for forced release request (e.g., from encryption lockout paths)
    if (state && state->p25_sm_force_release != 0) {
        state->p25_sm_force_release = 0;
        if (ctx->state == P25_SM_TUNED) {
            do_release(ctx, opts, state, "release-forced");
        }
        return;
    }

    double now_m = now_monotonic();
    double hangtime = ctx->config.hangtime_s;
    double grant_timeout = ctx->config.grant_timeout_s;
    double cc_grace = ctx->config.cc_grace_s;

    switch (ctx->state) {
        case P25_SM_IDLE:
            // Nothing to do
            break;

        case P25_SM_ON_CC:
            // Sync CC timestamp from state if more recent
            if (state && state->last_cc_sync_time_m > ctx->t_cc_sync_m) {
                ctx->t_cc_sync_m = state->last_cc_sync_time_m;
            }
            // CC candidate evaluation cooldown: if we tuned to a candidate
            // and no CC activity appeared within the eval window, penalize
            if (state && state->p25_cc_eval_freq != 0) {
                double eval_dt = (state->p25_cc_eval_start_m > 0.0) ? (now_m - state->p25_cc_eval_start_m) : 0.0;
                double eval_window_s = 3.0;
                if (eval_dt >= eval_window_s) {
                    double cc_ts = ctx->t_cc_sync_m;
                    if (state->last_cc_sync_time_m > 0.0 && state->last_cc_sync_time_m < cc_ts) {
                        cc_ts = state->last_cc_sync_time_m;
                    }
                    int stale = (cc_ts <= 0.0) || ((now_m - cc_ts) >= eval_window_s);
                    if (stale) {
                        // Apply cooldown to the candidate
                        dsd_trunk_cc_candidates_set_cooldown(state, state->p25_cc_eval_freq, now_m + 10.0);
                    }
                    state->p25_cc_eval_freq = 0;
                    state->p25_cc_eval_start_m = 0.0;
                }
            }
            // Check for CC loss
            {
                double cc_ts = ctx->t_cc_sync_m;
                // State's timestamp takes precedence - if it's 0, CC is lost
                if (state) {
                    if (state->last_cc_sync_time_m <= 0.0) {
                        // Explicit loss - state says no CC sync
                        cc_ts = 0.0;
                    } else if (state->last_cc_sync_time_m < cc_ts) {
                        // State's timestamp is older
                        cc_ts = state->last_cc_sync_time_m;
                    }
                }
                int cc_lost = 0;
                if (cc_ts <= 0.0 && ctx->t_cc_sync_m > 0.0) {
                    // State explicitly cleared CC timestamp
                    cc_lost = 1;
                } else if (cc_ts > 0.0) {
                    double dt_cc = now_m - cc_ts;
                    if (dt_cc > cc_grace) {
                        cc_lost = 1;
                    }
                }
                if (cc_lost) {
                    set_state(ctx, opts, state, P25_SM_HUNTING, "cc-lost");
                    // Try CC candidates immediately
                    ctx->t_hunt_try_m = now_m;
                    try_next_cc(ctx, opts, state, now_m);
                }
            }
            break;

        case P25_SM_TUNED: {
            // Unified TUNED state handles: waiting for voice, active voice, and hangtime
            int has_voice = ctx->slots[0].voice_active || ctx->slots[1].voice_active;

            if (has_voice) {
                // Voice is active - update timestamp
                ctx->t_voice_m = now_m;
            } else if (ctx->t_voice_m > 0.0) {
                // Voice was active before, now in hangtime
                double dt_voice = now_m - ctx->t_voice_m;
                double effective_hangtime = hangtime;
                /* Optional: extend hangtime when P25p1 voice error is elevated to reduce VC↔CC thrash. */
                if (state && hangtime > 0.0) {
                    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                    double thr_pct = (cfg && cfg->p25p1_err_hold_pct_is_set) ? cfg->p25p1_err_hold_pct : 0.0;
                    double add_s = (cfg && cfg->p25p1_err_hold_s_is_set) ? cfg->p25p1_err_hold_s : 0.0;
                    if (thr_pct > 0.0 && add_s > 0.0 && state->p25_p1_voice_err_hist_len > 0) {
                        double avg =
                            (double)state->p25_p1_voice_err_hist_sum / (double)state->p25_p1_voice_err_hist_len;
                        if (avg >= thr_pct) {
                            effective_hangtime = hangtime + add_s;
                        }
                    }
                }
                if (dt_voice >= effective_hangtime) {
                    // Hangtime expired - release
                    do_release(ctx, opts, state, "hangtime-expired");
                }
            } else {
                // Never saw voice - check grant timeout
                if (ctx->t_tune_m > 0.0) {
                    double dt_tune = now_m - ctx->t_tune_m;
                    if (dt_tune >= grant_timeout) {
                        // No voice seen within timeout - return to CC
                        do_release(ctx, opts, state, "grant-timeout");
                    }
                }
            }
            break;
        }

        case P25_SM_HUNTING:
            // Try next CC candidate periodically
            if (ctx->t_hunt_try_m <= 0.0 || (now_m - ctx->t_hunt_try_m) >= CC_HUNT_INTERVAL_S) {
                ctx->t_hunt_try_m = now_m;
                try_next_cc(ctx, opts, state, now_m);
            }
            break;
    }

    // Age affiliation/neighbor tables (1 Hz)
    if (state) {
        p25_aff_tick(state);
        p25_ga_tick(state);
        p25_nb_tick(state);
    }
}

/* ============================================================================
 * Global Singleton
 * ============================================================================ */

static p25_sm_ctx_t g_sm_ctx;
static int g_sm_initialized = 0;

p25_sm_ctx_t*
p25_sm_get_ctx(void) {
    if (!g_sm_initialized) {
        p25_sm_init_ctx(&g_sm_ctx, NULL, NULL);
        g_sm_initialized = 1;
    }
    return &g_sm_ctx;
}

/* ============================================================================
 * Convenience Emit Functions
 * ============================================================================ */

void
p25_sm_emit(dsd_opts* opts, dsd_state* state, const p25_sm_event_t* ev) {
    p25_sm_event(p25_sm_get_ctx(), opts, state, ev);
}

void
p25_sm_emit_ptt(dsd_opts* opts, dsd_state* state, int slot) {
    p25_sm_event_t ev = p25_sm_ev_ptt(slot);
    p25_sm_event(p25_sm_get_ctx(), opts, state, &ev);
}

void
p25_sm_emit_active(dsd_opts* opts, dsd_state* state, int slot) {
    p25_sm_event_t ev = p25_sm_ev_active(slot);
    p25_sm_event(p25_sm_get_ctx(), opts, state, &ev);
}

void
p25_sm_emit_end(dsd_opts* opts, dsd_state* state, int slot) {
    p25_sm_event_t ev = p25_sm_ev_end(slot);
    p25_sm_event(p25_sm_get_ctx(), opts, state, &ev);
}

void
p25_sm_emit_idle(dsd_opts* opts, dsd_state* state, int slot) {
    p25_sm_event_t ev = p25_sm_ev_idle(slot);
    p25_sm_event(p25_sm_get_ctx(), opts, state, &ev);
}

void
p25_sm_emit_tdu(dsd_opts* opts, dsd_state* state) {
    p25_sm_event_t ev = p25_sm_ev_tdu();
    p25_sm_event(p25_sm_get_ctx(), opts, state, &ev);
}

void
p25_sm_emit_enc(dsd_opts* opts, dsd_state* state, int slot, int algid, int keyid, int tg) {
    p25_sm_event_t ev = p25_sm_ev_enc(slot, algid, keyid, tg);
    p25_sm_event(p25_sm_get_ctx(), opts, state, &ev);
}

void
p25_sm_release(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const char* reason) {
    if (!ctx) {
        ctx = p25_sm_get_ctx();
    }
    do_release(ctx, opts, state, reason ? reason : "explicit-release");
}

int
p25_sm_audio_allowed(p25_sm_ctx_t* ctx, dsd_state* state, int slot) {
    if (!ctx) {
        ctx = p25_sm_get_ctx();
    }
    if (!ctx || ctx->state != P25_SM_TUNED) {
        return 0;
    }
    if (!state) {
        return 0;
    }

    int s = (slot >= 0 && slot <= 1) ? slot : 0;
    return state->p25_p2_audio_allowed[s] ? 1 : 0;
}

void
p25_sm_update_audio_gate(p25_sm_ctx_t* ctx, dsd_state* state, int slot, int algid, int keyid) {
    if (!ctx) {
        ctx = p25_sm_get_ctx();
    }
    if (!ctx || !state) {
        return;
    }

    int s = (slot >= 0 && slot <= 1) ? slot : 0;

    // Store encryption info in slot context
    ctx->slots[s].algid = algid;
    ctx->slots[s].keyid = keyid;

    // Update audio gating in state (single source of truth)
    state->p25_p2_audio_allowed[s] = slot_can_decrypt(state, s, algid) ? 1 : 0;
}

/* ============================================================================
 * Encryption Lockout Helper
 * ============================================================================ */

void
p25_emit_enc_lockout_once(dsd_opts* opts, dsd_state* state, uint8_t slot, int tg, int svc_bits) {
    if (!opts || !state || tg <= 0) {
        return;
    }

    // Locate existing entry
    int idx = -1;
    for (unsigned int i = 0; i < state->group_tally; i++) {
        if (state->group_array[i].groupNumber == (unsigned long)tg) {
            idx = (int)i;
            break;
        }
    }

    int already_de = 0;
    if (idx >= 0) {
        already_de = (strcmp(state->group_array[idx].groupMode, "DE") == 0);
    }
    if (already_de) {
        return; // already marked; event previously emitted
    }

    // Create or update entry to mark encrypted
    if (idx < 0 && state->group_tally < (unsigned)(sizeof(state->group_array) / sizeof(state->group_array[0]))) {
        state->group_array[state->group_tally].groupNumber = (uint32_t)tg;
        snprintf(state->group_array[state->group_tally].groupMode,
                 sizeof state->group_array[state->group_tally].groupMode, "%s", "DE");
        snprintf(state->group_array[state->group_tally].groupName,
                 sizeof state->group_array[state->group_tally].groupName, "%s", "ENC LO");
        state->group_tally++;
    } else if (idx >= 0) {
        snprintf(state->group_array[idx].groupMode, sizeof state->group_array[idx].groupMode, "%s", "DE");
        if (strcmp(state->group_array[idx].groupName, "") == 0) {
            snprintf(state->group_array[idx].groupName, sizeof state->group_array[idx].groupName, "%s", "ENC LO");
        }
    }

    // Prepare per-slot context and clear encryption display variables for this slot
    if ((slot & 1) == 0) {
        state->lasttg = (uint32_t)tg;
        state->dmr_so = (uint16_t)svc_bits;
        // Clear slot 0 encryption display variables to prevent stale UI
        state->payload_algid = 0;
        state->payload_keyid = 0;
        state->payload_miP = 0;
    } else {
        state->lasttgR = (uint32_t)tg;
        state->dmr_soR = (uint16_t)svc_bits;
        // Clear slot 1 encryption display variables to prevent stale UI
        state->payload_algidR = 0;
        state->payload_keyidR = 0;
        state->payload_miN = 0;
    }
    state->gi[slot & 1] = 0;

    // Compose event text and push
    Event_History_I* eh = (state->event_history_s != NULL) ? &state->event_history_s[slot & 1] : NULL;
    if (eh) {
        snprintf(eh->Event_History_Items[0].internal_str, sizeof eh->Event_History_Items[0].internal_str,
                 "Target: %d; has been locked out; Encryption Lock Out Enabled.", tg);
        dsd_p25_optional_hook_watchdog_event_current(opts, state, (uint8_t)(slot & 1));
        if (strncmp(eh->Event_History_Items[1].internal_str, eh->Event_History_Items[0].internal_str,
                    sizeof eh->Event_History_Items[0].internal_str)
            != 0) {
            if (opts->event_out_file[0] != '\0') {
                uint8_t swrite = DSD_SYNC_IS_P25P2(state->lastsynctype) ? 1 : 0;
                dsd_p25_optional_hook_write_event_to_log_file(opts, state, (uint8_t)(slot & 1), swrite,
                                                              eh->Event_History_Items[0].event_string);
            }
            dsd_p25_optional_hook_push_event_history(eh);
            dsd_p25_optional_hook_init_event_history(eh, 0, 1);
        }
    } else if (opts && opts->verbose > 1) {
        p25_sm_log_status(opts, state, "enc-lo-skip-nohist");
    }
}

/* ============================================================================
 * Affiliation (RID) Table Helpers
 * ============================================================================ */

#define P25_AFF_TTL_SEC ((time_t)15 * 60)

static int
p25_aff_find_idx(const dsd_state* state, uint32_t rid) {
    if (!state || rid == 0) {
        return -1;
    }
    for (int i = 0; i < 256; i++) {
        if (state->p25_aff_rid[i] == rid) {
            return i;
        }
    }
    return -1;
}

static int
p25_aff_find_free(const dsd_state* state) {
    if (!state) {
        return -1;
    }
    for (int i = 0; i < 256; i++) {
        if (state->p25_aff_rid[i] == 0) {
            return i;
        }
    }
    return -1;
}

void
p25_aff_register(dsd_state* state, uint32_t rid) {
    if (!state || rid == 0) {
        return;
    }
    int idx = p25_aff_find_idx(state, rid);
    if (idx < 0) {
        idx = p25_aff_find_free(state);
        if (idx < 0) {
            time_t oldest = state->p25_aff_last_seen[0];
            int old_idx = 0;
            for (int i = 1; i < 256; i++) {
                if (state->p25_aff_last_seen[i] < oldest) {
                    oldest = state->p25_aff_last_seen[i];
                    old_idx = i;
                }
            }
            idx = old_idx;
        } else {
            state->p25_aff_count++;
        }
        state->p25_aff_rid[idx] = rid;
    }
    state->p25_aff_last_seen[idx] = time(NULL);
}

void
p25_aff_deregister(dsd_state* state, uint32_t rid) {
    if (!state || rid == 0) {
        return;
    }
    int idx = p25_aff_find_idx(state, rid);
    if (idx >= 0) {
        state->p25_aff_rid[idx] = 0;
        state->p25_aff_last_seen[idx] = 0;
        if (state->p25_aff_count > 0) {
            state->p25_aff_count--;
        }
    }
}

void
p25_aff_tick(dsd_state* state) {
    if (!state) {
        return;
    }
    time_t now = time(NULL);
    for (int i = 0; i < 256; i++) {
        if (state->p25_aff_rid[i] != 0) {
            time_t last = state->p25_aff_last_seen[i];
            if (last != 0 && (now - last) > P25_AFF_TTL_SEC) {
                state->p25_aff_rid[i] = 0;
                state->p25_aff_last_seen[i] = 0;
                if (state->p25_aff_count > 0) {
                    state->p25_aff_count--;
                }
            }
        }
    }
}

/* ============================================================================
 * Group Affiliation (RID↔TG) Table Helpers
 * ============================================================================ */

#define P25_GA_TTL_SEC ((time_t)30 * 60)

static int
p25_ga_find_idx(const dsd_state* state, uint32_t rid, uint16_t tg) {
    if (!state || rid == 0 || tg == 0) {
        return -1;
    }
    for (int i = 0; i < 512; i++) {
        if (state->p25_ga_rid[i] == rid && state->p25_ga_tg[i] == tg) {
            return i;
        }
    }
    return -1;
}

static int
p25_ga_find_free(const dsd_state* state) {
    if (!state) {
        return -1;
    }
    for (int i = 0; i < 512; i++) {
        if (state->p25_ga_rid[i] == 0 || state->p25_ga_tg[i] == 0) {
            return i;
        }
    }
    return -1;
}

void
p25_ga_add(dsd_state* state, uint32_t rid, uint16_t tg) {
    if (!state || rid == 0 || tg == 0) {
        return;
    }
    int idx = p25_ga_find_idx(state, rid, tg);
    if (idx < 0) {
        idx = p25_ga_find_free(state);
        if (idx < 0) {
            time_t oldest = state->p25_ga_last_seen[0];
            int old_idx = 0;
            for (int i = 1; i < 512; i++) {
                if (state->p25_ga_last_seen[i] < oldest) {
                    oldest = state->p25_ga_last_seen[i];
                    old_idx = i;
                }
            }
            idx = old_idx;
        } else {
            state->p25_ga_count++;
        }
        state->p25_ga_rid[idx] = rid;
        state->p25_ga_tg[idx] = tg;
    }
    state->p25_ga_last_seen[idx] = time(NULL);
}

void
p25_ga_remove(dsd_state* state, uint32_t rid, uint16_t tg) {
    if (!state || rid == 0 || tg == 0) {
        return;
    }
    int idx = p25_ga_find_idx(state, rid, tg);
    if (idx >= 0) {
        state->p25_ga_rid[idx] = 0;
        state->p25_ga_tg[idx] = 0;
        state->p25_ga_last_seen[idx] = 0;
        if (state->p25_ga_count > 0) {
            state->p25_ga_count--;
        }
    }
}

void
p25_ga_tick(dsd_state* state) {
    if (!state) {
        return;
    }
    time_t now = time(NULL);
    for (int i = 0; i < 512; i++) {
        if (state->p25_ga_rid[i] != 0 && state->p25_ga_tg[i] != 0) {
            time_t last = state->p25_ga_last_seen[i];
            if (last != 0 && (now - last) > P25_GA_TTL_SEC) {
                state->p25_ga_rid[i] = 0;
                state->p25_ga_tg[i] = 0;
                state->p25_ga_last_seen[i] = 0;
                if (state->p25_ga_count > 0) {
                    state->p25_ga_count--;
                }
            }
        }
    }
}
