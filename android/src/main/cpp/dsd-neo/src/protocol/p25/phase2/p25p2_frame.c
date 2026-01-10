// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
 * p25p2_frame.c
 * Phase 2 TDMA Frame Processing
 *
 * original copyrights for portions used below (OP25 DUID table, MAC len table)
 *
 * LWVMOBILE
 * 2022-09 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dibit.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/core/vocoder.h>
#include <dsd-neo/fec/ez.h>
#ifdef USE_RTLSDR
#include <dsd-neo/runtime/rtl_stream_metrics_hooks.h>
#endif
#include <dsd-neo/protocol/p25/p25_lfsr.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/protocol/p25/p25_xcch.h>
#include <dsd-neo/protocol/p25/p25p2_frame.h>
#include <dsd-neo/protocol/p25/p25p2_soft.h>
#include <dsd-neo/runtime/colors.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/p25_optional_hooks.h>
#include <dsd-neo/runtime/p25_p2_audio_ring.h>
#include <dsd-neo/runtime/telemetry.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(DSD_NEO_P25P2_TEST_STUB)
#define p25_sm_emit_active(opts, state, slot) ((void)0)
#define p25_sm_on_release(opts, state)        ((void)0)
#endif

static int
p25_p2_s16_frames_have_audio(short frames[18][160]) {
    for (int j = 0; j < 18; j++) {
        for (int i = 0; i < 160; i++) {
            if (frames[j][i] != 0) {
                return 1;
            }
        }
    }
    return 0;
}

// Clear per-slot audio gates, small audio rings, encryption indicators, and
// UI call banners for both logical slots. Intended for use on call teardown
// before returning to the control channel.
static void
p25_p2_teardown_call(dsd_opts* opts, dsd_state* state) {
    if (!state) {
        return;
    }
    // Flush any partial superframe worth of decoded audio so short calls
    // (or late-entry captures that end before a full superframe) still
    // produce audible output in int16 mode.
    if (opts && opts->floating_point == 0 && opts->pulse_digi_rate_out == 8000) {
        int has_l = p25_p2_s16_frames_have_audio(state->s_l4);
        int has_r = p25_p2_s16_frames_have_audio(state->s_r4);
        if (has_l || has_r) {
            // At teardown, slot gates may already be cleared by MAC_END/IDLE.
            // The s_l4/s_r4 buffers only contain decoded audio when a slot was
            // allowed at decode time, so use buffer presence as the playback
            // gate here to avoid dropping the tail of short clear calls.
            state->p25_p2_audio_allowed[0] = has_l ? 1 : 0;
            state->p25_p2_audio_allowed[1] = has_r ? 1 : 0;
            playSynthesizedVoiceSS18(opts, state);
        }
        state->voice_counter[0] = 0;
        state->voice_counter[1] = 0;
    }

    state->p25_p2_audio_allowed[0] = 0;
    state->p25_p2_audio_allowed[1] = 0;
    p25_p2_audio_ring_reset(state, -1);
    // Clear buffered short audio frames to avoid replaying stale samples on
    // subsequent short calls that never reach the normal SS18 playback path.
    memset(state->s_l4, 0, sizeof(state->s_l4));
    memset(state->s_r4, 0, sizeof(state->s_r4));
    state->p25_p2_last_mac_active[0] = 0;
    state->p25_p2_last_mac_active[1] = 0;
    state->p25_p2_last_end_ptt[0] = 0;
    state->p25_p2_last_end_ptt[1] = 0;
    state->p25_call_is_packet[0] = 0;
    state->p25_call_is_packet[1] = 0;
    state->p25_call_emergency[0] = 0;
    state->p25_call_emergency[1] = 0;
    state->p25_call_priority[0] = 0;
    state->p25_call_priority[1] = 0;
    state->payload_algid = 0;
    state->payload_keyid = 0;
    state->payload_miP = 0ULL;
    state->payload_algidR = 0;
    state->payload_keyidR = 0;
    state->payload_miN = 0ULL;
    snprintf(state->call_string[0], sizeof state->call_string[0], "%s", "                     ");
    snprintf(state->call_string[1], sizeof state->call_string[1], "%s", "                     ");
}

//DUID Look Up Table from OP25
static const int16_t duid_lookup[256] =
    {
        //128 triggers false 4V on bad signal
        0,  0,  0,  -1, 0,  -1, -1, 1,  0,  -1, -1, 4,  -1, 8,  2,  -1, 0,  -1, -1, 1,  -1, 1,  1,  1,  -1, 3,  9,  -1,
        5,  -1, -1, 1,  0,  -1, -1, 10, -1, 6,  2,  -1, -1, 3,  2,  -1, 2,  -1, 2,  2,  -1, 3,  7,  -1, 11, -1, -1, 1,
        3,  3,  -1, 3,  -1, 3,  2,  -1, 0,  -1, -1, 4,  -1, 6,  12, -1, -1, 4,  4,  4,  5,  -1, -1, 4,  -1, 13, 7,  -1,
        5,  -1, -1, 1,  5,  -1, -1, 4,  5,  5,  5,  -1, -1, 6,  7,  -1, 6,  6,  -1, 6,  14, -1, -1, 4,  -1, 6,  2,  -1,
        7,  -1, 7,  7,  -1, 6,  7,  -1, -1, 3,  7,  -1, 5,  -1, -1, 15, -1, -1, -1, 10, -1, 8,  12, -1, -1, 8,  9,  -1,
        8,  8,  -1, 8, //first value was 0 for 4V, changing to -1 for testing -- 1000 0000 (perfect 4V should be 0000 0000, so is this the correct hamming distance?)
        -1, 13, 9,  -1, 11, -1, -1, 1,  9,  -1, 9,  9,  -1, 8,  9,  -1, -1, 10, 10, 10, 11, -1, -1, 10, 14, -1, -1, 10,
        -1, 8,  2,  -1, 11, -1, -1, 10, 11, 11, 11, -1, -1, 3,  9,  -1, 11, -1, -1, 15, -1, 13, 12, -1, 12, -1, 12, 12,
        14, -1, -1, 4,  -1, 8,  12, -1, 13, 13, -1, 13, -1, 13, 12, -1, -1, 13, 9,  -1, 5,  -1, -1, 15, 14, -1, -1, 10,
        -1, 6,  12, -1, 14, 14, 14, -1, 14, -1, -1, 15, -1, 13, 7,  -1, 11, -1, -1, 15, 14, -1, -1, 15, -1, 15, 15, 15,
};

//4V and 2V deinterleave schedule
const int c0[25] = {23, 5, 22, 4, 21, 3, 20, 2, 19, 1, 18, 0, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6};

const int c1[24] = {10, 9, 8, 7, 6, 5, 22, 4, 21, 3, 20, 2, 19, 1, 18, 0, 17, 16, 15, 14, 13, 12, 11};

const int c2[12] = {3, 2, 1, 0, 10, 9, 8, 7, 6, 5, 4};

const int c3[15] = {13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

const int csubset[73] = {0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 3, 0, 0, 1, 3,
                         0, 1, 1, 3, 0, 1, 1, 3, 0, 1, 1, 3, 0, 1, 1, 3, 0, 1, 1, 3, 0, 1, 2, 3,
                         0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};

const int* w;

char ambe_fr1[4][24] = {0};
char ambe_fr2[4][24] = {0};
char ambe_fr3[4][24] = {0};
char ambe_fr4[4][24] = {0};

int ts_counter = 0;     //timeslot counter for time slots 0-11
int p2bit[4320] = {0};  //4320
int p2lbit[8640] = {0}; //bits generated by lsfr scrambler, doubling up for offset roll-over
int p2xbit[4320] = {0}; //bits xored from p2bit and p2lbit

/* Per-dibit reliability for captured 700 dibits (soft-decision support) */
uint8_t p2reliab[700] = {0};  /* reliability before descramble */
uint8_t p2xreliab[700] = {0}; /* reliability after descramble */

int dibit = 0;
int vc_counter = 0;
int framing_counter = 0;
int voice = 0; //if voice in vch 0 or vch 1

uint64_t isch = 0;
int isch_decoded = -1;
uint8_t p2_duid[8] = {0};
int16_t duid_decoded = -1;

int ess_b[2][96] = {0};  //96 bits for 4 - 24 bit ESS_B fields starting bit 168 (RS 44,16,29)
int ess_a[2][168] = {0}; //ESS_A 1 (96 bit) and 2 (72 bit) fields, starting at bit 168 and bit 266 (RS Parity)

int facch[2][156] = {0};
int facch_rs[2][114] = {0};

int sacch[2][180] = {0};
int sacch_rs[2][132] = {0};

// Reset all P25P2 frame processing global state variables.
// This must be called when tuning to a new P25P2 voice channel to clear stale
// data from the previous channel that would otherwise cause decode failures.
// The issue manifests as: first P25P2 tune works, but subsequent voice channel
// grants fail to lock with tanking EVM/SNR until retune to P25P1 control channel.
void
p25_p2_frame_reset(void) {
    // Reset counters
    ts_counter = 0;
    vc_counter = 0;
    framing_counter = 0;
    voice = 0;
    dibit = 0;

    // Reset bit buffers (stale data from previous channel causes decode failures)
    memset(p2bit, 0, sizeof(p2bit));
    memset(p2lbit, 0, sizeof(p2lbit));
    memset(p2xbit, 0, sizeof(p2xbit));

    // Reset reliability buffers (soft-decision support)
    memset(p2reliab, 0, sizeof(p2reliab));
    memset(p2xreliab, 0, sizeof(p2xreliab));

    // Reset decoded state
    isch = 0;
    isch_decoded = -1;
    memset(p2_duid, 0, sizeof(p2_duid));
    duid_decoded = -1;

    // Reset ESS buffers (stale ESS_A/ESS_B from previous channel corrupts new channel)
    memset(ess_a, 0, sizeof(ess_a));
    memset(ess_b, 0, sizeof(ess_b));

    // Reset FACCH/SACCH buffers
    memset(facch, 0, sizeof(facch));
    memset(facch_rs, 0, sizeof(facch_rs));
    memset(sacch, 0, sizeof(sacch));
    memset(sacch_rs, 0, sizeof(sacch_rs));

    // Reset AMBE frame buffers
    memset(ambe_fr1, 0, sizeof(ambe_fr1));
    memset(ambe_fr2, 0, sizeof(ambe_fr2));
    memset(ambe_fr3, 0, sizeof(ambe_fr3));
    memset(ambe_fr4, 0, sizeof(ambe_fr4));
}

//store an entire p2 superframe worth of dibits into a bit buffer
void
p2_dibit_buffer(dsd_opts* opts, dsd_state* state) {
    for (int i = 0; i < 700; i++) //4 Timeslots minus sync
    {
        uint8_t rel = 255; /* default to max reliability if buffer unavailable */

        /* Use getDibitWithReliability to capture both dibit and reliability */
        dibit = getDibitWithReliability(opts, state, &rel);

        //dibit inversion handled internally by getDibit if sync type is inverted
        p2bit[((size_t)i * 2)] = (dibit >> 1) & 1;
        p2bit[((size_t)i * 2) + 1] = (dibit & 1);

        /* Store reliability for this dibit */
        p2reliab[i] = rel;
    }
}

void
process_Frame_Scramble(dsd_opts* opts, dsd_state* state) {
    UNUSED(opts);

    //The bits of the scramble sequence corresponding to signal bits that are not scrambled or not used are discarded.
    //descramble frame scrambled by LFSR of WACN, SysID, and CC(NAC)
    unsigned long long int seed = 0;

    //below calc is the same as shifting left the required number of bits.
    seed = ((state->p2_wacn * 16777216) + (state->p2_sysid * 4096) + state->p2_cc);

    unsigned long long int bit = 1; //temp bit for storage during LFSR operation

    for (int i = 0; i < 4320; i++) {
        // External LFSR per TIA‑102 BBAC Fig. 7.1 (TDMA frame scrambler)
        // 44‑bit Fibonacci LFSR with feedback polynomial:
        //   x^44 + x^34 + x^20 + x^15 + x^9 + x^4 + 1
        // Seed composition (MSB→LSB): WACN[20] | SYSID[12] | NAC(CC)[12]

        //assign our scramble bit to the array
        p2lbit[i] = (seed >> 43) & 0x1;
        //assign same bit to position +4320 to allow for a rollover with an offset value
        p2lbit[i + 4320] = (seed >> 43) & 0x1;
        //compute our next scramble bit and shift the seed register and append bit to LSB
        bit = ((seed >> 33) ^ (seed >> 19) ^ (seed >> 14) ^ (seed >> 8) ^ (seed >> 3) ^ (seed >> 43)) & 0x1;
        seed = (seed << 1) | bit;
    }

    for (int i = 0; i < 4300; i++) {
        //offset by 20 for sync, then 360 for each ts frame off from start of superframe
        p2xbit[i] = p2bit[i] ^ p2lbit[i + 20 + (360 * state->p2_scramble_offset)];
    }

    /* Map bits back to their source dibit reliability: bit i comes from dibit i/2.
       Only the captured 700 dibits (1400 bits) have valid reliability.
       Scrambling changes bit values but not symbol quality, so we propagate
       the original per-dibit reliability to the descrambled buffer. */
    memset(p2xreliab, 0, sizeof(p2xreliab));
    for (int i = 0; i < 700; i++) {
        p2xreliab[i] = p2reliab[i];
    }
}

void
process_FACCHc(dsd_opts* opts, dsd_state* state) {
    //gather and process FACCH w/o scrambling (S-OEMI) so we know what to do with the containing data.
    for (int i = 0; i < 72; i++) {
        facch[state->currentslot][i] = p2bit[i + 2 + (ts_counter * 360)];
    }
    //skip DUID 1
    for (int i = 0; i < 62; i++) {
        facch[state->currentslot][i + 72] = p2bit[i + 76 + (ts_counter * 360)];
    }
    //skip sync
    for (int i = 0; i < 22; i++) {
        facch[state->currentslot][i + 134] = p2bit[i + 180 + (ts_counter * 360)];
    }
    //gather FACCH RS parity bits
    for (int i = 0; i < 42; i++) {
        facch_rs[state->currentslot][i] = p2bit[i + 202 + (ts_counter * 360)];
    }
    //skip DUID 3
    for (int i = 0; i < 72; i++) {
        facch_rs[state->currentslot][i + 42] = p2bit[i + 246 + (ts_counter * 360)];
    }

    //send payload and parity to ez_rs28_facch for error correction (RS(63,35), t=14)
    int ec = -2;

    if (opts->p25_p2_soft_erasure) {
        /* Use soft-decision erasures */
        int erasures[28] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 54, 55, 56, 57, 58, 59, 60, 61, 62};
        int n_erasures = p25p2_facch_soft_erasures(ts_counter, 0, erasures, 18, 10);
        ec = ez_rs28_facch_soft(facch[state->currentslot], facch_rs[state->currentslot], erasures, n_erasures);
        if (ec >= 0) {
            state->p25_p2_soft_erasure_ok++;
        }
    } else {
        ec = ez_rs28_facch(facch[state->currentslot], facch_rs[state->currentslot]);
    }

    int opcode = 0;
    opcode =
        (facch[state->currentslot][0] << 2) | (facch[state->currentslot][1] << 1) | (facch[state->currentslot][2] << 0);

    if (state->currentslot == 0) {
        state->dmr_so = opcode;
    } else {
        state->dmr_soR = opcode;
    }

    if (ec >= 0) {
        state->p25_p2_rs_facch_ok++;
        state->p25_p2_rs_facch_corr += (unsigned int)ec;
        /* Feedback: RS OK */
#ifdef USE_RTLSDR
        dsd_rtl_stream_metrics_hook_p25p2_err_update(state->currentslot, 1, 0, 0, 0, 0);
#endif
        process_FACCH_MAC_PDU(opts, state, facch[state->currentslot]);
    } else {
        state->p25_p2_rs_facch_err++;
        fprintf(stderr, " R-S ERR Fc");
        /* Feedback: RS ERR */
#ifdef USE_RTLSDR
        dsd_rtl_stream_metrics_hook_p25p2_err_update(state->currentslot, 0, 1, 0, 0, 0);
#endif
    }
}

void
process_FACCHs(dsd_opts* opts, dsd_state* state) {
    //gather and process FACCH w scrambling (S-OEMI) so we know what to do with the containing data.
    for (int i = 0; i < 72; i++) {
        facch[state->currentslot][i] = p2xbit[i + 2 + (ts_counter * 360)];
    }
    //skip DUID 1
    for (int i = 0; i < 62; i++) {
        facch[state->currentslot][i + 72] = p2xbit[i + 76 + (ts_counter * 360)];
    }
    //skip sync
    for (int i = 0; i < 22; i++) {
        facch[state->currentslot][i + 134] = p2xbit[i + 180 + (ts_counter * 360)];
    }
    //gather FACCh RS parity bits
    for (int i = 0; i < 42; i++) {
        facch_rs[state->currentslot][i] = p2xbit[i + 202 + (ts_counter * 360)];
    }
    //skip DUID 3
    for (int i = 0; i < 72; i++) {
        facch_rs[state->currentslot][i + 42] = p2xbit[i + 246 + (ts_counter * 360)];
    }

    //send payload and parity to ez_rs28_facch for error correction (RS(63,35), t=14)
    int ec = -2;

    if (opts->p25_p2_soft_erasure) {
        /* Use soft-decision erasures (scrambled buffer) */
        int erasures[28] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 54, 55, 56, 57, 58, 59, 60, 61, 62};
        int n_erasures = p25p2_facch_soft_erasures(ts_counter, 1, erasures, 18, 10);
        ec = ez_rs28_facch_soft(facch[state->currentslot], facch_rs[state->currentslot], erasures, n_erasures);
        if (ec >= 0) {
            state->p25_p2_soft_erasure_ok++;
        }
    } else {
        ec = ez_rs28_facch(facch[state->currentslot], facch_rs[state->currentslot]);
    }

    int opcode = 0;
    opcode =
        (facch[state->currentslot][0] << 2) | (facch[state->currentslot][1] << 1) | (facch[state->currentslot][2] << 0);

    if (state->currentslot == 0) {
        state->dmr_so = opcode;
    } else {
        state->dmr_soR = opcode;
    }

    if (ec >= 0) {
        state->p25_p2_rs_facch_ok++;
        state->p25_p2_rs_facch_corr += (unsigned int)ec;
        /* Feedback: RS OK */
#ifdef USE_RTLSDR
        dsd_rtl_stream_metrics_hook_p25p2_err_update(state->currentslot, 1, 0, 0, 0, 0);
#endif
        process_FACCH_MAC_PDU(opts, state, facch[state->currentslot]);
    } else {
        state->p25_p2_rs_facch_err++;
        fprintf(stderr, " R-S ERR Fs");
        /* Feedback: RS ERR */
#ifdef USE_RTLSDR
        dsd_rtl_stream_metrics_hook_p25p2_err_update(state->currentslot, 0, 1, 0, 0, 0);
#endif
    }
}

void
process_SACCHc(dsd_opts* opts, dsd_state* state) {
    //gather and process SACCH w/o scrambling (I-OEMI) so we know what to do with the containing data.
    for (int i = 0; i < 72; i++) {
        sacch[state->currentslot][i] = p2bit[i + 2 + (ts_counter * 360)];
    }
    //skip DUID 1
    for (int i = 0; i < 108; i++) {
        sacch[state->currentslot][i + 72] = p2bit[i + 76 + (ts_counter * 360)];
    }
    //start collecting parity
    for (int i = 0; i < 60; i++) {
        sacch_rs[state->currentslot][i] = p2bit[i + 184 + (ts_counter * 360)];
    }
    //skip DUID 3
    for (int i = 0; i < 72; i++) {
        sacch_rs[state->currentslot][i + 60] = p2bit[i + 246 + (ts_counter * 360)];
    }

    //send payload and parity to ez_rs28_sacch for error correction (RS(63,35), t=14)
    int ec = -2;

    if (opts->p25_p2_soft_erasure) {
        /* Use soft-decision erasures */
        int erasures[28] = {0, 1, 2, 3, 4, 57, 58, 59, 60, 61, 62};
        int n_erasures = p25p2_sacch_soft_erasures(ts_counter, 0, erasures, 11, 16);
        ec = ez_rs28_sacch_soft(sacch[0], sacch_rs[0], erasures, n_erasures);
        if (ec >= 0) {
            state->p25_p2_soft_erasure_ok++;
        }
    } else {
        ec = ez_rs28_sacch(sacch[0], sacch_rs[0]);
    }

    int opcode = 0;
    opcode =
        (sacch[state->currentslot][0] << 2) | (sacch[state->currentslot][1] << 1) | (sacch[state->currentslot][2] << 0);

    //set inverse true for SACCH
    if (state->currentslot == 0) {
        state->dmr_soR = opcode;
    } else {
        state->dmr_so = opcode;
    }

    if (ec >= 0) {
        state->p25_p2_rs_sacch_ok++;
        state->p25_p2_rs_sacch_corr += (unsigned int)ec;
        /* Feedback: RS OK */
#ifdef USE_RTLSDR
        dsd_rtl_stream_metrics_hook_p25p2_err_update(state->currentslot, 0, 0, 1, 0, 0);
#endif
        process_SACCH_MAC_PDU(opts, state, sacch[state->currentslot]);
    } else {
        state->p25_p2_rs_sacch_err++;
        fprintf(stderr, " R-S ERR Sc");
        /* Feedback: RS ERR */
#ifdef USE_RTLSDR
        dsd_rtl_stream_metrics_hook_p25p2_err_update(state->currentslot, 0, 0, 0, 1, 0);
#endif
    }
}

void
process_SACCHs(dsd_opts* opts, dsd_state* state) {
    //gather and process SACCH w scrambling (I-OEMI) so we know what to do with the containing data.
    for (int i = 0; i < 72; i++) {
        sacch[state->currentslot][i] = p2xbit[i + 2 + (ts_counter * 360)];
    }
    //skip DUID 1
    for (int i = 0; i < 108; i++) {
        sacch[state->currentslot][i + 72] = p2xbit[i + 76 + (ts_counter * 360)];
    }
    //start collecting parity
    for (int i = 0; i < 60; i++) {
        sacch_rs[state->currentslot][i] = p2xbit[i + 184 + (ts_counter * 360)];
    }
    //skip DUID 3
    for (int i = 0; i < 72; i++) {
        sacch_rs[state->currentslot][i + 60] = p2xbit[i + 246 + (ts_counter * 360)];
    }

    //send payload and parity to ez_rs28_sacch for error correction (RS(63,35), t=14)
    int ec = -2;

    if (opts->p25_p2_soft_erasure) {
        /* Use soft-decision erasures (scrambled buffer) */
        int erasures[28] = {0, 1, 2, 3, 4, 57, 58, 59, 60, 61, 62};
        int n_erasures = p25p2_sacch_soft_erasures(ts_counter, 1, erasures, 11, 16);
        ec = ez_rs28_sacch_soft(sacch[0], sacch_rs[0], erasures, n_erasures);
        if (ec >= 0) {
            state->p25_p2_soft_erasure_ok++;
        }
    } else {
        ec = ez_rs28_sacch(sacch[0], sacch_rs[0]);
    }

    int opcode = 0;
    opcode =
        (sacch[state->currentslot][0] << 2) | (sacch[state->currentslot][1] << 1) | (sacch[state->currentslot][2] << 0);

    //set inverse true for SACCH
    if (state->currentslot == 0) {
        state->dmr_soR = opcode;
    } else {
        state->dmr_so = opcode;
    }

    if (ec >= 0) {
        state->p25_p2_rs_sacch_ok++;
        state->p25_p2_rs_sacch_corr += (unsigned int)ec;
        /* Feedback: RS OK */
#ifdef USE_RTLSDR
        dsd_rtl_stream_metrics_hook_p25p2_err_update(state->currentslot, 0, 0, 1, 0, 0);
#endif
        process_SACCH_MAC_PDU(opts, state, sacch[state->currentslot]);
    } else {
        state->p25_p2_rs_sacch_err++;
        fprintf(stderr, " R-S ERR Ss");
        /* Feedback: RS ERR */
#ifdef USE_RTLSDR
        dsd_rtl_stream_metrics_hook_p25p2_err_update(state->currentslot, 0, 0, 0, 1, 0);
#endif
    }
}

void
process_ISCH(dsd_opts* opts, dsd_state* state) {
    UNUSED(opts);

    isch = 0;
    for (int i = 0; i < 40; i++) {
        isch = isch << 1;
        isch = isch | p2bit[i + 320 + (360 * framing_counter)];
    }

    if (isch == 0x575D57F7FF) //S-ISCH frame sync, pass;
    {
        //do nothing
    } else {
        isch_decoded = isch_lookup(isch);

        if (isch_decoded > -1) {
            int uf_count = isch_decoded & 0x3;
            int free = (isch_decoded >> 2) & 0x1;
            int isch_loc = (isch_decoded >> 3) & 0x3;
            int chan_num = (isch_decoded >> 5) & 0x3;
            UNUSED2(uf_count, free);
            state->p2_vch_chan_num = chan_num;

            //relative position to the only chan 1 we should see
            if (chan_num == 1 && isch_loc == 0) {
                state->p2_scramble_offset = 12 - framing_counter;
            } else if (chan_num == 1 && isch_loc == 1) {
                state->p2_scramble_offset = 4 - framing_counter;
            } else if (chan_num == 1 && isch_loc == 2) {
                state->p2_scramble_offset = 8 - framing_counter;
            }

        } else {
            //if -2(no return value) or -1(fec error)
        }
    }

    isch_decoded = -1; //reset to bad value after running
}

void
process_4V(dsd_opts* opts, dsd_state* state) {

    w = csubset;
    int b = 0;
    int q = 0;
    int r = 0;
    int s = 0;
    int t = 0;

    // SM event: ACTIVE on current slot - only emit if audio is allowed for this
    // slot (clear or decryptable). This prevents encrypted/undecryptable frames
    // from keeping the SM alive indefinitely and defeating grant timeout.
    if (state) {
        int slot = state->currentslot & 1;
        if (state->p25_p2_audio_allowed[slot]) {
            p25_sm_emit_active(opts, state, slot);
            // Mark recent voice only when audio is actually allowed
            state->last_vc_sync_time = time(NULL);
            state->last_vc_sync_time_m = dsd_time_now_monotonic_s();
        }
    }
    for (int x = 0; x < 72; x++) {
        int ww = *w;
        if (ww == 0) {
            b = c0[q];
            q++;
        }
        if (ww == 1) {
            b = c1[r];
            r++;
        }
        if (ww == 2) {
            b = c2[s];
            s++;
        }
        if (ww == 3) {
            b = c3[t];
            t++;
        }

        if (*w >= 0 && *w < 4 && b >= 0 && b < 24) {
            ambe_fr1[*w][b] = p2xbit[x + 2 + vc_counter];
            ambe_fr2[*w][b] = p2xbit[x + 76 + vc_counter];
            ambe_fr3[*w][b] = p2xbit[x + 172 + vc_counter];
            ambe_fr4[*w][b] = p2xbit[x + 246 + vc_counter];
        }
        w++;
    }

    //collect our ESS_B fragments
    for (int i = 0; i < 24; i++) {
        state->ess_b[state->currentslot][i + (state->fourv_counter[state->currentslot] * 24)] =
            p2xbit[i + 148 + vc_counter];
    }

    state->fourv_counter[state->currentslot]++;

    //sanity check, reset if greater than 3 (bad signal or tuned away)
    if (state->fourv_counter[state->currentslot] > 3) {
        state->fourv_counter[state->currentslot] = 0;
    }

    if (opts->payload == 1) {
        fprintf(stderr, "\n");
    }

    //unsure of the best location for these counter resets
    if (state->voice_counter[0] >= 18) {
        state->voice_counter[0] = 0;
    }

    if (state->voice_counter[1] >= 18) {
        state->voice_counter[1] = 0;
    }

    // Gate before decode to avoid spurious/stuttery output when audio not allowed
    if (state->p25_p2_audio_allowed[state->currentslot]) {
        processMbeFrame(opts, state, NULL, ambe_fr1, NULL);
        if (state->currentslot == 0) {
            memcpy(state->f_l4[0], state->audio_out_temp_buf, sizeof(state->audio_out_temp_buf));
            memcpy(state->s_l4[(state->voice_counter[0]++) % 18], state->s_l, sizeof(state->s_l));
            memcpy(state->s_l4u[0], state->s_lu, sizeof(state->s_lu));
            // Push into small jitter buffer (slot 0)
            p25_p2_audio_ring_push(state, 0, state->f_l4[0]);
        } else {
            memcpy(state->f_r4[0], state->audio_out_temp_bufR, sizeof(state->audio_out_temp_bufR));
            memcpy(state->s_r4[(state->voice_counter[1]++) % 18], state->s_r, sizeof(state->s_r));
            memcpy(state->s_r4u[0], state->s_ru, sizeof(state->s_ru));
            // Push into small jitter buffer (slot 1)
            p25_p2_audio_ring_push(state, 1, state->f_r4[0]);
        }
    } else {
        // Not allowed: zero both float and short buffers to prevent stale
        // encrypted audio from leaking into SS18 mixer path
        if (state->currentslot == 0) {
            memset(state->f_l4[0], 0, sizeof(state->f_l4[0]));
            memset(state->s_l4[(state->voice_counter[0]++) % 18], 0, sizeof(state->s_l4[0]));
        } else {
            memset(state->f_r4[0], 0, sizeof(state->f_r4[0]));
            memset(state->s_r4[(state->voice_counter[1]++) % 18], 0, sizeof(state->s_r4[0]));
        }
    }

    if (state->p25_p2_audio_allowed[state->currentslot]) {
        processMbeFrame(opts, state, NULL, ambe_fr2, NULL);
        if (state->currentslot == 0) {
            memcpy(state->f_l4[1], state->audio_out_temp_buf, sizeof(state->audio_out_temp_buf));
            memcpy(state->s_l4[(state->voice_counter[0]++) % 18], state->s_l, sizeof(state->s_l));
            memcpy(state->s_l4u[1], state->s_lu, sizeof(state->s_lu));
        } else {
            memcpy(state->f_r4[1], state->audio_out_temp_bufR, sizeof(state->audio_out_temp_bufR));
            memcpy(state->s_r4[(state->voice_counter[1]++) % 18], state->s_r, sizeof(state->s_r));
            memcpy(state->s_r4u[1], state->s_ru, sizeof(state->s_ru));
        }
    } else {
        if (state->currentslot == 0) {
            memset(state->f_l4[1], 0, sizeof(state->f_l4[1]));
            memset(state->s_l4[(state->voice_counter[0]++) % 18], 0, sizeof(state->s_l4[0]));
        } else {
            memset(state->f_r4[1], 0, sizeof(state->f_r4[1]));
            memset(state->s_r4[(state->voice_counter[1]++) % 18], 0, sizeof(state->s_r4[0]));
        }
    }

    if (state->p25_p2_audio_allowed[state->currentslot]) {
        processMbeFrame(opts, state, NULL, ambe_fr3, NULL);
        if (state->currentslot == 0) {
            memcpy(state->f_l4[2], state->audio_out_temp_buf, sizeof(state->audio_out_temp_buf));
            memcpy(state->s_l4[(state->voice_counter[0]++) % 18], state->s_l, sizeof(state->s_l));
            memcpy(state->s_l4u[2], state->s_lu, sizeof(state->s_lu));
        } else {
            memcpy(state->f_r4[2], state->audio_out_temp_bufR, sizeof(state->audio_out_temp_bufR));
            memcpy(state->s_r4[(state->voice_counter[1]++) % 18], state->s_r, sizeof(state->s_r));
            memcpy(state->s_r4u[2], state->s_ru, sizeof(state->s_ru));
        }
    } else {
        if (state->currentslot == 0) {
            memset(state->f_l4[2], 0, sizeof(state->f_l4[2]));
            memset(state->s_l4[(state->voice_counter[0]++) % 18], 0, sizeof(state->s_l4[0]));
        } else {
            memset(state->f_r4[2], 0, sizeof(state->f_r4[2]));
            memset(state->s_r4[(state->voice_counter[1]++) % 18], 0, sizeof(state->s_r4[0]));
        }
    }

    if (state->p25_p2_audio_allowed[state->currentslot]) {
        processMbeFrame(opts, state, NULL, ambe_fr4, NULL);
        if (state->currentslot == 0) {
            memcpy(state->f_l4[3], state->audio_out_temp_buf, sizeof(state->audio_out_temp_buf));
            memcpy(state->s_l4[(state->voice_counter[0]++) % 18], state->s_l, sizeof(state->s_l));
            memcpy(state->s_l4u[3], state->s_lu, sizeof(state->s_lu));
        } else {
            memcpy(state->f_r4[3], state->audio_out_temp_bufR, sizeof(state->audio_out_temp_bufR));
            memcpy(state->s_r4[(state->voice_counter[1]++) % 18], state->s_r, sizeof(state->s_r));
            memcpy(state->s_r4u[3], state->s_ru, sizeof(state->s_ru));
        }
    } else {
        if (state->currentslot == 0) {
            memset(state->f_l4[3], 0, sizeof(state->f_l4[3]));
            memset(state->s_l4[(state->voice_counter[0]++) % 18], 0, sizeof(state->s_l4[0]));
        } else {
            memset(state->f_r4[3], 0, sizeof(state->f_r4[3]));
            memset(state->s_r4[(state->voice_counter[1]++) % 18], 0, sizeof(state->s_r4[0]));
        }
    }
}

void
process_ESS(dsd_opts* opts, dsd_state* state) {
    //collect and process ESS info (MI, Key ID, Alg ID)
    //hand over to (RS 44,16,29) decoder to receive ESS values

    int payload[96] = {0}; //local storage for ESS_A and ESS_B arrays
    for (int i = 0; i < 96; i++) {
        payload[i] = state->ess_b[state->currentslot][i];
    }

    int parity[168] = {0};
    for (int i = 0; i < 168; i++) {
        parity[i] = ess_a[state->currentslot][i];
    }

    int ec = 69;
    ec = ez_rs28_ess(payload, parity);

    /* If hard decode failed and soft-decision is enabled, try with erasures */
    if (ec < 0 && opts->p25_p2_soft_erasure) {
        /* Reload payload and parity (hard decode may have corrupted them) */
        for (int i = 0; i < 96; i++) {
            payload[i] = state->ess_b[state->currentslot][i];
        }
        for (int i = 0; i < 168; i++) {
            parity[i] = ess_a[state->currentslot][i];
        }

        /* Build erasure list from reliability info.
         * ESS_B (payload) is collected across 4V frames, ESS_A (parity) from 2V.
         * Use ts_counter=0 as base since ESS spans multiple frames.
         */
        int erasures[44];
        int n_erasures = p25p2_ess_soft_erasures(0, 1, erasures, 0, 10);      /* 4V payload */
        n_erasures = p25p2_ess_soft_erasures(0, 0, erasures, n_erasures, 10); /* 2V parity */

        if (n_erasures > 0) {
            ec = ez_rs28_ess_soft(payload, parity, erasures, n_erasures);
            if (ec >= 0) {
                state->p25_p2_soft_ess_ok++;
            }
        }
    }

    int algid = 0;
    for (short i = 0; i < 8; i++) {
        algid = algid << 1;
        algid = algid | payload[i];
    }

    unsigned long long int essb_hex1 = 0;
    unsigned long long int essb_hex2 = 0;
    for (int i = 0; i < 32; i++) {
        essb_hex1 = essb_hex1 << 1;
        essb_hex1 = essb_hex1 | payload[i];
    }
    for (int i = 0; i < 64; i++) {
        essb_hex2 = essb_hex2 << 1;
        essb_hex2 = essb_hex2 | payload[i + 32];
    }
    fprintf(stderr, "%s", KYEL);

    if (opts->payload == 1) {
        // fprintf (stderr, "\n");
        fprintf(stderr, " VCH %d - ESS_B %08llX%016llX ERR = %02d", state->currentslot + 1, essb_hex1, essb_hex2, ec);
    }

    if (ec >= 0 && ec < 15) //corrected up to 14 errors and not -1 failure
    {
        state->p25_p2_rs_ess_ok++;
        state->p25_p2_rs_ess_corr += (unsigned int)ec;
        if (state->currentslot == 0) {
            state->payload_algid = (essb_hex1 >> 24) & 0xFF;
            state->payload_keyid = (essb_hex1 >> 8) & 0xFFFF;
            state->payload_miP = ((essb_hex1 & 0xFF) << 56) | ((essb_hex2 & 0xFFFFFFFFFFFFFF00) >> 8);
            // Fallback: if SACCH/FACCH MAC_PTT was missed but ESS indicates
            // clear or decryptable audio, allow this slot's audio now.
            if (state->p25_p2_audio_allowed[0] == 0) {
                // ESS-driven enablement: permit during an active call context
                // OR when we are in the middle of a voice frame (2V/4V) on
                // this path. Using the local 'voice' indicator allows opening
                // gates at the very start of a call before MAC_PTT/ACTIVE
                // arrives, reducing missed first syllables, while still
                // protecting against stale re-enables after teardown.
                int in_call = ((state->dmrburstL >= 20 && state->dmrburstL <= 22) || voice);
                int alg = state->payload_algid;
                int allow = (in_call
                             && ((alg == 0 || alg == 0x80)
                                 || (((alg == 0xAA || alg == 0x81 || alg == 0x9F) && state->R != 0)
                                     || ((alg == 0x84 || alg == 0x89) && state->aes_key_loaded[0] == 1))))
                                ? 1
                                : 0;
                if (allow) {
                    state->p25_p2_audio_allowed[0] = 1;
                }
            }
            if (state->payload_algid != 0x80 && state->payload_algid != 0x0) {
                fprintf(stderr, "\n");
                fprintf(stderr, " VCH 1 -");
                fprintf(stderr, " ALG ID: 0x%02X", state->payload_algid);
                fprintf(stderr, " KEY ID: 0x%04X", state->payload_keyid);
                fprintf(stderr, " MI: 0x%016llX", state->payload_miP);
                fprintf(stderr, " ESSB");

                if (state->R != 0 && state->payload_algid == 0xAA) {
                    fprintf(stderr, " Key 0x%010llX", state->R);
                }
                if (state->R != 0 && state->payload_algid == 0x81) {
                    fprintf(stderr, " Key 0x%016llX", state->R);
                }
                if ((state->payload_algid == 0x84 || state->payload_algid == 0x89) && state->aes_key_loaded[0] == 1) {
                    fprintf(stderr, "\n ");
                    fprintf(stderr, "Key: %016llX %016llX ", state->A1[0], state->A2[0]);
                    if (state->payload_algid == 0x84) {
                        fprintf(stderr, "%016llX %016llX", state->A3[0], state->A4[0]);
                    }
                    // opts->unmute_encrypted_p25 = 1; //needed?
                }

                //expand 64-bit MI to 128-bit for AES
                if (state->payload_algid == 0x84 || state->payload_algid == 0x89) {
                    LFSR128(state);
                    // fprintf (stderr, "\n");
                }
            }
        }
        if (state->currentslot == 1) {
            state->payload_algidR = (essb_hex1 >> 24) & 0xFF;
            state->payload_keyidR = (essb_hex1 >> 8) & 0xFFFF;
            state->payload_miN = ((essb_hex1 & 0xFF) << 56) | ((essb_hex2 & 0xFFFFFFFFFFFFFF00) >> 8);
            // Fallback: if SACCH/FACCH MAC_PTT was missed but ESS indicates
            // clear or decryptable audio, allow this slot's audio now.
            if (state->p25_p2_audio_allowed[1] == 0) {
                // ESS-driven enablement with active-call OR immediate voice
                // context for the right slot, mirroring left-slot handling.
                int in_call = ((state->dmrburstR >= 20 && state->dmrburstR <= 22) || voice);
                int alg = state->payload_algidR;
                int allow = (in_call
                             && ((alg == 0 || alg == 0x80)
                                 || (((alg == 0xAA || alg == 0x81 || alg == 0x9F) && state->RR != 0)
                                     || ((alg == 0x84 || alg == 0x89) && state->aes_key_loaded[1] == 1))))
                                ? 1
                                : 0;
                if (allow) {
                    state->p25_p2_audio_allowed[1] = 1;
                }
            }
            if (state->payload_algidR != 0x80 && state->payload_algidR != 0x0) {
                fprintf(stderr, "\n");
                fprintf(stderr, " VCH 2 -");
                fprintf(stderr, " ALG ID: 0x%02X", state->payload_algidR);
                fprintf(stderr, " KEY ID: 0x%04X", state->payload_keyidR);
                fprintf(stderr, " MI: 0x%016llX", state->payload_miN);
                fprintf(stderr, " ESSB");

                if (state->RR != 0 && state->payload_algidR == 0xAA) {
                    fprintf(stderr, " Key 0x%010llX", state->RR);
                }
                if (state->RR != 0 && state->payload_algidR == 0x81) {
                    fprintf(stderr, " Key 0x%016llX", state->RR);
                }
                if ((state->payload_algidR == 0x84 || state->payload_algidR == 0x89) && state->aes_key_loaded[1] == 1) {
                    fprintf(stderr, "\n ");
                    fprintf(stderr, "Key: %016llX %016llX ", state->A1[1], state->A2[1]);
                    if (state->payload_algidR == 0x84) {
                        fprintf(stderr, "%016llX %016llX", state->A3[1], state->A4[1]);
                    }
                    // opts->unmute_encrypted_p25 = 1; //needed?
                }

                //expand 64-bit MI to 128-bit for AES
                if (state->payload_algidR == 0x84 || state->payload_algidR == 0x89) {
                    LFSR128(state);
                    // fprintf (stderr, "\n");
                }
            }
        }

#define P25p2_ENC_LO //disable if this behavior is detremental
#ifdef P25p2_ENC_LO

        // If trunking and tuning ENC calls is disabled, lock out and go back to CC
        // NOTE: Treat ALGID 0x00 and 0x80 as clear. Consider keys for RC4/DES/DES‑XL and
        // AES key presence to avoid false lockouts when decryptable.
        int enc_lo = 1;
        int ttg = 0; // checking to a valid TG will help make sure we have a good MAC_PTT or SACCH Channel Update First
        int alg = 0; // set alg and key based on current slot values
        unsigned long long int key = 0;
        int slot = state->currentslot;
        int aes_loaded = state->aes_key_loaded[slot];

        if (state->currentslot == 0) {
            ttg = state->lasttg;
            alg = state->payload_algid;
            if (alg == 0xAA) {
                key = state->R;
            }
            // else if (future condition) key = 1;
            // else if (future condition) key = 1;
        }

        if (state->currentslot == 1) {
            ttg = state->lasttgR;
            alg = state->payload_algidR;
            if (alg == 0xAA) {
                key = state->RR;
            }
            // else if (future condition) key = 1;
            // else if (future condition) key = 1;
        }

        if (alg != 0 && alg != 0x80 && opts->p25_trunk == 1 && opts->p25_is_tuned == 1
            && opts->trunk_tune_enc_calls == 0) {
            // Consider key presence for known algorithms
            int have_key = 0;
            if ((alg == 0xAA || alg == 0x81 || alg == 0x9F) && key != 0) {
                have_key = 1; // RC4/DES/DES-XL have key
            }
            if ((alg == 0x84 || alg == 0x89) && aes_loaded == 1) {
                have_key = 1; // AES-256/AES-128 key loaded
            }
            if (have_key) {
                enc_lo = 0;
            }

            // If locked out, mark DE and emit once for this TG, then apply
            // per-slot gating consistent with SACCH/FACCH handling: mute only
            // the encrypted slot and return to CC only if the opposite slot is
            // not active.
            if (enc_lo == 1 && ttg != 0) {
                int eslot = state->currentslot & 1;
                p25_emit_enc_lockout_once(opts, state, (uint8_t)eslot, ttg, /*svc_bits*/ 0);

                // Gate only this slot and flush any queued audio for it
                state->p25_p2_audio_allowed[eslot] = 0;
                p25_p2_audio_ring_reset(state, eslot);

                int other = eslot ^ 1;
                // Consider per-slot gate, ring, and recent MAC_ACTIVE recency on other slot
                double mac_hold = 0.75; // seconds; env override aligns with SM/xCCH
                {
                    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                    if (state->p25_cfg_mac_hold_s > 0.0) {
                        mac_hold = state->p25_cfg_mac_hold_s;
                    } else if (cfg && cfg->p25_mac_hold_is_set) {
                        mac_hold = cfg->p25_mac_hold_s;
                    }
                }
                double nowm_hold = dsd_time_now_monotonic_s();
                int other_recent = (state->p25_p2_last_mac_active_m[other] > 0.0)
                                   && ((nowm_hold - state->p25_p2_last_mac_active_m[other]) <= mac_hold);
                int other_audio =
                    state->p25_p2_audio_allowed[other] || state->p25_p2_audio_ring_count[other] > 0 || other_recent;
                if (!other_audio) {
                    fprintf(stderr, " No Enc Following on P25p2 Trunking; ");
                    // Defer return to CC within VC grace to protect opposite-slot clear calls
                    double vc_grace = (state->p25_cfg_vc_grace_s > 0.0) ? state->p25_cfg_vc_grace_s : 0.75;
                    if (!(state->p25_cfg_vc_grace_s > 0.0)) {
                        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                        if (cfg && cfg->p25_vc_grace_is_set) {
                            vc_grace = cfg->p25_vc_grace_s;
                        }
                    }
                    double nowm = dsd_time_now_monotonic_s();
                    double dt_since_tune =
                        (state->p25_last_vc_tune_time_m > 0.0) ? (nowm - state->p25_last_vc_tune_time_m) : 1e9;
                    if (dt_since_tune >= vc_grace) {
                        fprintf(stderr, "Return to CC; \n");
                        state->p25_sm_force_release = 1;
                        p25_p2_teardown_call(opts, state);
                        p25_sm_on_release(opts, state);
                    } else {
                        fprintf(stderr, "Defer (VC grace); stay on VC. \n");
                    }
                } else {
                    fprintf(stderr, " No Enc Following on P25p2 Trunking; Other slot active; stay on VC. \n");
                    // Keep encryption fields intact so gating remains correct; clear banner only.
                    if (eslot == 0) {
                        snprintf(state->call_string[0], sizeof state->call_string[0], "%s", "                     ");
                    } else {
                        snprintf(state->call_string[1], sizeof state->call_string[1], "%s", "                     ");
                    }
                }
            }
        }
#endif //P25p2_ENC_LO
    }
    if (ec == -1 || ec >= 15) {
        state->p25_p2_rs_ess_err++;
        //below needs a line break before LFSRP runs (when payload == 0)
        //ESS R-S Failure -- run LFSR on current MI if applicable
        if (state->currentslot == 0 && state->payload_algid != 0x80 && state->payload_keyid != 0
            && state->payload_miP != 0) {
            LFSRP(state);
        }
        if (state->currentslot == 1 && state->payload_algidR != 0x80 && state->payload_keyidR != 0
            && state->payload_miN != 0) {
            LFSRP(state);
        }

        //expand 64-bit MI to 128-bit for AES, vch 0
        if (state->currentslot == 0 && (state->payload_algid == 0x84 || state->payload_algid == 0x89)) {
            LFSR128(state);
        }

        //expand 64-bit MI to 128-bit for AES, vch 1
        if (state->currentslot == 1 && (state->payload_algidR == 0x84 || state->payload_algidR == 0x89)) {
            LFSR128(state);
        }
    }
    fprintf(stderr, "%s", KNRM);

    state->fourv_counter[state->currentslot] = 0;
}

void
process_2V(dsd_opts* opts, dsd_state* state) {

    w = csubset;
    int b = 0;
    int q = 0;
    int r = 0;
    int s = 0;
    int t = 0;

    // SM event: ACTIVE on current slot - only emit if audio is allowed for this
    // slot (clear or decryptable). This prevents encrypted/undecryptable frames
    // from keeping the SM alive indefinitely and defeating grant timeout.
    if (state) {
        int slot = state->currentslot & 1;
        if (state->p25_p2_audio_allowed[slot]) {
            p25_sm_emit_active(opts, state, slot);
            // Mark recent voice only when audio is actually allowed
            state->last_vc_sync_time = time(NULL);
            state->last_vc_sync_time_m = dsd_time_now_monotonic_s();
        }
    }
    for (int x = 0; x < 72; x++) {
        int ww = *w;
        if (ww == 0) {
            b = c0[q];
            q++;
        }
        if (ww == 1) {
            b = c1[r];
            r++;
        }
        if (ww == 2) {
            b = c2[s];
            s++;
        }
        if (ww == 3) {
            b = c3[t];
            t++;
        }

        if (*w >= 0 && *w < 4 && b >= 0 && b < 24) {
            ambe_fr1[*w][b] = p2xbit[x + 2 + vc_counter];
            ambe_fr2[*w][b] = p2xbit[x + 76 + vc_counter];
        }
        w++;
    }

    //collect ESS_A (both parts)
    for (short i = 0; i < 96; i++) {
        ess_a[state->currentslot][i] = p2xbit[i + 148 + vc_counter];
    }

    for (short i = 0; i < 72; i++) //load up ESS_A 2
    {
        ess_a[state->currentslot][i + 96] = p2xbit[i + 246 + vc_counter];
    }

    // Run ESS processing early so ALG/KID decisions can enable audio gates
    // before decoding the first two AMBE frames. This helps avoid missing the
    // beginning of clear calls when early MAC_PTT/ACTIVE are missed.
    process_ESS(opts, state);

    if (opts->payload == 1) {
        fprintf(stderr, "\n");
    }

    //unsure of the best location for these counter resets
    if (state->voice_counter[0] >= 18) {
        state->voice_counter[0] = 0;
    }

    if (state->voice_counter[1] >= 18) {
        state->voice_counter[1] = 0;
    }

    // Gate first 2V AMBE decode like subsequent frames to avoid decoding
    // encrypted audio when ENC lockout is enabled or audio is otherwise
    // disallowed for this slot. ESS has already run above to open audio early
    // when clear/decryptable.
    if (state->p25_p2_audio_allowed[state->currentslot]) {
        processMbeFrame(opts, state, NULL, ambe_fr1, NULL);
        if (state->currentslot == 0) {
            memcpy(state->f_l4[0], state->audio_out_temp_buf, sizeof(state->audio_out_temp_buf));
            memcpy(state->s_l4[(state->voice_counter[0]++) % 18], state->s_l, sizeof(state->s_l));
            memcpy(state->s_l4u[0], state->s_lu, sizeof(state->s_lu));
        } else {
            memcpy(state->f_r4[0], state->audio_out_temp_bufR, sizeof(state->audio_out_temp_bufR));
            memcpy(state->s_r4[(state->voice_counter[1]++) % 18], state->s_r, sizeof(state->s_r));
            memcpy(state->s_r4u[0], state->s_ru, sizeof(state->s_ru));
        }
    } else {
        // Not allowed: zero both float and short buffers to prevent stale
        // encrypted audio from leaking into SS18 mixer path
        if (state->currentslot == 0) {
            memset(state->f_l4[0], 0, sizeof(state->f_l4[0]));
            memset(state->s_l4[(state->voice_counter[0]++) % 18], 0, sizeof(state->s_l4[0]));
        } else {
            memset(state->f_r4[0], 0, sizeof(state->f_r4[0]));
            memset(state->s_r4[(state->voice_counter[1]++) % 18], 0, sizeof(state->s_r4[0]));
        }
    }

    if (state->p25_p2_audio_allowed[state->currentslot]) {
        processMbeFrame(opts, state, NULL, ambe_fr2, NULL);
        if (state->currentslot == 0) {
            memcpy(state->f_l4[1], state->audio_out_temp_buf, sizeof(state->audio_out_temp_buf));
            memcpy(state->s_l4[(state->voice_counter[0]++) % 18], state->s_l, sizeof(state->s_l));
            memcpy(state->s_l4u[1], state->s_lu, sizeof(state->s_lu));
            p25_p2_audio_ring_push(state, 0, state->f_l4[1]);
        } else {
            memcpy(state->f_r4[1], state->audio_out_temp_bufR, sizeof(state->audio_out_temp_bufR));
            memcpy(state->s_r4[(state->voice_counter[1]++) % 18], state->s_r, sizeof(state->s_r));
            memcpy(state->s_r4u[1], state->s_ru, sizeof(state->s_ru));
            p25_p2_audio_ring_push(state, 1, state->f_r4[1]);
        }
    } else {
        if (state->currentslot == 0) {
            memset(state->f_l4[1], 0, sizeof(state->f_l4[1]));
            memset(state->s_l4[(state->voice_counter[0]++) % 18], 0, sizeof(state->s_l4[0]));
        } else {
            memset(state->f_r4[1], 0, sizeof(state->f_r4[1]));
            memset(state->s_r4[(state->voice_counter[1]++) % 18], 0, sizeof(state->s_r4[0]));
        }
    }
    // if (state->currentslot == 0) state->voice_counter[0] = 0; if (state->currentslot == 1) state->voice_counter[1] = 0;

    //reset drop bytes after a 2V
    if (state->currentslot == 0 && state->payload_algid == 0xAA) {
        state->dropL = 256;
    }

    if (state->currentslot == 1 && state->payload_algidR == 0xAA) {
        state->dropR = 256;
    }

    //reset voice counter after 2V (DES)
    if (state->currentslot == 0 && state->payload_algid == 0x81) {
        state->DMRvcL = 0;
    }

    if (state->currentslot == 1 && state->payload_algidR == 0x81) {
        state->DMRvcR = 0;
    }

    //reset voice counter after 2V (AES 256)
    if (state->currentslot == 0 && state->payload_algid == 0x84) {
        state->DMRvcL = 0;
    }

    if (state->currentslot == 1 && state->payload_algidR == 0x84) {
        state->DMRvcR = 0;
    }

    //reset voice counter after 2V (AES 128)
    if (state->currentslot == 0 && state->payload_algid == 0x89) {
        state->DMRvcL = 0;
    }

    if (state->currentslot == 1 && state->payload_algidR == 0x89) {
        state->DMRvcR = 0;
    }
}

//P2 Data Unit ID
void
process_P2_DUID(dsd_opts* opts, dsd_state* state) {
    //DUID exist on all P25p2 frames, need to check this so we can process the TS frame properly
    vc_counter = 0;
    int err_counter = 0;
    const time_t now = time(NULL);

    for (ts_counter = 0; ts_counter < 4; ts_counter++) //12
    {
        duid_decoded = -2;
        int sacch = 0;
        UNUSED(sacch);
        p2_duid[0] = p2bit[0 + (ts_counter * 360)];
        p2_duid[1] = p2bit[1 + (ts_counter * 360)];
        p2_duid[2] = p2bit[74 + (ts_counter * 360)];
        p2_duid[3] = p2bit[75 + (ts_counter * 360)];
        p2_duid[4] = p2bit[244 + (ts_counter * 360)];
        p2_duid[5] = p2bit[245 + (ts_counter * 360)];
        p2_duid[6] = p2bit[318 + (ts_counter * 360)];
        p2_duid[7] = p2bit[319 + (ts_counter * 360)];

        //process p2_duid with (8,4,4) encoding/decoding
        int p2_duid_complete = 0;
        for (int i = 0; i < 8; i++) {
            p2_duid_complete = p2_duid_complete << 1;
            p2_duid_complete = p2_duid_complete | p2_duid[i];
        }
        duid_decoded = duid_lookup[p2_duid_complete];

        char timestr[9];
        getTimeC_buf(timestr);
        fprintf(stderr, "\n%s        P25p2 ", timestr);

        if (state->currentslot == 0 && duid_decoded != 3 && duid_decoded != 12 && duid_decoded != 13
            && duid_decoded != 4) {
            fprintf(stderr, "LCH 0 ");
            //open MBEout file - slot 1 - USE WITH CAUTION on Phase 2! Consider using a symbol capture bin instead!
            if (duid_decoded == 0 || duid_decoded == 6) //4V or 2V (voice)
            {
                voice = 1;
                if ((opts->mbe_out_dir[0] != 0) && (opts->mbe_out_f == NULL)) {
                    openMbeOutFile(opts, state);
                }
            }
        } else if (state->currentslot == 1 && duid_decoded != 3 && duid_decoded != 12 && duid_decoded != 13
                   && duid_decoded != 4) {
            fprintf(stderr, "LCH 1 ");
            //open MBEout file - slot 2 - USE WITH CAUTION on Phase 2! Consider using a symbol capture bin instead!
            if (duid_decoded == 0 || duid_decoded == 6) //4V or 2V (voice)
            {
                voice = 1;
                if ((opts->mbe_out_dir[0] != 0) && (opts->mbe_out_fR == NULL)) {
                    openMbeOutFileR(opts, state);
                }
            }
        }
        //The LCCH may occupy LCH 0 or LCH 1 or both. BBAD 3.3 p8
        else if (duid_decoded == 13) //MAC_SIGNAL, or clear LCCH
        {
            // sacch = 1; //only an 'inverted' slot when its TS index 10 or 11
            fprintf(stderr, "LCCH  ");

            //when on a CC, rotate the symbol out file every hour, if enabled
            if (opts->p25_is_tuned == 0) {
                rotate_symbol_out_file(opts, state);
            }
        } else if (duid_decoded == 4) //Scrambled LCCH (TDMA_CC only...look in the manual again)
        {
            // sacch = 1; //only an 'inverted' slot when its TS index 10 or 11
            fprintf(stderr, "LCCHs ");
        } else {
            sacch = 1; //always an "inverted" sacch slot
            fprintf(stderr, "SACCH ");
        }

        // Check to see when last voice activity occurred in order to allow tuning on phase 2
        // MAC_SIGNAL or MAC_IDLE when no more voice activity on current channel
        // This is primarily a fix for TDMA control channels that carry voice (Duke P25)
        // For trunking, defer the release until after LCCH processing so per-slot audio
        // gates are cleared first; this avoids the SM deferring on stale gates.
        int p2_pending_release = 0;
        if (duid_decoded == 13 && opts->p25_is_tuned == 1
            && ((now - state->last_vc_sync_time) > opts->trunk_hangtime)) { // MAC_SIGNAL hangtime expiry
            // Also respect a small grace window after VC tune so we don't
            // bounce back to CC before audio gates open on fresh calls.
            double vc_grace = 0.75; // seconds; override via DSD_NEO_P25_VC_GRACE
            {
                const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                if (state->p25_cfg_vc_grace_s > 0.0) {
                    vc_grace = state->p25_cfg_vc_grace_s;
                } else if (cfg && cfg->p25_vc_grace_is_set) {
                    vc_grace = cfg->p25_vc_grace_s;
                }
            }
            double dt_since_tune =
                (state->p25_last_vc_tune_time != 0) ? (double)(now - state->p25_last_vc_tune_time) : 1e9;
            if (dt_since_tune < vc_grace) {
                // Too soon after tuning; skip early release this cycle
                goto after_mac_signal_idle_check;
            }
            // Do not treat channel as idle if SACCH recently indicated
            // MAC_PTT/ACTIVE on either logical channel; this avoids bouncing
            // back to CC during the first moments after a tune when audio
            // gates are not yet open but valid voice is present.
            double mac_hold = 0.75; // seconds; override via DSD_NEO_P25_MAC_HOLD
            {
                const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                if (state->p25_cfg_mac_hold_s > 0.0) {
                    mac_hold = state->p25_cfg_mac_hold_s;
                } else if (cfg && cfg->p25_mac_hold_is_set) {
                    mac_hold = cfg->p25_mac_hold_s;
                }
            }
            int left_mac_active = (state->p25_p2_last_mac_active_m[0] > 0.0
                                   && (dsd_time_now_monotonic_s() - state->p25_p2_last_mac_active_m[0]) <= mac_hold);
            int right_mac_active = (state->p25_p2_last_mac_active_m[1] > 0.0
                                    && (dsd_time_now_monotonic_s() - state->p25_p2_last_mac_active_m[1]) <= mac_hold);
            if (opts->p25_trunk == 1) {
                if (!(left_mac_active || right_mac_active)) {
                    p2_pending_release = 1; // handle after LCCH is processed below
                }
            } else {
                // Non-trunking: minimal reset
                state->p25_vc_freq[0] = state->p25_vc_freq[1] = 0;
                memset(state->active_channel, 0, sizeof(state->active_channel));
                state->voice_counter[0] = 0;
                state->voice_counter[1] = 0;
                memset(state->s_l4, 0, sizeof(state->s_l4));
                memset(state->s_r4, 0, sizeof(state->s_r4));
                opts->p25_is_tuned = 0;
            }
        }
    after_mac_signal_idle_check:

        if (duid_decoded == 13 && ((now - state->last_active_time) > 2)
            && opts->p25_is_tuned == 0) //should we use && opts->p25_is_tuned == 1?
        {
            memset(state->active_channel, 0, sizeof(state->active_channel)); //zero out here? I think this will be fine
            //clear out stale voice samples left in the buffer and reset counter value
            state->voice_counter[0] = 0;
            state->voice_counter[1] = 0;
            memset(state->s_l4, 0, sizeof(state->s_l4));
            memset(state->s_r4, 0, sizeof(state->s_r4));
        }

        if (duid_decoded == 0) {
            fprintf(stderr, " 4V %d", state->fourv_counter[state->currentslot] + 1);
            //debug see which 4V and which 2V randomly pop on Duke P25p2 CC (8 bit binary code)
            // fprintf (stderr, " DUID: %d", p2_duid_complete);
            if (state->p2_wacn != 0 && state->p2_cc != 0 && state->p2_sysid != 0 && state->p2_wacn != 0xFFFFF
                && state->p2_cc != 0xFFF && state->p2_sysid != 0xFFF) {
                // Refresh recent-voice on valid voice frames when audio is
                // allowed OR when ENC follow is enabled. This avoids rapid
                // VC↔CC bounce on encrypted calls we are following, while
                // still allowing ENC-lockout policy to end calls quickly when
                // not following.
                if (state->p25_p2_audio_allowed[state->currentslot] || opts->trunk_tune_enc_calls == 1) {
                    state->last_vc_sync_time = now;
                }
                process_4V(opts, state);
            }
        } else if (duid_decoded == 6) {
            fprintf(stderr, " 2V");
            //debug see which 4V and which 2V randomly pop on Duke P25p2 CC (8 bit binary code)
            // fprintf (stderr, " DUID: %d", p2_duid_complete);
            if (state->p2_wacn != 0 && state->p2_cc != 0 && state->p2_sysid != 0 && state->p2_wacn != 0xFFFFF
                && state->p2_cc != 0xFFF && state->p2_sysid != 0xFFF) {
                if (state->p25_p2_audio_allowed[state->currentslot] || opts->trunk_tune_enc_calls == 1) {
                    state->last_vc_sync_time = now;
                }
                process_2V(opts, state);
            }
        } else if (duid_decoded == 3) {
            if (state->p2_wacn != 0 && state->p2_cc != 0 && state->p2_sysid != 0 && state->p2_wacn != 0xFFFFF
                && state->p2_cc != 0xFFF && state->p2_sysid != 0xFFF) {
                process_SACCHs(opts, state);
            }
        } else if (duid_decoded == 12) {
            process_SACCHc(opts, state);
        } else if (duid_decoded == 15) {
            process_FACCHc(opts, state);
        } else if (duid_decoded == 9) {
            if (state->p2_wacn != 0 && state->p2_cc != 0 && state->p2_sysid != 0 && state->p2_wacn != 0xFFFFF
                && state->p2_cc != 0xFFF && state->p2_sysid != 0xFFF) {
                process_FACCHs(opts, state);
            }
        } else if (duid_decoded == 13) {
            state->p2_is_lcch = 1;
            process_SACCHc(opts, state);
            // If MAC_SIGNAL hangtime expired while tuned on a trunked VC,
            // perform the centralized release now that LCCH processing has
            // cleared per-slot audio gates.
            if (p2_pending_release) {
                p25_p2_teardown_call(opts, state);
                p25_sm_on_release(opts, state);
            }
        } else if (duid_decoded == 4) {
            if (state->p2_wacn != 0 && state->p2_cc != 0 && state->p2_sysid != 0 && state->p2_wacn != 0xFFFFF
                && state->p2_cc != 0xFFF && state->p2_sysid != 0xFFF) {
                state->p2_is_lcch = 1;
                process_SACCHs(opts, state);
            }
        } else {
            fprintf(stderr, " DUID ERR %d", duid_decoded);
            err_counter++;
        }
        if (err_counter > 1) //&& opts->aggressive_framesync == 1
        {
            //zero out values when errs accumulate in DUID
            //most likely cause will be signal drop or tuning away
            state->payload_algid = 0;
            state->payload_keyid = 0;
            state->payload_algidR = 0;
            state->payload_keyidR = 0;
            // state->lastsrc = 0; //disable?
            // state->lastsrcR = 0; //disable?
            // state->lasttg = 0; //disable?
            // state->lasttgR = 0; //disable?
            state->p2_is_lcch = 0;
            state->fourv_counter[0] = 0;
            state->fourv_counter[1] = 0;
            state->voice_counter[0] = 0;
            state->voice_counter[1] = 0;

            goto END;
        }
        //since we are in a while loop, run ncursesPrinter here.
        if (opts->use_ncurses_terminal == 1) {
            ui_publish_both_and_redraw(opts, state);
        }

        //slot 1
        watchdog_event_history(opts, state, 0);
        dsd_p25_optional_hook_watchdog_event_current(opts, state, 0);

        //slot 2 for TDMA systems
        watchdog_event_history(opts, state, 1);
        dsd_p25_optional_hook_watchdog_event_current(opts, state, 1);

        //add 360 bits to each counter
        vc_counter = vc_counter + 360;

        //debug enable both slots before playback
        // opts->slot1_on = 1;
        // opts->slot2_on = 1;

        //NOTE: Could be an issue if MAC_SIGNAL onn LCH 1 and voice in LCH 0? It might Stutter?
        if (sacch == 0 && ts_counter & 1 && opts->floating_point == 1 && opts->pulse_digi_rate_out == 8000) {
            playSynthesizedVoiceFS4(opts, state);
        }

        // if (sacch == 0 && ts_counter & 1 && opts->floating_point == 0 && opts->pulse_digi_rate_out == 8000)
        // 		playSynthesizedVoiceSS4 (opts, state);

        // fprintf (stderr, " VCH0: %d;", state->voice_counter[0]); //debug
        // fprintf (stderr, " VCH1: %d;", state->voice_counter[1]); //debug

        //this works, but may still have an element of 'dual voice stutter' which was my initial complaint, but shouldn't 'lag' during trunking operations (hopefully)
        if ((state->voice_counter[0] >= 18 || state->voice_counter[1] >= 18) && opts->floating_point == 0
            && opts->pulse_digi_rate_out == 8000 && ts_counter & 1) {
            //debug test, see what each counter is at during playback on dual voice
            // fprintf (stderr, " VC1: %02d; VC2: %02d;", state->voice_counter[0], state->voice_counter[1] );

            playSynthesizedVoiceSS18(opts, state);
            state->voice_counter[0] = 0; //reset
            state->voice_counter[1] = 0; //reset
        }

        //flip slots after each TS processed
        if (state->currentslot == 0) {
            state->currentslot = 1;
        } else {
            state->currentslot = 0;
        }

        //reset voice after each compliment of 2 slots
        if (ts_counter & 1) {
            voice = 0;
        }
    }

    // Fallback release: if we are tuned to a P25p2 voice channel but have not
    // observed any recent voice activity for longer than hangtime AND both
    // logical channels have audio disabled, force a return to the control
    // channel. This covers sites that do not emit MAC_SIGNAL/IDLE on the VCs
    // after call teardown, preventing the tuner from getting wedged.
    if (opts->p25_trunk == 1 && opts->p25_is_tuned == 1) {
        time_t now2 = time(NULL);
        int no_recent_voice =
            (state->last_vc_sync_time != 0) && ((now2 - state->last_vc_sync_time) > opts->trunk_hangtime);
        int both_slots_idle = (state->p25_p2_audio_allowed[0] == 0 && state->p25_p2_audio_allowed[1] == 0);
        double dt_since_tune =
            (state->p25_last_vc_tune_time != 0) ? (double)(now2 - state->p25_last_vc_tune_time) : 1e9;
        double vc_grace = 0.75; // seconds; override with DSD_NEO_P25_VC_GRACE
        {
            const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
            if (state->p25_cfg_vc_grace_s > 0.0) {
                vc_grace = state->p25_cfg_vc_grace_s;
            } else if (cfg && cfg->p25_vc_grace_is_set) {
                vc_grace = cfg->p25_vc_grace_s;
            }
        }
        if (no_recent_voice && both_slots_idle && dt_since_tune >= vc_grace) {
            state->p25_sm_force_release = 1;
            p25_p2_teardown_call(opts, state);
            p25_sm_on_release(opts, state);
        }
    }
END:
    voice = 0; //reset before exit
}

void
processP2(dsd_opts* opts, dsd_state* state) {
    state->dmr_stereo = 1;
    p2_dibit_buffer(opts, state);
    voice = 0;

    //look at our ISCH values and determine location in superframe before running frame scramble
    for (framing_counter = 0; framing_counter < 4; framing_counter++) {
        //run ISCH in here so we know when to start descramble offset
        process_ISCH(opts, state);
    }

    //set initial current slot depending on offset value
    if (state->p2_scramble_offset % 2) {
        state->currentslot = 1;
    } else {
        state->currentslot = 0;
    }

    //frame_scramble runs lfsr and creates an array of unscrambled bits to pull from
    process_Frame_Scramble(opts, state);

    //process DUID will run through all collected frames and handle them appropriately
    process_P2_DUID(opts, state);

    state->dmr_stereo = 0;
    state->p2_is_lcch = 0;

    fprintf(stderr, "\n");
}
