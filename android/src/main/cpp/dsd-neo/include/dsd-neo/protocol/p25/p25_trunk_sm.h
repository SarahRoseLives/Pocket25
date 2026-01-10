// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief P25 trunking state-machine interfaces and constants.
 *
 * This is the unified P25 trunking state machine. Design goals:
 *   - Single state machine for both P25P1 and P25P2
 *   - Minimal timing parameters (hangtime, grant_timeout, cc_grace)
 *   - Single timestamp-based activity tracking per slot
 *   - Unified release path with clear semantics
 *   - Event-driven transitions matching OP25's simpler model
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * High-level trunk SM mode (for UI/telemetry)
 * ============================================================================ */

typedef enum {
    DSD_P25_SM_MODE_UNKNOWN = 0,
    DSD_P25_SM_MODE_ON_CC = 1,
    DSD_P25_SM_MODE_ON_VC = 2,
    DSD_P25_SM_MODE_HANG = 3,
    DSD_P25_SM_MODE_HUNTING = 4,
    // Extended states for richer UI/telemetry across P1/P2
    DSD_P25_SM_MODE_ARMED = 5,     // tuned to VC, awaiting PTT/ACTIVE
    DSD_P25_SM_MODE_FOLLOW = 6,    // following active voice
    DSD_P25_SM_MODE_RETURNING = 7, // teardown in progress back to CC
} dsd_p25_sm_mode_e;

/* ============================================================================
 * State Machine States (4-state model aligned with OP25)
 * ============================================================================ */

typedef enum {
    P25_SM_IDLE = 0, // Not trunking or no CC known
    P25_SM_ON_CC,    // Parked on control channel, listening for grants
    P25_SM_TUNED,    // On voice channel (awaiting voice, active, or hangtime)
    P25_SM_HUNTING,  // Lost CC, searching candidates
} p25_sm_state_e;

/* ============================================================================
 * Events
 * ============================================================================ */

typedef enum {
    P25_SM_EV_GRANT = 0, // Channel grant received (channel, freq, tg, src, svc_bits)
    P25_SM_EV_PTT,       // MAC_PTT on slot
    P25_SM_EV_ACTIVE,    // MAC_ACTIVE on slot
    P25_SM_EV_END,       // MAC_END on slot
    P25_SM_EV_IDLE,      // MAC_IDLE on slot
    P25_SM_EV_TDU,       // P1 Terminator Data Unit
    P25_SM_EV_CC_SYNC,   // Control channel sync acquired
    P25_SM_EV_VC_SYNC,   // Voice channel sync acquired
    P25_SM_EV_SYNC_LOST, // Sync lost
    P25_SM_EV_ENC,       // Encryption params detected on slot (algid, keyid)
} p25_sm_event_type_e;

typedef struct {
    p25_sm_event_type_e type;
    int slot;     // 0 or 1 for TDMA, -1 for P1/N/A
    int channel;  // 16-bit channel number (for GRANT)
    long freq_hz; // Frequency in Hz (for GRANT)
    int tg;       // Talkgroup (for GRANT, 0 if individual)
    int src;      // Source RID (for GRANT)
    int dst;      // Destination RID (for individual GRANT)
    int svc_bits; // Service options (for GRANT)
    int is_group; // 1 for group grant, 0 for individual
    int algid;    // Algorithm ID (for ENC event)
    int keyid;    // Key ID (for ENC event)
} p25_sm_event_t;

/* ============================================================================
 * Configuration
 * ============================================================================ */

typedef struct {
    double hangtime_s;      // Hangtime after voice ends (default 2.0s)
    double grant_timeout_s; // Max wait for voice after grant (default 3.0s)
    double cc_grace_s;      // Wait before CC hunting (default 5.0s)
} p25_sm_config_t;

/* ============================================================================
 * Per-Slot Activity Context
 * ============================================================================ */

typedef struct {
    double last_active_m; // Monotonic timestamp of last activity (PTT/ACTIVE/voice)
    int voice_active;     // 1 if voice is currently active on this slot
    int algid;            // Current algorithm ID for this slot
    int keyid;            // Current key ID for this slot
    int tg;               // Current talkgroup for this slot
} p25_sm_slot_ctx_t;

/* ============================================================================
 * State Machine Context
 * ============================================================================ */

typedef struct {
    // Current state
    p25_sm_state_e state;

    // Configuration (cached from opts or defaults)
    p25_sm_config_t config;

    // Voice channel context (valid when state >= ARMED)
    long vc_freq_hz;
    int vc_channel;
    int vc_tg;
    int vc_src;
    int vc_is_tdma; // 1 if TDMA channel, 0 if single-carrier

    // Per-slot activity (index 0 = left/P1, index 1 = right)
    p25_sm_slot_ctx_t slots[2];

    // Timing
    double t_tune_m;     // Monotonic time of last VC tune
    double t_voice_m;    // Monotonic time of last voice activity
    double t_hangtime_m; // Monotonic time hangtime started
    double t_cc_sync_m;  // Monotonic time of last CC sync
    double t_hunt_try_m; // Monotonic time of last CC candidate attempt

    // Statistics (for debugging/UI)
    uint32_t tune_count;
    uint32_t release_count;
    uint32_t grant_count;
    uint32_t cc_return_count;

    // Initialized flag
    int initialized;
} p25_sm_ctx_t;

/* ============================================================================
 * Public API - Core State Machine
 * ============================================================================ */

/**
 * @brief Initialize the unified P25 state machine.
 *
 * Reads timing parameters from opts/env, sets initial state based on CC presence.
 *
 * @param ctx State machine context to initialize.
 * @param opts Decoder options (may be NULL for defaults).
 * @param state Decoder state (may be NULL).
 */
void p25_sm_init_ctx(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state);

/**
 * @brief Process an event and update state machine.
 *
 * This is the main entry point for all P25 signaling events.
 *
 * @param ctx State machine context.
 * @param opts Decoder options.
 * @param state Decoder state.
 * @param ev Event to process.
 */
void p25_sm_event(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const p25_sm_event_t* ev);

/**
 * @brief Periodic tick for timeout-based transitions.
 *
 * Call at ~1-10 Hz. Handles:
 *   - ARMED -> ON_CC (grant timeout)
 *   - HANGTIME -> ON_CC (hangtime expired)
 *   - ON_CC -> HUNTING (CC lost)
 *
 * @param ctx State machine context.
 * @param opts Decoder options.
 * @param state Decoder state.
 */
void p25_sm_tick_ctx(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state);

/**
 * @brief Get current state machine state.
 *
 * @param ctx State machine context.
 * @return Current state.
 */
static inline p25_sm_state_e
p25_sm_get_state(const p25_sm_ctx_t* ctx) {
    return ctx ? ctx->state : P25_SM_IDLE;
}

/**
 * @brief Get human-readable state name.
 *
 * @param state State to convert.
 * @return Static string with state name.
 */
const char* p25_sm_state_name(p25_sm_state_e state);

/**
 * @brief Access the global singleton state machine instance.
 *
 * Returns a process-global instance initialized with default callbacks.
 *
 * @return Pointer to global state machine context.
 */
p25_sm_ctx_t* p25_sm_get_ctx(void);

/**
 * @brief Trigger explicit release and return to CC.
 *
 * @param ctx State machine context.
 * @param opts Decoder options.
 * @param state Decoder state.
 * @param reason Log tag for release reason.
 */
void p25_sm_release(p25_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const char* reason);

/**
 * @brief Check if audio output is allowed for a slot.
 *
 * Centralized audio gating decision. Decoders should call this before
 * pushing audio to output buffers.
 *
 * @param ctx State machine context (NULL to use global).
 * @param state Decoder state.
 * @param slot Slot index (0 or 1, -1 for P1).
 * @return 1 if audio is allowed, 0 if muted.
 */
int p25_sm_audio_allowed(p25_sm_ctx_t* ctx, dsd_state* state, int slot);

/**
 * @brief Update audio gating for a slot based on current encryption state.
 *
 * Called when encryption parameters are received to update allow_audio.
 *
 * @param ctx State machine context.
 * @param state Decoder state.
 * @param slot Slot index.
 * @param algid Algorithm ID.
 * @param keyid Key ID.
 */
void p25_sm_update_audio_gate(p25_sm_ctx_t* ctx, dsd_state* state, int slot, int algid, int keyid);

/* ============================================================================
 * Public API - Convenience Emit Functions (use global singleton)
 * ============================================================================ */

/**
 * @brief Emit an event to the global state machine.
 *
 * Convenience function for decoders to emit events without managing context.
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 * @param ev Event to emit.
 */
void p25_sm_emit(dsd_opts* opts, dsd_state* state, const p25_sm_event_t* ev);

/**
 * @brief Emit PTT event for a slot.
 */
void p25_sm_emit_ptt(dsd_opts* opts, dsd_state* state, int slot);

/**
 * @brief Emit ACTIVE event for a slot.
 */
void p25_sm_emit_active(dsd_opts* opts, dsd_state* state, int slot);

/**
 * @brief Emit END event for a slot.
 */
void p25_sm_emit_end(dsd_opts* opts, dsd_state* state, int slot);

/**
 * @brief Emit IDLE event for a slot.
 */
void p25_sm_emit_idle(dsd_opts* opts, dsd_state* state, int slot);

/**
 * @brief Emit TDU (P1 terminator) event.
 */
void p25_sm_emit_tdu(dsd_opts* opts, dsd_state* state);

/**
 * @brief Emit ENC event for a slot (encryption params detected).
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 * @param slot Slot index (0 or 1).
 * @param algid Algorithm ID.
 * @param keyid Key ID.
 * @param tg Talkgroup associated with this call.
 */
void p25_sm_emit_enc(dsd_opts* opts, dsd_state* state, int slot, int algid, int keyid, int tg);

/* ============================================================================
 * Public API - Neighbor/CC Candidate Management
 * ============================================================================ */

/**
 * @brief Process neighbor frequency update from control channel.
 *
 * Adds frequencies to the CC candidate list for hunting.
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 * @param freqs Array of neighbor frequencies in Hz.
 * @param count Number of frequencies in array.
 */
void p25_sm_on_neighbor_update(dsd_opts* opts, dsd_state* state, const long* freqs, int count);

/**
 * @brief Get next CC candidate frequency for hunting.
 *
 * @param state Decoder state.
 * @param out_freq Output: next candidate frequency in Hz.
 * @return 1 if a candidate was found, 0 if none available.
 */
int p25_sm_next_cc_candidate(dsd_state* state, long* out_freq);

/* ============================================================================
 * Public API - Legacy Compatibility Wrappers
 * ============================================================================ */

/**
 * @brief Initialize any internal P25 trunking state.
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 */
void p25_sm_init(dsd_opts* opts, dsd_state* state);

/**
 * @brief Handle a group voice channel grant (explicit form).
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 * @param channel Voice channel number.
 * @param svc_bits Service options associated with the grant.
 * @param tg Talkgroup.
 * @param src Source RID.
 */
void p25_sm_on_group_grant(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int tg, int src);

/**
 * @brief Handle an individual (unit-to-unit/telephone) voice channel grant.
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 * @param channel Voice channel number.
 * @param svc_bits Service options associated with the grant.
 * @param dst Destination RID.
 * @param src Source RID.
 */
void p25_sm_on_indiv_grant(dsd_opts* opts, dsd_state* state, int channel, int svc_bits, int dst, int src);

/**
 * @brief Handle an explicit release/end-of-call indication.
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 */
void p25_sm_on_release(dsd_opts* opts, dsd_state* state);

/**
 * @brief Optional periodic heartbeat/tick for safety fallback.
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 */
void p25_sm_tick(dsd_opts* opts, dsd_state* state);

/* ============================================================================
 * Helper: SACCH slot mapping
 * ============================================================================ */

/**
 * @brief Convert SACCH currentslot to voice channel slot.
 *
 * P25 Phase 2 SACCH uses inverted slot mapping relative to voice frames.
 * Use this helper at SM event emission points for consistency.
 *
 * @param currentslot The currentslot value from state.
 * @return Voice channel slot index (0 or 1).
 */
static inline int
p25_sacch_to_voice_slot(int currentslot) {
    return (currentslot ^ 1) & 1;
}

/* ============================================================================
 * Helper: Create events from common scenarios
 * ============================================================================ */

static inline p25_sm_event_t
p25_sm_ev_group_grant(int channel, long freq_hz, int tg, int src, int svc_bits) {
    p25_sm_event_t ev = {0};
    ev.type = P25_SM_EV_GRANT;
    ev.slot = -1;
    ev.channel = channel;
    ev.freq_hz = freq_hz;
    ev.tg = tg;
    ev.src = src;
    ev.svc_bits = svc_bits;
    ev.is_group = 1;
    return ev;
}

static inline p25_sm_event_t
p25_sm_ev_indiv_grant(int channel, long freq_hz, int dst, int src, int svc_bits) {
    p25_sm_event_t ev = {0};
    ev.type = P25_SM_EV_GRANT;
    ev.slot = -1;
    ev.channel = channel;
    ev.freq_hz = freq_hz;
    ev.dst = dst;
    ev.src = src;
    ev.svc_bits = svc_bits;
    ev.is_group = 0;
    return ev;
}

static inline p25_sm_event_t
p25_sm_ev_ptt(int slot) {
    p25_sm_event_t ev = {0};
    ev.type = P25_SM_EV_PTT;
    ev.slot = slot;
    return ev;
}

static inline p25_sm_event_t
p25_sm_ev_active(int slot) {
    p25_sm_event_t ev = {0};
    ev.type = P25_SM_EV_ACTIVE;
    ev.slot = slot;
    return ev;
}

static inline p25_sm_event_t
p25_sm_ev_end(int slot) {
    p25_sm_event_t ev = {0};
    ev.type = P25_SM_EV_END;
    ev.slot = slot;
    return ev;
}

static inline p25_sm_event_t
p25_sm_ev_idle(int slot) {
    p25_sm_event_t ev = {0};
    ev.type = P25_SM_EV_IDLE;
    ev.slot = slot;
    return ev;
}

static inline p25_sm_event_t
p25_sm_ev_tdu(void) {
    p25_sm_event_t ev = {0};
    ev.type = P25_SM_EV_TDU;
    ev.slot = -1;
    return ev;
}

static inline p25_sm_event_t
p25_sm_ev_enc(int slot, int algid, int keyid, int tg) {
    p25_sm_event_t ev = {0};
    ev.type = P25_SM_EV_ENC;
    ev.slot = slot;
    ev.algid = algid;
    ev.keyid = keyid;
    ev.tg = tg;
    return ev;
}

/* ============================================================================
 * Patch group (P25 regroup/patch) tracking helpers
 * ============================================================================ */

/**
 * @brief Record or update a P25 regroup/patch state for a Super Group ID (SGID).
 *
 * @param state Decoder state holding patch context.
 * @param sgid Super Group ID.
 * @param is_patch 1 for two-way patch, 0 for simulselect (one-way regroup).
 * @param active 1 to activate, 0 to deactivate/clear.
 */
void p25_patch_update(dsd_state* state, int sgid, int is_patch, int active);

/**
 * @brief Compose a compact summary string for active patch SGIDs.
 *
 * Example output: "P: 069,142".
 *
 * @param state Decoder state (read-only).
 * @param out Destination buffer for summary string.
 * @param cap Capacity of destination buffer.
 * @return Length written (0 when none active).
 */
int p25_patch_compose_summary(const dsd_state* state, char* out, size_t cap);

/**
 * @brief Add a Working Group ID to an SGID entry (creates/activates if needed).
 *
 * @param state Decoder state holding patch context.
 * @param sgid Super Group ID.
 * @param wgid Working Group ID to add.
 */
void p25_patch_add_wgid(dsd_state* state, int sgid, int wgid);

/**
 * @brief Add a Working Unit ID to an SGID entry (creates/activates if needed).
 *
 * @param state Decoder state holding patch context.
 * @param sgid Super Group ID.
 * @param wuid Working Unit ID to add.
 */
void p25_patch_add_wuid(dsd_state* state, int sgid, uint32_t wuid);

/**
 * @brief Compose a detailed status string including WGID/WUID context.
 *
 * Example: "SG069[P] WG:2(0345,0789); SG142[S] U:3".
 *
 * @param state Decoder state (read-only).
 * @param out Destination buffer.
 * @param cap Capacity of destination buffer.
 * @return Length written.
 */
int p25_patch_compose_details(const dsd_state* state, char* out, size_t cap);

/**
 * @brief Remove a WGID membership or clear the entire SG record.
 *
 * @param state Decoder state holding patch context.
 * @param sgid Super Group ID.
 * @param wgid Working Group ID to remove.
 */
void p25_patch_remove_wgid(dsd_state* state, int sgid, int wgid);
/**
 * @brief Remove a WUID membership from an SG record.
 *
 * @param state Decoder state holding patch context.
 * @param sgid Super Group ID.
 * @param wuid Working Unit ID to remove.
 */
void p25_patch_remove_wuid(dsd_state* state, int sgid, uint32_t wuid);
/**
 * @brief Clear all membership and status for an SG record.
 *
 * @param state Decoder state holding patch context.
 * @param sgid Super Group ID to clear.
 */
void p25_patch_clear_sg(dsd_state* state, int sgid);

/**
 * @brief Set optional Key/Alg/SSN context for an SG.
 *
 * Values of -1 leave the existing field unchanged.
 *
 * @param state Decoder state holding patch context.
 * @param sgid Super Group ID.
 * @param key Key ID (or -1 to leave unchanged).
 * @param alg Algorithm ID (or -1 to leave unchanged).
 * @param ssn SSN ID (or -1 to leave unchanged).
 */
void p25_patch_set_kas(dsd_state* state, int sgid, int key, int alg, int ssn);

/**
 * @brief Return 1 if TG is a WGID within an active SG that is explicitly clear.
 *
 * Used to override ENC lockout when GRG commands indicate a clear operation
 * for a patch/regroup.
 *
 * @param state Decoder state (read-only).
 * @param tg Talkgroup to test.
 * @return 1 if the TG maps to a clear SG, 0 otherwise.
 */
int p25_patch_tg_key_is_clear(const dsd_state* state, int tg);
/**
 * @brief Return 1 if an SGID has explicit KEY=0 (clear) policy and is active.
 *
 * @param state Decoder state (read-only).
 * @param sgid Super Group ID to query.
 * @return 1 if active and explicitly clear; 0 otherwise.
 */
int p25_patch_sg_key_is_clear(const dsd_state* state, int sgid);

/* ============================================================================
 * Affiliation (RID) tracking
 * ============================================================================ */

/**
 * @brief Record a RID as affiliated/registered (updates last_seen or adds new entry).
 *
 * @param state Decoder state holding affiliation table.
 * @param rid Radio ID to register.
 */
void p25_aff_register(dsd_state* state, uint32_t rid);

/**
 * @brief Remove a RID from the affiliation table (explicit deregistration or aging).
 *
 * @param state Decoder state holding affiliation table.
 * @param rid Radio ID to remove.
 */
void p25_aff_deregister(dsd_state* state, uint32_t rid);

/**
 * @brief Periodic aging/cleanup of the affiliation table (call at ~1 Hz).
 *
 * @param state Decoder state holding affiliation table.
 */
void p25_aff_tick(dsd_state* state);

/* ============================================================================
 * Group Affiliation (RID ↔ TG) helpers
 * ============================================================================ */

/**
 * @brief Add a group affiliation (RID to TG).
 *
 * @param state Decoder state holding group affiliation table.
 * @param rid Radio ID.
 * @param tg Talkgroup to associate.
 */
void p25_ga_add(dsd_state* state, uint32_t rid, uint16_t tg);
/**
 * @brief Remove a group affiliation (RID to TG).
 *
 * @param state Decoder state holding group affiliation table.
 * @param rid Radio ID.
 * @param tg Talkgroup to remove.
 */
void p25_ga_remove(dsd_state* state, uint32_t rid, uint16_t tg);
/**
 * @brief Age/cleanup group affiliation entries (call at ~1 Hz).
 *
 * @param state Decoder state holding group affiliation table.
 */
void p25_ga_tick(dsd_state* state);

/**
 * @brief Emit a single encryption lockout event for a talkgroup.
 *
 * Marks the TG as encrypted (mode "DE") if not already and pushes the
 * corresponding event to history/log exactly once per TG until scrubbed.
 *
 * @param opts Decoder options.
 * @param state Decoder state.
 * @param slot 0 for FDMA/left, 1 for TDMA/right.
 * @param tg Talkgroup.
 * @param svc_bits Optional service bits (pass 0 if unknown).
 */
void p25_emit_enc_lockout_once(dsd_opts* opts, dsd_state* state, uint8_t slot, int tg, int svc_bits);

#ifdef __cplusplus
}
#endif
