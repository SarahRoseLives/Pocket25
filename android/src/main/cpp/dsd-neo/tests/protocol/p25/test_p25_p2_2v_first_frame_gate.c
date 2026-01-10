// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Validate P25 Phase 2 2V first-subframe gating: when per-slot audio is not
 * allowed (e.g., due to encryption lockout), the first AMBE subframe should
 * not be decoded. When audio is allowed, both AMBE subframes in 2V should be
 * decoded.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

// Expose the P25p2 2V handler under test
void process_2V(dsd_opts* opts, dsd_state* state);

// Provide stubs to satisfy link dependencies (rigctl and return_to_cc)
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

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

// MBE file close stubs referenced by XCCH path
void
openMbeOutFile(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
openMbeOutFileR(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
closeMbeOutFile(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
closeMbeOutFileR(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

// Misc helpers referenced by P25p2 frame path
void
rtl_stream_p25p2_err_update(int slot, int facch_ok_delta, int facch_err_delta, int sacch_ok_delta, int sacch_err_delta,
                            int aach_ok_delta) {
    (void)slot;
    (void)facch_ok_delta;
    (void)facch_err_delta;
    (void)sacch_ok_delta;
    (void)sacch_err_delta;
    (void)aach_ok_delta;
}

void
getTimeC_buf(char out[9]) {
    if (out) {
        memcpy(out, "00:00:00", 9);
    }
}

void
rotate_symbol_out_file(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
ncursesPrinter(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
watchdog_event_history(dsd_opts* opts, dsd_state* state, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)slot;
}

void
playSynthesizedVoiceFS4(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
playSynthesizedVoiceSS18(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

// Audio/playback stubs referenced by P25p1 LDU2 object (pulled in by LFSR helpers)
void
playSynthesizedVoiceMS(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
playSynthesizedVoiceSS(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
playSynthesizedVoiceFM(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
playSynthesizedVoiceFS(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

// Dibit helpers referenced from Phase 1 paths (not exercised here)
int
getDibit(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
    return 0;
}

int
getDibitWithReliability(dsd_opts* opts, dsd_state* state, uint8_t* out_reliability) {
    (void)opts;
    (void)state;
    if (out_reliability) {
        *out_reliability = 128;
    }
    return 0;
}

int
get_dibit_and_analog_signal(dsd_opts* opts, dsd_state* state, int* out_analog_signal) {
    (void)opts;
    (void)state;
    if (out_analog_signal) {
        *out_analog_signal = 0;
    }
    return 0;
}

void
skipDibit(dsd_opts* opts, dsd_state* state, int count) {
    (void)opts;
    (void)state;
    (void)count;
}

// Bit conversion helper
uint64_t
ConvertBitIntoBytes(uint8_t* BufferIn, uint32_t BitLength) {
    (void)BufferIn;
    (void)BitLength;
    return 0ULL;
}

// MBE table pointers (from mbelib) referenced by Phase 1 code
int iW[24] = {0};
int iX[24] = {0};
int iY[24] = {0};
int iZ[24] = {0};

// Alias decode helpers referenced by VPDU path (not exercised here)
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

// Interpose the MBE frame decoder to count invocations without pulling in the
// full vocoder stack. The signature must match src/core/vocoder/dsd_mbe.c.
static int g_mbe_calls = 0;

void
processMbeFrame(dsd_opts* opts, dsd_state* state, char imbe_fr[8][23], char ambe_fr[4][24], char imbe7100_fr[7][24]) {
    (void)opts;
    (void)state;
    (void)imbe_fr;
    (void)ambe_fr;
    (void)imbe7100_fr;
    g_mbe_calls++;
}

static int
expect_eq(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

static void
reset_state(dsd_opts* opts, dsd_state* st) {
    memset(opts, 0, sizeof *opts);
    memset(st, 0, sizeof *st);
    // Ensure deterministic behavior
    opts->floating_point = 0;
}

int
main(void) {
    int rc = 0;
    static dsd_opts opts;
    static dsd_state st;

    // Slot 0: audio gated off -> expect 0 MBE calls (first-subframe gating active)
    reset_state(&opts, &st);
    st.currentslot = 0;
    st.p25_p2_audio_allowed[0] = 0;
    g_mbe_calls = 0;
    process_2V(&opts, &st);
    rc |= expect_eq("slot0 gated: mbe calls", g_mbe_calls, 0);

    // Slot 0: audio allowed -> expect 2 MBE calls (both 2V subframes decoded)
    reset_state(&opts, &st);
    st.currentslot = 0;
    st.p25_p2_audio_allowed[0] = 1;
    g_mbe_calls = 0;
    process_2V(&opts, &st);
    rc |= expect_eq("slot0 allowed: mbe calls", g_mbe_calls, 2);

    // Slot 1: audio gated off -> expect 0 MBE calls
    reset_state(&opts, &st);
    st.currentslot = 1;
    st.p25_p2_audio_allowed[1] = 0;
    g_mbe_calls = 0;
    process_2V(&opts, &st);
    rc |= expect_eq("slot1 gated: mbe calls", g_mbe_calls, 0);

    // Slot 1: audio allowed -> expect 2 MBE calls
    reset_state(&opts, &st);
    st.currentslot = 1;
    st.p25_p2_audio_allowed[1] = 1;
    g_mbe_calls = 0;
    process_2V(&opts, &st);
    rc |= expect_eq("slot1 allowed: mbe calls", g_mbe_calls, 2);

    return rc;
}
