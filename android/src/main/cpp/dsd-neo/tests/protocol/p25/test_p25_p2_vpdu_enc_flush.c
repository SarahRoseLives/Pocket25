// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Validate P25p2 VPDU SVC encrypted gating flushes only the encrypted slot
 * and preserves the clear slot, and triggers release only if the other slot
 * is inactive.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_vpdu.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>

// Stubs to satisfy external references

void
unpack_byte_array_into_bit_array(uint8_t* input, uint8_t* output, int len) {
    (void)input;
    (void)output;
    (void)len;
}

void
apx_embedded_alias_header_phase2(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
apx_embedded_alias_blocks_phase2(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
l3h_embedded_alias_decode(dsd_opts* opts, dsd_state* state, uint8_t slot, int16_t len, uint8_t* input) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)len;
    (void)input;
}

void
nmea_harris(dsd_opts* opts, dsd_state* state, uint8_t* input, uint32_t src, int slot) {
    (void)opts;
    (void)state;
    (void)input;
    (void)src;
    (void)slot;
}

bool
SetFreq(int sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return false;
}

bool
SetModulation(int sockfd, int bandwidth) {
    (void)sockfd;
    (void)bandwidth;
    return false;
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
    (void)opts;
    (void)state;
    g_return_to_cc_called++;
}

static void
install_trunk_tuning_hooks(void) {
    dsd_trunk_tuning_hooks hooks = {0};
    hooks.return_to_cc = return_to_cc;
    dsd_trunk_tuning_hooks_set(hooks);
}

static int
expect_eq(const char* tag, int got, int want) {
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
    static dsd_state st;
    install_trunk_tuning_hooks();
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);

    // Trunking + ENC lockout enabled and tuned to VC
    opts.p25_trunk = 1;
    opts.p25_is_tuned = 1;
    opts.trunk_tune_enc_calls = 0;

    // Scenario 1: other slot active. ENC should gate only current slot and not release.
    st.currentslot = 0;             // so VPDU slot=0 for FACCH
    st.p25_p2_audio_allowed[0] = 1; // will be gated
    st.p25_p2_audio_allowed[1] = 1; // other slot active
    st.p25_p2_audio_ring_count[0] = 2;
    st.p25_p2_audio_ring_count[1] = 1;
    g_return_to_cc_called = 0;

    // Pre-mark TG as already DE to skip event emission branches in VPDU
    st.group_tally = 1;
    st.group_array[0].groupNumber = 0x1234;
    snprintf(st.group_array[0].groupMode, sizeof st.group_array[0].groupMode, "%s", "DE");

    unsigned long long MAC[24] = {0};
    // Group Voice Channel Message (opcode 0x01)
    MAC[1] = 0x01;
    MAC[2] = 0x40; // SVC with ENC bit set
    MAC[3] = 0x12; // TG high
    MAC[4] = 0x34; // TG low
    MAC[5] = 0x00; // SRC high
    MAC[6] = 0x00; // SRC mid
    MAC[7] = 0x01; // SRC low
    process_MAC_VPDU(&opts, &st, /*type FACCH*/ 0, MAC);

    rc |= expect_eq("slot0 muted", st.p25_p2_audio_allowed[0], 0);
    rc |= expect_eq("slot0 ring flushed", st.p25_p2_audio_ring_count[0], 0);
    rc |= expect_eq("slot1 ring kept", st.p25_p2_audio_ring_count[1], 1);
    rc |= expect_eq("no release", g_return_to_cc_called, 0);

    // Scenario 2: other slot idle. ENC should gate current slot and release to CC.
    st.currentslot = 0;
    st.p25_p2_audio_allowed[0] = 1;
    st.p25_p2_audio_allowed[1] = 0; // other idle
    st.p25_p2_audio_ring_count[0] = 1;
    st.p25_p2_audio_ring_count[1] = 0;
    g_return_to_cc_called = 0;

    process_MAC_VPDU(&opts, &st, 0, MAC);

    rc |= expect_eq("slot0 muted again", st.p25_p2_audio_allowed[0], 0);
    rc |= expect_eq("slot0 ring flushed again", st.p25_p2_audio_ring_count[0], 0);
    rc |= expect_eq("released to CC", g_return_to_cc_called, 1);

    return rc;
}
