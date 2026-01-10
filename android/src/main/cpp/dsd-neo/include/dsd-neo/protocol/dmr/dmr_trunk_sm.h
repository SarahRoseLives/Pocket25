// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief DMR trunking state-machine interfaces and constants.
 *
 * DMR Tier III trunking state machine:
 *   - Explicit 4-state model (IDLE, ON_CC, TUNED, HUNTING)
 *   - Event-driven transitions
 *   - Per-slot activity tracking with timestamps
 *   - Tick-based timeout handling
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * State Machine States
 * ============================================================================ */

typedef enum {
    DMR_SM_IDLE = 0, // Not trunking or no CC known
    DMR_SM_ON_CC,    // Parked on control channel, listening for grants
    DMR_SM_TUNED,    // On voice channel (awaiting voice, active, or hangtime)
    DMR_SM_HUNTING,  // Lost CC, searching candidates
} dmr_sm_state_e;

/* ============================================================================
 * Events
 * ============================================================================ */

typedef enum {
    DMR_SM_EV_GRANT = 0,  // Voice channel grant (group or individual)
    DMR_SM_EV_VOICE_SYNC, // Voice frame sync detected on slot
    DMR_SM_EV_DATA_SYNC,  // Data frame sync detected on slot
    DMR_SM_EV_RELEASE,    // P_CLEAR or slot termination
    DMR_SM_EV_CC_SYNC,    // Control channel sync acquired
    DMR_SM_EV_SYNC_LOST,  // Sync lost
} dmr_sm_event_type_e;

typedef struct {
    dmr_sm_event_type_e type;
    int slot;     // 0 (left/TS1) or 1 (right/TS2), -1 if N/A
    long freq_hz; // Frequency in Hz (for GRANT, 0 to resolve from LPCN)
    int lpcn;     // Logical Physical Channel Number (for GRANT)
    int tg;       // Talkgroup (for GRANT, 0 if individual)
    int src;      // Source RID
    int dst;      // Destination RID (for individual GRANT)
    int is_group; // 1 for group grant, 0 for individual
} dmr_sm_event_t;

/* ============================================================================
 * Per-Slot Activity Context
 * ============================================================================ */

typedef struct {
    int voice_active;     // 1 if voice currently active on this slot
    double last_active_m; // Monotonic timestamp of last activity
    int tg;               // Current talkgroup for this slot
} dmr_sm_slot_ctx_t;

/* ============================================================================
 * State Machine Context
 * ============================================================================ */

typedef struct {
    dmr_sm_state_e state;

    // Per-slot activity (index 0 = TS1/left, index 1 = TS2/right)
    dmr_sm_slot_ctx_t slots[2];

    // Voice channel context (valid when state == DMR_SM_TUNED)
    long vc_freq_hz;
    int vc_lpcn;
    int vc_tg;
    int vc_src;

    // Timing (monotonic only)
    double t_tune_m;    // Time of last VC tune
    double t_voice_m;   // Time of last voice activity
    double t_cc_sync_m; // Time of last CC sync

    // Configuration
    double hangtime_s;      // Hangtime after voice ends (default 2.0s)
    double grant_timeout_s; // Max wait for voice after grant (default 4.0s)
    double cc_grace_s;      // Wait before CC hunting (default 2.0s)

    // Initialized flag
    int initialized;
} dmr_sm_ctx_t;

/* ============================================================================
 * Public API - Core State Machine
 * ============================================================================ */

/**
 * @brief Initialize the DMR state machine context.
 */
void dmr_sm_init_ctx(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state);

/**
 * @brief Process an event and update state machine.
 */
void dmr_sm_event(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state, const dmr_sm_event_t* ev);

/**
 * @brief Periodic tick for timeout-based transitions.
 *
 * Call at ~1-10 Hz. Handles hangtime expiry, grant timeout, CC loss detection.
 */
void dmr_sm_tick_ctx(dmr_sm_ctx_t* ctx, dsd_opts* opts, dsd_state* state);

/**
 * @brief Get current state machine state.
 */
static inline dmr_sm_state_e
dmr_sm_get_state(const dmr_sm_ctx_t* ctx) {
    return ctx ? ctx->state : DMR_SM_IDLE;
}

/**
 * @brief Get human-readable state name.
 */
const char* dmr_sm_state_name(dmr_sm_state_e state);

/**
 * @brief Access the global singleton state machine instance.
 */
dmr_sm_ctx_t* dmr_sm_get_ctx(void);

/* ============================================================================
 * Public API - Convenience Emit Functions (use global singleton)
 * ============================================================================ */

/**
 * @brief Emit an event to the global state machine.
 */
void dmr_sm_emit(dsd_opts* opts, dsd_state* state, const dmr_sm_event_t* ev);

/**
 * @brief Emit voice sync event for a slot.
 */
void dmr_sm_emit_voice_sync(dsd_opts* opts, dsd_state* state, int slot);

/**
 * @brief Emit data sync event for a slot.
 */
void dmr_sm_emit_data_sync(dsd_opts* opts, dsd_state* state, int slot);

/**
 * @brief Emit release event.
 */
void dmr_sm_emit_release(dsd_opts* opts, dsd_state* state, int slot);

/**
 * @brief Emit CC sync event.
 */
void dmr_sm_emit_cc_sync(dsd_opts* opts, dsd_state* state);

/**
 * @brief Emit a group voice grant event.
 */
void dmr_sm_emit_group_grant(dsd_opts* opts, dsd_state* state, long freq_hz, int lpcn, int tg, int src);

/**
 * @brief Emit an individual voice grant event.
 */
void dmr_sm_emit_indiv_grant(dsd_opts* opts, dsd_state* state, long freq_hz, int lpcn, int dst, int src);

/**
 * @brief Initialize the global state machine singleton.
 */
void dmr_sm_init(dsd_opts* opts, dsd_state* state);

/**
 * @brief Periodic tick for the global state machine singleton.
 */
void dmr_sm_tick(dsd_opts* opts, dsd_state* state);

/* ============================================================================
 * Public API - Neighbor/CC Candidate Management
 * ============================================================================ */

/**
 * @brief Update neighbor/alternate control channel list.
 */
void dmr_sm_on_neighbor_update(dsd_opts* opts, dsd_state* state, const long* freqs, int count);

/**
 * @brief Fetch the next candidate CC frequency.
 */
int dmr_sm_next_cc_candidate(dsd_state* state, long* out_freq);

/* ============================================================================
 * Helper: Create events
 * ============================================================================ */

static inline dmr_sm_event_t
dmr_sm_ev_group_grant(long freq_hz, int lpcn, int tg, int src) {
    dmr_sm_event_t ev = {0};
    ev.type = DMR_SM_EV_GRANT;
    ev.slot = -1;
    ev.freq_hz = freq_hz;
    ev.lpcn = lpcn;
    ev.tg = tg;
    ev.src = src;
    ev.is_group = 1;
    return ev;
}

static inline dmr_sm_event_t
dmr_sm_ev_indiv_grant(long freq_hz, int lpcn, int dst, int src) {
    dmr_sm_event_t ev = {0};
    ev.type = DMR_SM_EV_GRANT;
    ev.slot = -1;
    ev.freq_hz = freq_hz;
    ev.lpcn = lpcn;
    ev.dst = dst;
    ev.src = src;
    ev.is_group = 0;
    return ev;
}

static inline dmr_sm_event_t
dmr_sm_ev_voice_sync(int slot) {
    dmr_sm_event_t ev = {0};
    ev.type = DMR_SM_EV_VOICE_SYNC;
    ev.slot = slot;
    return ev;
}

static inline dmr_sm_event_t
dmr_sm_ev_data_sync(int slot) {
    dmr_sm_event_t ev = {0};
    ev.type = DMR_SM_EV_DATA_SYNC;
    ev.slot = slot;
    return ev;
}

static inline dmr_sm_event_t
dmr_sm_ev_release(int slot) {
    dmr_sm_event_t ev = {0};
    ev.type = DMR_SM_EV_RELEASE;
    ev.slot = slot;
    return ev;
}

static inline dmr_sm_event_t
dmr_sm_ev_cc_sync(void) {
    dmr_sm_event_t ev = {0};
    ev.type = DMR_SM_EV_CC_SYNC;
    ev.slot = -1;
    return ev;
}

#ifdef __cplusplus
}
#endif
