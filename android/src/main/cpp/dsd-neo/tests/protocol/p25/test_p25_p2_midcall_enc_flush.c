// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Verify mid-call ENC transition on one slot flushes that slot's jitter
 * ring and does not affect the clear slot, and only releases to CC when
 * the opposite slot is inactive.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/trunk_tuning_hooks.h>

// Helper from shim that mirrors early ENC handling and now flushes ring
int p25_test_p2_early_enc_handle(dsd_opts* opts, dsd_state* state, int slot);

// Stubs/overrides to avoid IO/alias deps pulled by lib
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

// Capture return_to_cc calls invoked by p25_sm_on_release
static int g_return_to_cc_called = 0;
void return_to_cc(dsd_opts* opts, dsd_state* state);

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

    opts.p25_trunk = 1;
    opts.p25_is_tuned = 1;
    opts.trunk_tune_enc_calls = 0; // ENC lockout enabled

    // Pre-fill ring counts to simulate queued audio
    st.p25_p2_audio_ring_count[0] = 2;
    st.p25_p2_audio_ring_count[1] = 3;
    st.p25_p2_audio_allowed[0] = 1; // clear slot active
    st.p25_p2_audio_allowed[1] = 1; // will be gated (enc)

    // ENC on slot 1 while slot 0 has clear audio: should flush slot 1 ring only
    g_return_to_cc_called = 0;
    (void)p25_test_p2_early_enc_handle(&opts, &st, /*slot*/ 1);
    rc |= expect_eq("slot1 muted", st.p25_p2_audio_allowed[1], 0);
    rc |= expect_eq("slot1 ring flushed", st.p25_p2_audio_ring_count[1], 0);
    rc |= expect_eq("slot0 ring preserved", st.p25_p2_audio_ring_count[0], 2);
    rc |= expect_eq("no immediate release", g_return_to_cc_called, 0);

    // Now both slots idle, ENC on slot 0 should flush slot 0 and release
    st.p25_p2_audio_allowed[0] = 1; // active and will be gated
    st.p25_p2_audio_allowed[1] = 0; // other idle
    st.p25_p2_audio_ring_count[0] = 1;
    st.p25_p2_audio_ring_count[1] = 0;
    g_return_to_cc_called = 0;
    (void)p25_test_p2_early_enc_handle(&opts, &st, /*slot*/ 0);
    rc |= expect_eq("slot0 muted", st.p25_p2_audio_allowed[0], 0);
    rc |= expect_eq("slot0 ring flushed", st.p25_p2_audio_ring_count[0], 0);
    rc |= expect_eq("released to CC", g_return_to_cc_called, 1);

    return rc;
}
