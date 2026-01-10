// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Verify C_MOVE handling: retunes only while on VC and updates to new VC.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dsd-neo/core/events.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/io/rigctl_client.h>
#include <dsd-neo/protocol/dmr/dmr_trunk_sm.h>
#include <dsd-neo/protocol/dmr/dmr_utils_api.h>

// --- Stubs for external symbols referenced by dmr_csbk.c ---

// Minimal bit converter: MSB-first across the provided bit array
uint64_t
ConvertBitIntoBytes(uint8_t* bits, uint32_t n) {
    uint64_t v = 0ULL;
    for (uint32_t i = 0; i < n; i++) {
        v = (v << 1) | (uint64_t)(bits[i] & 1);
    }
    return v;
}

void
watchdog_event_history(dsd_opts* opts, dsd_state* state, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)slot;
}

void
watchdog_event_current(dsd_opts* opts, dsd_state* state, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)slot;
}

void
watchdog_event_datacall(dsd_opts* opts, dsd_state* state, uint32_t src, uint32_t dst, char* data_string, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)src;
    (void)dst;
    (void)data_string;
    (void)slot;
}

void
rotate_symbol_out_file(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

// Rig/RTL helpers
bool
SetFreq(dsd_socket_t sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return false;
}

bool
SetModulation(dsd_socket_t sockfd, int bandwidth) {
    (void)sockfd;
    (void)bandwidth;
    return false;
}

long int
GetCurrentFreq(dsd_socket_t sockfd) {
    (void)sockfd;
    return 0;
}
struct RtlSdrContext;
struct RtlSdrContext* g_rtl_ctx = 0;

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

// SM tuner/reset stubs
void
trunk_tune_to_freq(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    (void)ted_sps;
    if (!opts || !state || freq <= 0) {
        return;
    }
    state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = freq;
    opts->trunk_is_tuned = 1;
    state->last_vc_sync_time = time(NULL);
}

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    if (opts) {
        opts->trunk_is_tuned = 0;
    }
    if (state) {
        state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = 0;
    }
}

void
dmr_reset_blocks(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

// CRC8 helper used in CSBK path (provide a trivial non-zero value)
uint8_t
crc8(uint8_t bits[], unsigned int len) {
    (void)bits;
    (void)len;
    return 0xFF;
}

// Audio drain helper referenced by tuner paths
void
dsd_drain_audio_output(dsd_opts* opts) {
    (void)opts;
}

// --- Helpers ---
static void
init_env(dsd_opts* opts, dsd_state* state) {
    memset(opts, 0, sizeof(*opts));
    memset(state, 0, sizeof(*state));
    opts->trunk_enable = 1;
    state->trunk_cc_freq = 851000000; // mock CC
}

// Compose a minimal C_MOVE CSBK + MBC absolute channel parms into bit and byte arrays
static void
build_cmove_apcn(uint8_t* bits, uint8_t* bytes, uint16_t apcn, uint16_t rx_int_mhz, uint16_t rx_step_125hz,
                 uint8_t slot) {
    memset(bits, 0, 256);
    memset(bytes, 0, 48);
    // Opcode (low 6 bits of first byte) = 57
    bytes[0] = (uint8_t)(57 & 0x3F);
    // LPCN field (bits 16..27) = 0xFFF (absolute)
    for (int i = 0; i < 12; i++) {
        bits[16 + i] = ((0xFFF >> (11 - i)) & 1);
    }
    // Slot bit (bit 28)
    bits[28] = (slot & 1);
    // MBC CDEFTYPE (bits 112..115) = 0 (absolute channel parms)
    // already zeroed
    // APCN (bits 118..129)
    for (int i = 0; i < 12; i++) {
        bits[118 + i] = ((apcn >> (11 - i)) & 1);
    }
    // RX_INT (bits 153..162) 10 bits
    for (int i = 0; i < 10; i++) {
        bits[153 + i] = ((rx_int_mhz >> (9 - i)) & 1);
    }
    // RX_STEP (bits 163..175) 13 bits
    for (int i = 0; i < 13; i++) {
        bits[163 + i] = ((rx_step_125hz >> (12 - i)) & 1);
    }
}

extern void dmr_cspdu(dsd_opts*, dsd_state*, uint8_t*, uint8_t*, uint32_t, uint32_t);

int
main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    static dsd_opts opts;
    static dsd_state state;
    init_env(&opts, &state);

    // Step 1: tune to initial VC via SM grant
    long f1 = 852000000;
    dmr_sm_emit_group_grant(&opts, &state, f1, /*lpcn*/ 0x0010, /*tg*/ 101, /*src*/ 1234);
    assert(opts.trunk_is_tuned == 1);
    assert(state.trunk_vc_freq[0] == f1);

    // Step 2: issue C_MOVE to a new absolute channel (eg. 853.500000 MHz)
    uint8_t bits[256];
    uint8_t bytes[48];
    uint16_t apcn = 0x0123;           // also learned into map
    uint16_t rx_int = 853;            // MHz
    uint16_t rx_step = 4000;          // 4000 * 125 Hz = 500 kHz
    long f2 = 853000000 + 4000 * 125; // 853.500000 MHz
    build_cmove_apcn(bits, bytes, apcn, rx_int, rx_step, /*slot*/ 0);
    dmr_cspdu(&opts, &state, bits, bytes, /*crc ok*/ 1, /*errs*/ 0);
    assert(opts.trunk_is_tuned == 1);
    assert(state.trunk_vc_freq[0] == f2);

    // Step 3: while on CC (not on VC), a C_MOVE should NOT cause a retune
    return_to_cc(&opts, &state); // sets trunk_is_tuned = 0 and clears vc freqs
    assert(opts.trunk_is_tuned == 0);
    // Build another move to 854.000000 MHz
    rx_int = 854;
    rx_step = 0;
    long f3 = 854000000;
    build_cmove_apcn(bits, bytes, apcn, rx_int, rx_step, /*slot*/ 0);
    dmr_cspdu(&opts, &state, bits, bytes, /*crc ok*/ 1, /*errs*/ 0);
    // Expect no tune while on CC
    assert(opts.trunk_is_tuned == 0);
    assert(state.trunk_vc_freq[0] == 0);

    printf("DMR_T3_CMOVE: OK\n");
    return 0;
}
