// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Regression: P25p1 TDULC must not force an immediate return to the control channel.
 *
 * Some systems use TDULC to carry mid-call link control updates (e.g., LCW 0x44).
 * Returning to CC on every TDULC causes VC bouncing and missed audio.
 *
 * This test:
 *  - Puts the unified P25 trunk SM into TUNED via a synthetic group grant
 *  - Invokes processTDULC() while forcing TDULC FEC failure (no LCW dispatch)
 *  - Asserts that return_to_cc() is not called (i.e., no immediate CC bounce)
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>

void processTDULC(dsd_opts* opts, dsd_state* state);

// Strong stubs for I/O hooks to keep tests hermetic
bool
SetFreq(int sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return true;
}

bool
SetModulation(int sockfd, int bandwidth) {
    (void)sockfd;
    (void)bandwidth;
    return true;
}

struct RtlSdrContext* g_rtl_ctx = 0;

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

static int g_return_to_cc_called = 0;

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    g_return_to_cc_called++;
    if (opts) {
        opts->p25_is_tuned = 0;
        opts->trunk_is_tuned = 0;
    }
    if (state) {
        state->p25_vc_freq[0] = state->p25_vc_freq[1] = 0;
        state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = 0;
    }
}

static void
install_trunk_tuning_hooks(void) {
    dsd_trunk_tuning_hooks hooks = {0};
    hooks.return_to_cc = return_to_cc;
    dsd_trunk_tuning_hooks_set(hooks);
}

// Minimal utility used by TDULC path (MSB-first)
uint64_t
ConvertBitIntoBytes(uint8_t* BufferIn, uint32_t BitLength) {
    uint64_t v = 0;
    for (uint32_t i = 0; i < BitLength; i++) {
        v = (v << 1) | (uint64_t)(BufferIn[i] & 1);
    }
    return v;
}

// LCW path external helpers (not exercised by this test; provide no-op stubs for link)
void
apx_embedded_alias_header_phase1(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
apx_embedded_alias_blocks_phase1(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
l3h_embedded_alias_blocks_phase1(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
apx_embedded_gps(dsd_opts* opts, dsd_state* state, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)lc_bits;
}

void
nmea_harris(dsd_opts* opts, dsd_state* state, uint8_t* input, uint32_t src, int slot) {
    (void)opts;
    (void)state;
    (void)input;
    (void)src;
    (void)slot;
}

void
tait_iso7_embedded_alias_decode(dsd_opts* opts, dsd_state* state, uint8_t slot, int16_t len, uint8_t* input) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)len;
    (void)input;
}

// FEC stubs: force Reed-Solomon failure so processTDULC does not dispatch LCW
int
check_and_fix_golay_24_12(char* dodeca, char* parity, int* fixed_errors) {
    (void)dodeca;
    (void)parity;
    if (fixed_errors) {
        *fixed_errors = 0;
    }
    return 0;
}

void
encode_golay_24_12(char* data, char* parity) {
    (void)data;
    (void)parity;
}

int
check_and_fix_reedsolomon_24_12_13(char* data, char* parity) {
    (void)data;
    (void)parity;
    return 1; // irrecoverable
}

void
encode_reedsolomon_24_12_13(char* data, char* parity) {
    (void)data;
    (void)parity;
}

// Analog/sample reader stubs
void
read_dibit_update_analog_data(dsd_opts* opts, dsd_state* state, char* output, unsigned int count, int* status_count,
                              AnalogSignal* analog_signal_array, int* analog_signal_index) {
    (void)opts;
    (void)state;
    (void)status_count;
    (void)analog_signal_array;
    (void)analog_signal_index;
    memset(output, 0, count);
}

int
getDibit(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
    return 0;
}

void
contribute_to_heuristics(int rf_mod, P25Heuristics* heuristics, AnalogSignal* analog_signal_array, int count) {
    (void)rf_mod;
    (void)heuristics;
    (void)analog_signal_array;
    (void)count;
}

void
update_error_stats(P25Heuristics* heuristics, int bits, int errors) {
    (void)heuristics;
    (void)bits;
    (void)errors;
}

// TDULC word reader stubs (all zeros)
void
read_word(dsd_opts* opts, dsd_state* state, char* word, unsigned int length, int* status_count,
          AnalogSignal* analog_signal_array, int* analog_signal_index) {
    (void)opts;
    (void)state;
    (void)status_count;
    (void)analog_signal_array;
    (void)analog_signal_index;
    memset(word, 0, length);
}

void
read_golay24_parity(dsd_opts* opts, dsd_state* state, char* parity, int* status_count,
                    AnalogSignal* analog_signal_array, int* analog_signal_index) {
    (void)opts;
    (void)state;
    (void)status_count;
    (void)analog_signal_array;
    (void)analog_signal_index;
    memset(parity, 0, 12);
}

static int
expect_eq_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;

    static dsd_opts opts;
    static dsd_state state;
    install_trunk_tuning_hooks();
    memset(&opts, 0, sizeof opts);
    memset(&state, 0, sizeof state);

    // Enable trunking and allow group-call tuning
    opts.p25_trunk = 1;
    opts.trunk_tune_group_calls = 1;
    opts.trunk_tune_enc_calls = 1;
    opts.verbose = 0;

    // Seed a known CC to allow the SM to initialize in ON_CC
    state.p25_cc_freq = 851000000;

    // Minimal IDEN mapping so the synthetic grant produces a non-zero VC frequency
    int iden = 1;
    state.p25_chan_type[iden] = 1;
    state.p25_chan_tdma[iden] = 0;
    state.p25_chan_spac[iden] = 100;             // 12.5 kHz (100 * 125 Hz)
    state.p25_base_freq[iden] = 851000000L / 5L; // base in 5 Hz units

    // Initialize SM and tune to a VC via a group grant
    p25_sm_init(&opts, &state);
    int channel = (iden << 12) | 0x000A;
    p25_sm_on_group_grant(&opts, &state, channel, /*svc*/ 0, /*tg*/ 1234, /*src*/ 5678);
    rc |= expect_eq_int("tuned after grant", opts.p25_is_tuned, 1);

    // TDULC should not immediately bounce back to CC
    g_return_to_cc_called = 0;
    processTDULC(&opts, &state);
    rc |= expect_eq_int("return_to_cc not called", g_return_to_cc_called, 0);
    rc |= expect_eq_int("still tuned after TDULC", opts.p25_is_tuned, 1);

    return rc;
}
