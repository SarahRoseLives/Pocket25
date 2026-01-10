// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Lightweight test shim to exercise internal P25 functions without exposing
 * broad decoder headers to unit tests that lack external deps (e.g., mbelib).
 */

#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/protocol/p25/p25_vpdu.h>
#include <dsd-neo/protocol/p25/p25p1_pdu_trunking.h>
#include <dsd-neo/runtime/p25_p2_audio_ring.h>
void processTDULC(dsd_opts* opts, dsd_state* state);

// Invoke the P25p1 MBT -> MAC Identifier Update bridge and report key state.
// Returns 0 on success.
int
p25_test_mbt_iden_bridge(const unsigned char* mbt, int mbt_len, long* out_base, int* out_spac, int* out_type,
                         int* out_tdma, long* out_freq) {
    (void)mbt_len; // unused; decode inspects header fields only

    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(*opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(*state));
    if (!opts || !state) {
        free(opts);
        free(state);
        return -1;
    }

    // Run the trunking PDU decoder on provided MBT bytes
    p25_decode_pdu_trunking(opts, state, (unsigned char*)mbt);

    int iden = state->p25_chan_iden & 0xF;
    if (iden < 0 || iden > 15) {
        free(opts);
        free(state);
        return -1;
    }

    if (out_type) {
        *out_type = state->p25_chan_type[iden] & 0xF;
    }
    if (out_tdma) {
        *out_tdma = state->p25_chan_tdma[iden] & 0x1;
    }
    if (out_spac) {
        *out_spac = state->p25_chan_spac[iden];
    }
    if (out_base) {
        *out_base = state->p25_base_freq[iden];
    }
    if (out_freq) {
        // Compute a simple test channel (channel number 10 on selected iden)
        int channel = (iden << 12) | 10;
        *out_freq = process_channel_to_freq(opts, state, channel);
    }
    free(opts);
    free(state);
    return 0;
}

// Decode a single MBT PDU with pre-seeded IDEN parameters and report key fields.
// - iden/type/tdma/spac/base configure the channel table used by the frequency calculator.
// - Returns 0 on success and fills out_cc (control channel Hz), out_wacn (20-bit), out_sysid (12-bit).
int
p25_test_decode_mbt_with_iden(const unsigned char* mbt, int mbt_len, int iden, int type, int tdma, long base, int spac,
                              long* out_cc, long* out_wacn, int* out_sysid) {
    (void)mbt_len;

    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(*opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(*state));
    if (!opts || !state) {
        free(opts);
        free(state);
        return -1;
    }

    if (iden < 0 || iden > 15) {
        free(opts);
        free(state);
        return -2;
    }
    state->p25_chan_iden = iden & 0xF;
    state->p25_chan_type[iden] = type & 0xF;
    state->p25_chan_tdma[iden] = tdma & 0x1;
    state->p25_chan_spac[iden] = spac;
    state->p25_base_freq[iden] = base;

    p25_decode_pdu_trunking(opts, state, (unsigned char*)mbt);

    if (out_cc) {
        *out_cc = state->p25_cc_freq;
    }
    if (out_wacn) {
        *out_wacn = (long)state->p2_wacn;
    }
    if (out_sysid) {
        *out_sysid = state->p2_sysid;
    }
    free(opts);
    free(state);
    return 0;
}

/* (intentionally empty) */

// Lightweight wrapper to invoke the Phase 2 MAC VPDU handler from tests.
// Accepts a byte-oriented MAC buffer (up to 24 bytes) and channel type
// (0=FACCH, 1=SACCH). Emits JSON to stderr when DSD_NEO_PDU_JSON=1.
void
p25_test_process_mac_vpdu(int type, const unsigned char* mac_bytes, int mac_len) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(*opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(*state));
    if (!opts || !state) {
        free(opts);
        free(state);
        return;
    }

    unsigned long long int MAC[24] = {0};
    int n = mac_len < 24 ? mac_len : 24;
    for (int i = 0; i < n; i++) {
        MAC[i] = mac_bytes[i];
    }

    // Let the VPDU handler compute lengths and optionally emit JSON
    process_MAC_VPDU(opts, state, type, MAC);
    free(opts);
    free(state);
}

// Simplified P25p1 LDU audio gating decision helper.
// Returns 1 when audio should be allowed under the current encryption state,
// or 0 when audio should remain muted. Mirrors the policy in p25p1_ldu2.c:
//  - ALGID 0 or 0x80 (clear) => allow
//  - ALGID RC4/DES/DES-XL (0xAA/0x81/0x9F) => allow only when R != 0
//  - ALGID AES-256/AES-128 (0x84/0x89) => allow only when aes_loaded != 0
//  - Any other non-zero ALGID => mute
int
p25_test_p1_ldu_gate(int algid, unsigned long long R, int aes_loaded) {
    if (algid == 0 || algid == 0x80) {
        return 1; // clear
    }
    if ((algid == 0xAA || algid == 0x81 || algid == 0x9F)) {
        return (R != 0) ? 1 : 0;
    }
    if (algid == 0x84 || algid == 0x89) {
        return (aes_loaded != 0) ? 1 : 0;
    }
    return 0;
}

// Simplified P25p2 audio gating decision helper matching the logic in
// process_SACCH_MAC_PDU (ACTIVE/PTT handling):
//  - ALGID 0 or 0x80 (clear)
//  - RC4/DES/DES-XL (0xAA/0x81/0x9F) require non-zero key
//  - AES-256/AES-128 (0x84/0x89) require aes_loaded
int
p25_test_p2_gate(int algid, unsigned long long key, int aes_loaded) {
    if (algid == 0 || algid == 0x80) {
        return 1;
    }
    if ((algid == 0xAA || algid == 0x81 || algid == 0x9F) && key != 0ULL) {
        return 1;
    }
    if ((algid == 0x84 || algid == 0x89) && aes_loaded == 1) {
        return 1;
    }
    return 0;
}

// Compute a channel→frequency mapping with explicit iden parameters.
// If map_override > 0, preload trunk_chan_map[chan16] with that frequency to
// test direct mapping behavior.
int
p25_test_frequency_for(int iden, int type, int tdma, long base, int spac, int chan16, long map_override,
                       long* out_freq) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(*opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(*state));
    if (!opts || !state) {
        free(opts);
        free(state);
        return -1;
    }

    if (iden < 0 || iden > 15) {
        free(opts);
        free(state);
        return -1;
    }
    state->p25_chan_type[iden] = type & 0xF;
    state->p25_chan_tdma[iden] = tdma & 0x1;
    state->p25_chan_spac[iden] = spac;
    state->p25_base_freq[iden] = base;
    if (map_override > 0) {
        uint16_t c = (uint16_t)chan16;
        state->trunk_chan_map[c] = map_override;
    }
    long f = process_channel_to_freq(opts, state, chan16);
    if (out_freq) {
        *out_freq = f;
    }
    free(opts);
    free(state);
    return 0;
}

/* (P25p1 PDU data EVH test wrapper is provided in tests/) */

// Extended MAC VPDU test entry allowing LCCH flag and slot control.
void
p25_test_process_mac_vpdu_ex(int type, const unsigned char* mac_bytes, int mac_len, int is_lcch, int currentslot) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(*opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(*state));
    if (!opts || !state) {
        free(opts);
        free(state);
        return;
    }
    state->p2_is_lcch = (is_lcch != 0) ? 1 : 0;
    state->currentslot = currentslot & 1;

    unsigned long long int MAC[24] = {0};
    int n = mac_len < 24 ? mac_len : 24;
    for (int i = 0; i < n; i++) {
        MAC[i] = mac_bytes[i];
    }
    process_MAC_VPDU(opts, state, type, MAC);
    free(opts);
    free(state);
}

// Invoke MAC VPDU with a pre-seeded trunking state for tests that need
// valid channel→frequency mapping and/or trunking grant gating.
// - p25_trunk: enable trunking decisions (1 to allow grants)
// - p25_cc_freq: non-zero to satisfy grant tuning guard
// - iden/type/tdma/spac/base: seed IDEN parameters used by process_channel_to_freq
void
p25_test_invoke_mac_vpdu_with_state(const unsigned char* mac_bytes, int mac_len, int p25_trunk, long p25_cc_freq,
                                    int iden, int type, int tdma, long base, int spac) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(*opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(*state));
    if (!opts || !state) {
        free(opts);
        free(state);
        return;
    }

    opts->p25_trunk = p25_trunk ? 1 : 0;
    opts->p25_is_tuned = 0;
    opts->trunk_tune_group_calls = 1; // enable group call tuning in tests
    state->p25_cc_freq = p25_cc_freq;
    state->p25_chan_iden = iden & 0xF;
    state->p25_chan_type[state->p25_chan_iden] = type & 0xF;
    state->p25_chan_tdma[state->p25_chan_iden] = tdma & 0x1;
    state->p25_chan_spac[state->p25_chan_iden] = spac;
    state->p25_base_freq[state->p25_chan_iden] = base;
    state->p25_iden_trust[state->p25_chan_iden] = 2; // trust for tests
    state->synctype = DSD_SYNC_P25P1_POS;            // P1 FDMA context

    unsigned long long int MAC[24] = {0};
    int n = mac_len < 24 ? mac_len : 24;
    for (int i = 0; i < n; i++) {
        MAC[i] = mac_bytes[i];
    }
    process_MAC_VPDU(opts, state, 0, MAC);
    free(opts);
    free(state);
}

// Invoke MAC VPDU and capture tuned flag and VC frequency for assertions.
void
p25_test_invoke_mac_vpdu_capture(const unsigned char* mac_bytes, int mac_len, int p25_trunk, long p25_cc_freq, int iden,
                                 int type, int tdma, long base, int spac, long* out_vc0, int* out_tuned) {
    dsd_opts* opts = (dsd_opts*)calloc(1, sizeof(*opts));
    dsd_state* state = (dsd_state*)calloc(1, sizeof(*state));
    if (!opts || !state) {
        free(opts);
        free(state);
        if (out_vc0) {
            *out_vc0 = 0;
        }
        if (out_tuned) {
            *out_tuned = 0;
        }
        return;
    }

    opts->p25_trunk = p25_trunk ? 1 : 0;
    opts->p25_is_tuned = 0;
    opts->trunk_tune_group_calls = 1;
    opts->trunk_tune_private_calls = 1;
    // Tests that use this capture helper are not about ENC gating; default to
    // following encrypted calls so vendor-specific grants (without SVC bits)
    // do not get conservatively gated.
    opts->trunk_tune_enc_calls = 1;
    state->p25_cc_freq = p25_cc_freq;
    state->p25_chan_iden = iden & 0xF;
    state->p25_chan_type[state->p25_chan_iden] = type & 0xF;
    state->p25_chan_tdma[state->p25_chan_iden] = tdma & 0x1;
    state->p25_chan_spac[state->p25_chan_iden] = spac;
    state->p25_base_freq[state->p25_chan_iden] = base;
    state->p25_iden_trust[state->p25_chan_iden] = 2; // trust for tests

    unsigned long long int MAC[24] = {0};
    int n = mac_len < 24 ? mac_len : 24;
    for (int i = 0; i < n; i++) {
        MAC[i] = mac_bytes[i];
    }
    process_MAC_VPDU(opts, state, 0, MAC);

    if (out_vc0) {
        *out_vc0 = state->p25_vc_freq[0];
    }
    if (out_tuned) {
        *out_tuned = opts->p25_is_tuned;
    }
    free(opts);
    free(state);
}

/* (xcch test wrapper provided as a separate TU in tests/) */

// Test helper: emulate the early ENC lockout decision used in P25p2 SACCH/FACCH
// after two consecutive encrypted MAC_PTT indications for the same TG when
// ENC lockout is enabled. Mirrors the patched behavior:
//  - Mute only the indicated slot
//  - Release to CC only if the opposite slot is not active
// Returns 1 if release was requested via p25_sm_on_release, else 0.
int
p25_test_p2_early_enc_handle(dsd_opts* opts, dsd_state* state, int slot) {
    if (!opts || !state) {
        return 0;
    }
    int eslot = slot & 1;
    int other_audio = state->p25_p2_audio_allowed[eslot ^ 1];
    state->p25_p2_audio_allowed[eslot] = 0;
    // Mirror production behavior: flush any queued audio for this slot so
    // residual samples do not bleed into playback after gating.
    p25_p2_audio_ring_reset(state, eslot);
    if (!other_audio) {
        p25_sm_on_release(opts, state);
        return 1;
    }
    return 0;
}
