// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
 *
 * Float-path audio processing helpers and playback mixers
 * (DMR stereo variants and utilities)
 *
 * LWVMOBILE
 * 2023-10 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/audio_filters.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/platform/audio.h>
#include <dsd-neo/platform/file_compat.h>
#include <dsd-neo/runtime/p25_p2_audio_ring.h>
#include <dsd-neo/runtime/udp_audio_hooks.h>

#include <mbelib.h>

#include <sndfile.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Write int16 audio using the abstraction layer */
static void
write_s16_audio(dsd_opts* opts, const int16_t* buf, size_t frames) {
    if (opts->audio_out_stream) {
        dsd_audio_write(opts->audio_out_stream, buf, frames);
    }
}

/* Convert float audio to int16 and write using the abstraction layer */
static void
write_float_audio(dsd_opts* opts, const float* buf, size_t frames) {
    if (!opts->audio_out_stream || !buf) {
        return;
    }
    /* Convert float [-1.0, 1.0] to int16 with clipping */
    int channels = opts->pulse_digi_out_channels;
    size_t total_samples = frames * (size_t)channels;
    int16_t tmp[320 * 2]; /* Max 320 stereo frames */
    if (total_samples > sizeof(tmp) / sizeof(tmp[0])) {
        total_samples = sizeof(tmp) / sizeof(tmp[0]);
    }
    for (size_t i = 0; i < total_samples; i++) {
        float v = buf[i] * 32767.0f;
        if (v > 32767.0f) {
            v = 32767.0f;
        } else if (v < -32768.0f) {
            v = -32768.0f;
        }
        tmp[i] = (int16_t)v;
    }
    dsd_audio_write(opts->audio_out_stream, tmp, frames);
}

// Return 1 if all elements are effectively zero (|x| < 1e-12f)
static inline int
dsd_is_all_zero_f(const float* buf, size_t n) {
    if (!buf) {
        return 1;
    }
    const float eps = 1e-12f;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] > eps || buf[i] < -eps) {
            return 0;
        }
    }
    return 1;
}

static inline void
write_audio_out(int fd, const void* buf, size_t bytes) {
    const ssize_t written = dsd_write(fd, buf, bytes);
    (void)written;
}

static int
p25p2_s16_frames_have_audio(short frames[18][160]) {
    for (int j = 0; j < 18; j++) {
        for (int i = 0; i < 160; i++) {
            if (frames[j][i] != 0) {
                return 1;
            }
        }
    }
    return 0;
}

void
dsd_p25p2_flush_partial_audio(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return;
    }
    // This helper is specifically for the short/int16 P25p2 SS18 path.
    if (opts->floating_point != 0 || opts->pulse_digi_rate_out != 8000) {
        return;
    }

    int has_l = p25p2_s16_frames_have_audio(state->s_l4);
    int has_r = p25p2_s16_frames_have_audio(state->s_r4);
    if (!(has_l || has_r)) {
        return;
    }

    // The SS18 mixer uses p25_p2_audio_allowed as its per-slot gate.
    // At release, MAC_END/IDLE may already have cleared the gate; the
    // s_l4/s_r4 buffers only contain decoded audio when a slot was allowed at
    // decode time, so gate playback based on actual buffered audio presence.
    state->p25_p2_audio_allowed[0] = has_l ? 1 : 0;
    state->p25_p2_audio_allowed[1] = has_r ? 1 : 0;

    playSynthesizedVoiceSS18(opts, state);
    state->voice_counter[0] = 0;
    state->voice_counter[1] = 0;
}

//NOTE: Tones produce ringing sound when put through the hpf_d, may want to look into tweaking it,
//or looking for a way to store is_tone by glancing at ambe_d values and not running hpf_d on them

//TODO: WAV File saving (works fine on shorts, but on float, writing short to wav is not auto-gained,
//so super quiet, either convert to float wav files, or run processAudio AFTER memcpy of the temp_buf)

//simple method -- produces cleaner results, but can be muted (or very loud) at times
//has manual control only, no auto gain
// void agf (dsd_opts * opts, dsd_state * state, float samp[160], int slot)
// {
//   int i;
//   float mmax = 0.75f;
//   float mmin = -0.75f;
//   float df = 3276.7f;

//   //Default gain value of 1.0f on audio_gain == 0
//   float gain = 1.0f;

//   //make it so that gain is 1.0f on 25.0f aout, and 2.0f on 50 aout
//   if (opts->audio_gain != 0 && slot == 0)
//     gain = state->aout_gain / 25.0f;

//   if (opts->audio_gain != 0 && slot == 1)
//     gain = state->aout_gainR / 25.0f;

//   //mono output handles slightly different, need to further decimate
//   if (opts->pulse_digi_out_channels == 1)
//     df *= 4.0f;

//   for (i = 0; i < 160; i++)
//   {
//     //simple decimation
//     samp[i] /= df;
//     samp[i] *= 0.65f;

//     //simple clipping
//     if (samp[i] > mmax)
//       samp[i] = mmax;
//     if (samp[i] < mmin)
//       samp[i] = mmin;

//     //user gain factor
//     samp[i] *= gain;

//   }

// }

//float stereo mix 3v2 DMR
void
playSynthesizedVoiceFS3(dsd_opts* opts, dsd_state* state) {

    //NOTE: This runs once for every two timeslots, if we are in the BS voice loop
    //it doesn't matter if both slots have voice, or if one does, the slot without voice
    //will play silence while this runs if no voice present

    int i;
    int encL, encR;
    float stereo_samp1[320]; //8k 2-channel stereo interleave mix
    float stereo_samp2[320]; //8k 2-channel stereo interleave mix
    float stereo_samp3[320]; //8k 2-channel stereo interleave mix

    memset(stereo_samp1, 0.0f, sizeof(stereo_samp1));
    memset(stereo_samp2, 0.0f, sizeof(stereo_samp2));
    memset(stereo_samp3, 0.0f, sizeof(stereo_samp3));

    //TODO: add option to bypass enc with a toggle as well

    // DMR per-slot ENC gating: derive from decoder-side flags and user policy.
    // Unmute if either the stream is clear or user elected to unmute enc.
    // These mirror dsd_mbe.c logic used for 16-bit path, but kept local here for float path.
    encL = 1;
    encR = 1;
    {
        int l_is_enc = state->dmr_encL != 0; // decoder flagged L as encrypted
        int r_is_enc = state->dmr_encR != 0; // decoder flagged R as encrypted
        if (!l_is_enc || opts->dmr_mute_encL == 0) {
            encL = 0;
        }
        if (!r_is_enc || opts->dmr_mute_encR == 0) {
            encR = 0;
        }
    }

    //CHEAT: Using the slot on/off, use that to set encL or encR back on
    //as a simple way to turn off voice synthesis in a particular slot
    //its not really 'disabled', we just aren't playing it
    if (opts->slot1_on == 0) {
        encL = 1;
    }
    if (opts->slot2_on == 0) {
        encR = 1;
    }

    unsigned long TGL = (unsigned long)state->lasttg;
    unsigned long TGR = (unsigned long)state->lasttgR;

    // Apply whitelist/TG-hold gating shared with other mixers.
    (void)dsd_audio_group_gate_dual(opts, state, TGL, TGR, encL, encR, &encL, &encR);

    //run autogain on the f_ buffers
    agf(opts, state, state->f_l4[0], 0);
    agf(opts, state, state->f_r4[0], 1);
    agf(opts, state, state->f_l4[1], 0);
    agf(opts, state, state->f_r4[1], 1);
    agf(opts, state, state->f_l4[2], 0);
    agf(opts, state, state->f_r4[2], 1);

    //interleave left and right channels from the temp (float) buffer
    audio_mix_interleave_stereo_f32(state->f_l4[0], state->f_r4[0], 160, encL, encR, stereo_samp1);
    audio_mix_interleave_stereo_f32(state->f_l4[1], state->f_r4[1], 160, encL, encR, stereo_samp2);
    audio_mix_interleave_stereo_f32(state->f_l4[2], state->f_r4[2], 160, encL, encR, stereo_samp3);

    //at this point, if both channels are still flagged as enc, then we can skip all playback/writing functions
    if (encL && encR) {
        goto FS3_END;
    }

    // If only one slot is active, duplicate to both channels for stereo sinks
    if (!encL && encR) {
        for (i = 0; i < 320; i += 2) {
            stereo_samp1[i + 1] = stereo_samp1[i + 0];
            stereo_samp2[i + 1] = stereo_samp2[i + 0];
            stereo_samp3[i + 1] = stereo_samp3[i + 0];
        }
        encR = 0;
    } else if (encL && !encR) {
        for (i = 0; i < 320; i += 2) {
            stereo_samp1[i + 0] = stereo_samp1[i + 1];
            stereo_samp2[i + 0] = stereo_samp2[i + 1];
            stereo_samp3[i + 0] = stereo_samp3[i + 1];
        }
        encL = 0;
    }

    if (opts->audio_out == 1) {
        if (opts->pulse_digi_out_channels == 1) {
            // Mix down to mono respecting which side(s) are active
            float mono1[160], mono2[160], mono3[160];
            memset(mono1, 0, sizeof(mono1));
            memset(mono2, 0, sizeof(mono2));
            memset(mono3, 0, sizeof(mono3));
            int l_on = !encL;
            int r_on = !encR;
            audio_mix_mono_from_slots_f32(state->f_l4[0], state->f_r4[0], 160, l_on, r_on, mono1);
            audio_mix_mono_from_slots_f32(state->f_l4[1], state->f_r4[1], 160, l_on, r_on, mono2);
            audio_mix_mono_from_slots_f32(state->f_l4[2], state->f_r4[2], 160, l_on, r_on, mono3);
            if (opts->audio_out_type == 0) { // Pulse mono
                write_float_audio(opts, mono1, 160);
                write_float_audio(opts, mono2, 160);
                write_float_audio(opts, mono3, 160);
            } else if (opts->audio_out_type == 8) { // UDP mono
                dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(float), mono1);
                dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(float), mono2);
                dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(float), mono3);
            } else if (opts->audio_out_type == 1) { // STDOUT mono
                write_audio_out(opts->audio_out_fd, mono1, (size_t)160u * sizeof(float));
                write_audio_out(opts->audio_out_fd, mono2, (size_t)160u * sizeof(float));
                write_audio_out(opts->audio_out_fd, mono3, (size_t)160u * sizeof(float));
            }
        } else {
            if (opts->audio_out_type == 0) { // Pulse stereo
                write_float_audio(opts, stereo_samp1, 160);
                write_float_audio(opts, stereo_samp2, 160);
                write_float_audio(opts, stereo_samp3, 160);
            } else if (opts->audio_out_type == 8) { // UDP stereo
                dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(float), stereo_samp1);
                dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(float), stereo_samp2);
                dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(float), stereo_samp3);
            } else if (opts->audio_out_type == 1) { // STDOUT stereo
                write_audio_out(opts->audio_out_fd, stereo_samp1, (size_t)320u * sizeof(float));
                write_audio_out(opts->audio_out_fd, stereo_samp2, (size_t)320u * sizeof(float));
                write_audio_out(opts->audio_out_fd, stereo_samp3, (size_t)320u * sizeof(float));
            }
        }
    }

FS3_END:

    memset(state->audio_out_temp_buf, 0.0f, sizeof(state->audio_out_temp_buf));
    memset(state->audio_out_temp_bufR, 0.0f, sizeof(state->audio_out_temp_bufR));

    //set float temp buffer to baseline
    memset(state->f_l4, 0.0f, sizeof(state->f_l4));
    memset(state->f_r4, 0.0f, sizeof(state->f_r4));

    //if we don't run processAudio, then I don't think we really need any of the items below active
    state->audio_out_idx = 0;
    state->audio_out_idxR = 0;

    if (state->audio_out_idx2 >= 800000) {
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
    }

    if (state->audio_out_idx2R >= 800000) {
        state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
        state->audio_out_buf_pR = state->audio_out_bufR + 100;
        memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
        memset(state->audio_out_bufR, 0, 100 * sizeof(short));
        state->audio_out_idx2R = 0;
    }
}

//NOTE: On FS4 and SS4 voice, the longer the transmission, the more the function will start to lag
//the entire DSD-neo loop due to the skipping of playback on SACCH frames (causes noticeable skip when it does play them),
//this isn't a major problem, since the buffer can handle it, but it does delay return to CC until the end
//of the call on busy systems where both VCH slots are constantly busy with voice
//the longer the call, the more delayed until returning to the control channel

//NOTE: Disabling voice synthesis clears up the delay issue (obviosly since we aren't having to wait on it to play)
//disabling voice in only one slot will also fix most random stutter from the 4v in one slot, and 2v in the other slot

//NOTE: The same skip may be occurring on the main and v2.1b branches of DSD-neo as well, so that may be due to the 4v/2v and
//playing back immediately instead of buffering x number of samples or 4v/2v to get a smoother playback

//NOTE: When using capture bins for playback, this issue is not as observable compared to real time reception due to how fast
//we can blow through pure data on bin files compared to waiting for the real time reception

//its usually a lot more noticeable on dual voices than single (probably due to various arrangements of dual 4v/2v in each superframe)

//float stereo mix 4v2 P25p2
void
playSynthesizedVoiceFS4(dsd_opts* opts, dsd_state* state) {

    //NOTE: This will run for every TS % 2, except on SACCH inverted slots (10 and 11)
    //WIP: Get the real TS number out of the P25p2 frame, and not our ts_counter values

    int i, encL, encR;
    float stereo_samp1[320]; //8k 2-channel stereo interleave mix
    float stereo_samp2[320]; //8k 2-channel stereo interleave mix
    float stereo_samp3[320]; //8k 2-channel stereo interleave mix
    float stereo_samp4[320]; //8k 2-channel stereo interleave mix

    float empty[320]; //this is used to see if we want to play a single 2v or double 2v or not

    // Per-slot audio gating for P25p2:
    // Use the centralized gate set by SACCH/FACCH/ESS logic so encrypted
    // slot mute (lockout) never impacts the clear slot. This mirrors FS3 behavior.
    encL = state->p25_p2_audio_allowed[0] ? 0 : 1;
    encR = state->p25_p2_audio_allowed[1] ? 0 : 1;

    //CHEAT: Using the slot on/off, use that to set encL or encR back on
    //as a simple way to turn off voice synthesis in a particular slot
    //its not really 'disabled', we just aren't playing it
    if (opts->slot1_on == 0) {
        encL = 1;
    }
    if (opts->slot2_on == 0) {
        encR = 1;
    }

    unsigned long TGL = (unsigned long)state->lasttg;
    unsigned long TGR = (unsigned long)state->lasttgR;

    // Apply whitelist/TG-hold gating shared with other mixers.
    (void)dsd_audio_group_gate_dual(opts, state, TGL, TGR, encL, encR, &encL, &encR);

    memset(stereo_samp1, 0.0f, sizeof(stereo_samp1));
    memset(stereo_samp2, 0.0f, sizeof(stereo_samp2));
    memset(stereo_samp3, 0.0f, sizeof(stereo_samp3));
    memset(stereo_samp4, 0.0f, sizeof(stereo_samp4));

    memset(empty, 0.0f, sizeof(empty));

    // Drain up to 4 frames from per-slot jitter buffers and interleave to stereo
    float lf[4][160];
    float rf[4][160];
    int l_ok[4] = {0, 0, 0, 0};
    int r_ok[4] = {0, 0, 0, 0};
    for (int j = 0; j < 4; j++) {
        l_ok[j] = p25_p2_audio_ring_pop(state, 0, lf[j]);
        r_ok[j] = p25_p2_audio_ring_pop(state, 1, rf[j]);
        if (l_ok[j]) {
            agf(opts, state, lf[j], 0);
        }
        if (r_ok[j]) {
            agf(opts, state, rf[j], 1);
        }
    }

    int encL0 = (encL || !l_ok[0]) ? 1 : 0;
    int encR0 = (encR || !r_ok[0]) ? 1 : 0;
    int encL1 = (encL || !l_ok[1]) ? 1 : 0;
    int encR1 = (encR || !r_ok[1]) ? 1 : 0;
    int encL2 = (encL || !l_ok[2]) ? 1 : 0;
    int encR2 = (encR || !r_ok[2]) ? 1 : 0;
    int encL3 = (encL || !l_ok[3]) ? 1 : 0;
    int encR3 = (encR || !r_ok[3]) ? 1 : 0;

    audio_mix_interleave_stereo_f32(lf[0], rf[0], 160, encL0, encR0, stereo_samp1);
    audio_mix_interleave_stereo_f32(lf[1], rf[1], 160, encL1, encR1, stereo_samp2);
    audio_mix_interleave_stereo_f32(lf[2], rf[2], 160, encL2, encR2, stereo_samp3);
    audio_mix_interleave_stereo_f32(lf[3], rf[3], 160, encL3, encR3, stereo_samp4);

    // If exactly one slot is active (the other enc-muted), duplicate the
    // active slot onto both channels so users with stereo sinks hear it.
    if (!encL && encR) {
        for (i = 0; i < 320; i += 2) {
            stereo_samp1[i + 1] = stereo_samp1[i + 0];
            stereo_samp2[i + 1] = stereo_samp2[i + 0];
            stereo_samp3[i + 1] = stereo_samp3[i + 0];
            stereo_samp4[i + 1] = stereo_samp4[i + 0];
        }
        encR = 0; // treat as stereo-duplicated
    } else if (encL && !encR) {
        for (i = 0; i < 320; i += 2) {
            stereo_samp1[i + 0] = stereo_samp1[i + 1];
            stereo_samp2[i + 0] = stereo_samp2[i + 1];
            stereo_samp3[i + 0] = stereo_samp3[i + 1];
            stereo_samp4[i + 0] = stereo_samp4[i + 1];
        }
        encL = 0; // treat as stereo-duplicated
    }

    if (encL && encR) {
        goto END_FS4;
    }

    // If output is mono, mix active channels into one buffer per frame span
    if (opts->pulse_digi_out_channels == 1) {
        float mono1[160], mono2[160], mono3[160], mono4[160];
        memset(mono1, 0, sizeof(mono1));
        memset(mono2, 0, sizeof(mono2));
        memset(mono3, 0, sizeof(mono3));
        memset(mono4, 0, sizeof(mono4));
        int l_on0 = (!encL && l_ok[0]);
        int r_on0 = (!encR && r_ok[0]);
        int l_on1 = (!encL && l_ok[1]);
        int r_on1 = (!encR && r_ok[1]);
        int l_on2 = (!encL && l_ok[2]);
        int r_on2 = (!encR && r_ok[2]);
        int l_on3 = (!encL && l_ok[3]);
        int r_on3 = (!encR && r_ok[3]);
        audio_mix_mono_from_slots_f32(lf[0], rf[0], 160, l_on0, r_on0, mono1);
        audio_mix_mono_from_slots_f32(lf[1], rf[1], 160, l_on1, r_on1, mono2);
        audio_mix_mono_from_slots_f32(lf[2], rf[2], 160, l_on2, r_on2, mono3);
        audio_mix_mono_from_slots_f32(lf[3], rf[3], 160, l_on3, r_on3, mono4);
        if (opts->audio_out == 1 && opts->audio_out_type == 0) { // Pulse Audio mono
            write_float_audio(opts, mono1, 160);
            write_float_audio(opts, mono2, 160);
            if (!dsd_is_all_zero_f(mono3, 160)) {
                write_float_audio(opts, mono3, 160);
            }
            if (!dsd_is_all_zero_f(mono4, 160)) {
                write_float_audio(opts, mono4, 160);
            }
        } else if (opts->audio_out == 1 && opts->audio_out_type == 8) { // UDP mono
            dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(float), mono1);
            dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(float), mono2);
            if (!dsd_is_all_zero_f(mono3, 160)) {
                dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(float), mono3);
            }
            if (!dsd_is_all_zero_f(mono4, 160)) {
                dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(float), mono4);
            }
        } else if (opts->audio_out == 1 && opts->audio_out_type == 1) { // STDOUT mono
            write_audio_out(opts->audio_out_fd, mono1, (size_t)160u * sizeof(float));
            write_audio_out(opts->audio_out_fd, mono2, (size_t)160u * sizeof(float));
            if (!dsd_is_all_zero_f(mono3, 160)) {
                write_audio_out(opts->audio_out_fd, mono3, (size_t)160u * sizeof(float));
            }
            if (!dsd_is_all_zero_f(mono4, 160)) {
                write_audio_out(opts->audio_out_fd, mono4, (size_t)160u * sizeof(float));
            }
        }
        goto END_FS4;
    }

    // Stereo output (2ch)
    if (opts->audio_out == 1 && opts->audio_out_type == 0) //Pulse Audio
    {
        write_float_audio(opts, stereo_samp1, 160);
        write_float_audio(opts, stereo_samp2, 160);
        if (!dsd_is_all_zero_f(stereo_samp3, 320)) {
            write_float_audio(opts, stereo_samp3, 160);
        }
        if (!dsd_is_all_zero_f(stereo_samp4, 320)) {
            write_float_audio(opts, stereo_samp4, 160);
        }
    }

    if (opts->audio_out == 1 && opts->audio_out_type == 8) //UDP Audio
    {
        dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(float), stereo_samp1);
        dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(float), stereo_samp2);
        if (!dsd_is_all_zero_f(stereo_samp3, 320)) {
            dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(float), stereo_samp3);
        }
        if (!dsd_is_all_zero_f(stereo_samp4, 320)) {
            dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(float), stereo_samp4);
        }
    }

    if (opts->audio_out == 1 && opts->audio_out_type == 1) //STDOUT
    {
        write_audio_out(opts->audio_out_fd, stereo_samp1, (size_t)320u * sizeof(float));
        write_audio_out(opts->audio_out_fd, stereo_samp2, (size_t)320u * sizeof(float));
        if (!dsd_is_all_zero_f(stereo_samp3, 320)) {
            write_audio_out(opts->audio_out_fd, stereo_samp3, (size_t)320u * sizeof(float));
        }
        if (!dsd_is_all_zero_f(stereo_samp4, 320)) {
            write_audio_out(opts->audio_out_fd, stereo_samp4, (size_t)320u * sizeof(float));
        }
    }

END_FS4:

    memset(state->audio_out_temp_buf, 0.0f, sizeof(state->audio_out_temp_buf));
    memset(state->audio_out_temp_bufR, 0.0f, sizeof(state->audio_out_temp_bufR));

    //set float temp buffer to baseline
    memset(state->f_l4, 0.0f, sizeof(state->f_l4));
    memset(state->f_r4, 0.0f, sizeof(state->f_r4));

    //if we don't run processAudio, then I don't think we really need any of the items below active
    state->audio_out_idx = 0;
    state->audio_out_idxR = 0;

    if (state->audio_out_idx2 >= 800000) {
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
    }

    if (state->audio_out_idx2R >= 800000) {
        state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
        state->audio_out_buf_pR = state->audio_out_bufR + 100;
        memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
        memset(state->audio_out_bufR, 0, 100 * sizeof(short));
        state->audio_out_idx2R = 0;
    }
}

//float stereo mix -- when using Float Stereo Output, we need to send P25p1, DMR MS/Simplex, DStar, and YSF here
void
playSynthesizedVoiceFS(dsd_opts* opts, dsd_state* state) {

    int encL;
    float stereo_samp1[320]; //8k 2-channel stereo interleave mix

    memset(stereo_samp1, 0.0f, sizeof(stereo_samp1));

    //TODO: ENC Check on P25p1, DMR MS, etc
    encL = 0;

    //Enc Checkdown -- P25p1 when run with -ft -y switch
    if (DSD_SYNC_IS_P25P1(state->synctype)) {
        if (state->payload_algid != 0 && state->payload_algid != 0x80) {
            encL = 1;
        }
    }

    //checkdown to see if we can lift the 'mute' if a key is available
    if (encL) {
        if (state->payload_algid == 0xAA || state->payload_algid == 0x81 || state->payload_algid == 0x9F) {
            if (state->R != 0) {
                encL = 0;
            }
        } else if (state->payload_algid == 0x84 || state->payload_algid == 0x89 || state->payload_algid == 0x83) {
            if (state->aes_key_loaded[0] == 1) {
                encL = 0;
            }
        }
    }

    //TODO: add option to bypass enc with a toggle as well

    if (opts->slot1_on == 0) {
        encL = 1;
    }

    unsigned long TGL = (unsigned long)state->lasttg;
    (void)dsd_audio_group_gate_mono(opts, state, TGL, encL, &encL);

    //run autogain on the f_ buffers
    agf(opts, state, state->f_l, 0);

    //at this point, if both channels are still flagged as enc, then we can skip all playback/writing functions
    if (encL) {
        goto FS_END;
    }

    //interleave left and right channels from the temp (float) buffer with makeshift 'volume' decimation
    audio_mono_to_stereo_f32(state->f_l, stereo_samp1, 160);
    audio_apply_gain_f32(stereo_samp1, 320, 0.5f);

    if (opts->audio_out == 1) {
        if (opts->audio_out_type == 0) { //Pulse Audio
            write_float_audio(opts, stereo_samp1, 160);
        }

        if (opts->audio_out_type == 8) { //UDP Audio
            dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(float), stereo_samp1);
        }

        if (opts->audio_out_type == 1) {
            write_audio_out(opts->audio_out_fd, stereo_samp1, (size_t)320u * sizeof(float));
        }
    }

FS_END:

    memset(state->audio_out_temp_buf, 0.0f, sizeof(state->audio_out_temp_buf));
    memset(state->audio_out_temp_bufR, 0.0f, sizeof(state->audio_out_temp_bufR));

    //set float temp buffer to baseline
    memset(state->f_l4, 0.0f, sizeof(state->f_l4));
    memset(state->f_r4, 0.0f, sizeof(state->f_r4));

    //if we don't run processAudio, then I don't think we really need any of the items below active
    state->audio_out_idx = 0;
    state->audio_out_idxR = 0;

    if (state->audio_out_idx2 >= 800000) {
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
    }

    if (state->audio_out_idx2R >= 800000) {
        state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
        state->audio_out_buf_pR = state->audio_out_bufR + 100;
        memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
        memset(state->audio_out_bufR, 0, 100 * sizeof(short));
        state->audio_out_idx2R = 0;
    }
}

void
playSynthesizedVoiceFM(dsd_opts* opts, dsd_state* state) {

    agf(opts, state, state->f_l, 0);

    //mono may need an additional decimation
    // for (int i = 0; i < 160; i++)
    //   state->f_l[i] *= 0.5f;

    int encL = 0;

    //Enc Checkdown -- P25p1 when run with -ft -y switch
    if (DSD_SYNC_IS_P25P1(state->synctype)) {
        if (state->payload_algid != 0 && state->payload_algid != 0x80) {
            encL = 1;
        }
    }

    //NXDN
    if (state->nxdn_cipher_type != 0) {
        encL = 1;
    }

    //checkdown to see if we can lift the 'mute' if a key is available
    if (encL) {
        if (state->payload_algid == 0xAA || state->payload_algid == 0x81 || state->payload_algid == 0x83
            || state->payload_algid == 0x9F || state->nxdn_cipher_type == 0x1 || state->nxdn_cipher_type == 0x2) {
            if (state->R != 0) {
                encL = 0;
            }
        } else if (state->payload_algid == 0x84 || state->payload_algid == 0x89 || state->nxdn_cipher_type == 0x3) {
            if (state->aes_key_loaded[0] == 1) {
                encL = 0;
            }
        }
    }

    unsigned long TGL = (unsigned long)state->lasttg;
    if (opts->frame_nxdn48 == 1 || opts->frame_nxdn96 == 1) {
        TGL = (unsigned long)state->nxdn_last_tg;
    }

    (void)dsd_audio_group_gate_mono(opts, state, TGL, encL, &encL);

    if (encL == 1) {
        goto vfm_end;
    }

    if (opts->slot1_on == 0) {
        goto vfm_end;
    }

    if (opts->audio_out == 1) {
        if (opts->audio_out_type == 0) {
            write_float_audio(opts, state->f_l, 160);
        }

        if (opts->audio_out_type == 8) { //UDP Audio
            dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(float), state->f_l);
        }

        if (opts->audio_out_type == 1) {
            write_audio_out(opts->audio_out_fd, state->f_l, (size_t)160u * sizeof(float));
        }
    }

vfm_end:

    if (state->audio_out_idx2 >= 800000) {
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
    }

    memset(state->f_l, 0.0f, sizeof(state->f_l));
    memset(state->audio_out_temp_buf, 0.0f, sizeof(state->audio_out_temp_buf));
}

//Mono - Short (SB16LE) - Drop-in replacement for playSyntesizedVoice, but easier to manipulate
void
playSynthesizedVoiceMS(dsd_opts* opts, dsd_state* state) {
    int i;
    size_t len = state->audio_out_idx;
    if (len > 960) {
        len = 960; // clamp to buffer capacity
    }

    //debug
    // fprintf (stderr, " L LEN: %d", len);

    short mono_samp_buf[960];
    short* mono_samp = mono_samp_buf;
    memset(mono_samp, 0, len * sizeof(short));

    if (opts->slot1_on == 0) {
        goto MS_END;
    }

    if (len == 160) {
        for (size_t j = 0; j < len; j++) {
            mono_samp[j] = state->s_l[j];
        }
    } else if (len == 960) {
        state->audio_out_buf_p -= 960; //rewind first
        for (size_t j = 0; j < len; j++) {
            mono_samp[j] = *state->audio_out_buf_p;
            state->audio_out_buf_p++;
        }
    }

    if (opts->use_hpf_d == 1) {
        hpf_dL(state, state->s_l, len);
    }

    if (opts->audio_out == 1) {
        if (opts->audio_out_type == 0) { //Pulse Audio
            write_s16_audio(opts, mono_samp, (size_t)len);
        }

        if (opts->audio_out_type == 8) { //UDP Audio
            dsd_udp_audio_hook_blast(opts, state, (size_t)len * sizeof(short), mono_samp);
        }

        if (opts->audio_out_type == 1) {
            write_audio_out(opts->audio_out_fd, mono_samp, (size_t)len * sizeof(short));
        }
    }

    if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
        //convert to stereo for new static wav file setup
        short ss[320];
        memset(ss, 0, sizeof(ss));
        if (len == 160) {
            for (i = 0; i < 160; i++) {
                ss[(i * 2) + 0] = mono_samp[i];
                ss[(i * 2) + 1] = mono_samp[i];
            }
        } else if (len == 960) {
            for (i = 0; i < 160; i++) {
                ss[(i * 2) + 0] = mono_samp[(size_t)i * 6]; //grab every 6th sample to downsample
                ss[(i * 2) + 1] = mono_samp[(size_t)i * 6]; //grab every 6th sample to downsample
            }
        }
        sf_write_short(opts->wav_out_f, ss, 320);
    }

MS_END:

    //run cleanup since we pulled stuff from processAudio
    state->audio_out_idx = 0;

    //set short temp buffer to baseline
    memset(state->s_l, 0, sizeof(state->s_l));

    if (state->audio_out_idx2 >= 800000) {
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
    }
}

//Mono - Short (SB16LE) - Drop-in replacement for playSyntesizedVoiceR, but easier to manipulate
void
playSynthesizedVoiceMSR(dsd_opts* opts, dsd_state* state) {
    int i;
    size_t len = state->audio_out_idxR;
    if (len > 960) {
        len = 960; // clamp to buffer capacity
    }

    //debug
    // fprintf (stderr, " R LEN: %d", len);

    short mono_samp_buf[960];
    short* mono_samp = mono_samp_buf;
    memset(mono_samp, 0, len * sizeof(short));

    if (opts->slot2_on == 0) {
        goto MS_ENDR;
    }

    if (len == 160) {
        for (size_t j = 0; j < len; j++) {
            mono_samp[j] = state->s_r[j];
        }
    } else if (len == 960) {
        state->audio_out_buf_pR -= 960; //rewind first
        for (size_t j = 0; j < len; j++) {
            mono_samp[j] = *state->audio_out_buf_pR;
            state->audio_out_buf_pR++;
        }
    }

    if (opts->use_hpf_d == 1) {
        hpf_dR(state, mono_samp, len);
    }

    if (opts->audio_out == 1) {
        if (opts->audio_out_type == 0) { //Pulse Audio
            write_s16_audio(opts, mono_samp, (size_t)len);
        }

        if (opts->audio_out_type == 8) { //UDP Audio
            dsd_udp_audio_hook_blast(opts, state, (size_t)len * sizeof(short), mono_samp);
        }

        if (opts->audio_out_type == 1) {
            write_audio_out(opts->audio_out_fd, mono_samp, (size_t)len * sizeof(short));
        }
    }

    if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
        //convert to stereo for new static wav file setup
        short ss[320];
        memset(ss, 0, sizeof(ss));
        if (len == 160) {
            for (i = 0; i < 160; i++) {
                ss[(i * 2) + 0] = mono_samp[i];
                ss[(i * 2) + 1] = mono_samp[i];
            }
        } else if (len == 960) {
            for (i = 0; i < 160; i++) {
                ss[(i * 2) + 0] = mono_samp[(size_t)i * 6]; //grab every 6th sample to downsample
                ss[(i * 2) + 1] = mono_samp[(size_t)i * 6]; //grab every 6th sample to downsample
            }
        }
        sf_write_short(opts->wav_out_f, ss, 320);
    }

MS_ENDR:

    //run cleanup since we pulled stuff from processAudioR
    state->audio_out_idxR = 0;

    //set short temp buffer to baseline
    memset(state->s_r, 0, sizeof(state->s_r));

    if (state->audio_out_idx2R >= 800000) {
        state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
        state->audio_out_buf_pR = state->audio_out_bufR + 100;
        memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
        memset(state->audio_out_bufR, 0, 100 * sizeof(short));
        state->audio_out_idx2R = 0;
    }
}

//Stereo Mix - Short (SB16LE) -- When Playing Short FDMA samples when setup for stereo output
void
playSynthesizedVoiceSS(dsd_opts* opts, dsd_state* state) {

    int encL;
    short stereo_samp1[320]; //8k 2-channel stereo interleave mix
    memset(stereo_samp1, 0, sizeof(stereo_samp1));

    //enc checkdown for whether or not to fill the stereo sample or not for playback or writing
    encL = 0;

    //Enc Checkdown -- P25p1 when run with -ft switch
    if (DSD_SYNC_IS_P25P1(state->synctype)) {
        if (state->payload_algid != 0 && state->payload_algid != 0x80) {
            encL = 1;
        }
    }

    //checkdown to see if we can lift the 'mute' if a key is available
    if (encL) {
        if (state->payload_algid == 0xAA || state->payload_algid == 0x81 || state->payload_algid == 0x9F
            || state->payload_algid == 0x83) {
            if (state->R != 0) {
                encL = 0;
            }
        } else if (state->payload_algid == 0x84 || state->payload_algid == 0x89) {
            if (state->aes_key_loaded[0] == 1) {
                encL = 0;
            }
        }
    }
    //TODO: add option to bypass enc with a toggle as well

    if (opts->slot1_on == 0) {
        encL = 1;
    }

    unsigned long TGL = (unsigned long)state->lasttg;

    (void)dsd_audio_group_gate_mono(opts, state, TGL, encL, &encL);

    //test hpf
    if (opts->use_hpf_d == 1) {
        hpf_dL(state, state->s_l, 160);
    }

    //interleave left and right channels from the short storage area
    audio_mono_to_stereo_s16(state->s_l, stereo_samp1, 160);

    //at this point, if still flagged as enc, then we can skip all playback/writing functions
    if (encL) {
        goto SSM_END;
    }

    if (opts->audio_out == 1) {
        if (opts->audio_out_type == 0) { //Pulse Audio
            write_s16_audio(opts, stereo_samp1, 160);
        }

        if (opts->audio_out_type == 8) { //UDP Audio
            dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(short), stereo_samp1);
        }

        if (opts->audio_out_type == 1) {
            write_audio_out(opts->audio_out_fd, stereo_samp1, (size_t)320u * sizeof(short));
        }
    }

    if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
        sf_write_short(opts->wav_out_f, stereo_samp1, 320);
    }

SSM_END:

    //run cleanup since we pulled stuff from processAudio
    state->audio_out_idx = 0;
    state->audio_out_idxR = 0;

    //set short temp buffer to baseline
    memset(state->s_l, 0, sizeof(state->s_l));
    memset(state->s_r, 0, sizeof(state->s_r));

    if (state->audio_out_idx2 >= 800000) {
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
    }

    if (state->audio_out_idx2R >= 800000) {
        state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
        state->audio_out_buf_pR = state->audio_out_bufR + 100;
        memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
        memset(state->audio_out_bufR, 0, 100 * sizeof(short));
        state->audio_out_idx2R = 0;
    }
}

//short stereo mix 3v2 DMR
void
playSynthesizedVoiceSS3(dsd_opts* opts, dsd_state* state) {

    //NOTE: This runs once for every two timeslots, if we are in the BS voice loop
    //it doesn't matter if both slots have voice, or if one does, the slot without voice
    //will play silence while this runs if no voice present

    int encL, encR;
    short stereo_samp1[320]; //8k 2-channel stereo interleave mix
    short stereo_samp2[320]; //8k 2-channel stereo interleave mix
    short stereo_samp3[320]; //8k 2-channel stereo interleave mix

    memset(stereo_samp1, 0, sizeof(stereo_samp1));
    memset(stereo_samp2, 0, sizeof(stereo_samp2));
    memset(stereo_samp3, 0, sizeof(stereo_samp3));

    //dmr enc checkdown for whether or not to fill the stereo sample or not for playback or writing
    encL = (state->dmr_so >> 6) & 0x1;
    encR = (state->dmr_soR >> 6) & 0x1;

    //checkdown to see if we can lift the 'mute' if a key is available
    if (encL) {
        if (state->payload_algid == 0) {
            if (state->K != 0 || state->K1 != 0) {
                encL = 0;
            }
        } else if (state->payload_algid == 0x02 || state->payload_algid == 0x21 || state->payload_algid == 0x22) {
            if (state->R != 0) {
                encL = 0;
            }
        } else if (state->payload_algid == 0x24 || state->payload_algid == 0x25) {
            //going to need a better check for this later on, or seperated keys or something
            if (state->aes_key_loaded[0] == 1) {
                encL = 0;
            }
        }
    }

    if (encR) {
        if (state->payload_algidR == 0) {
            if (state->K != 0 || state->K1 != 0) {
                encR = 0;
            }
        } else if (state->payload_algidR == 0x02 || state->payload_algidR == 0x21 || state->payload_algidR == 0x22) {
            if (state->RR != 0) {
                encR = 0;
            }
        } else if (state->payload_algidR == 0x24 || state->payload_algidR == 0x25) {
            //going to need a better check for this later on, or seperated keys or something
            if (state->aes_key_loaded[1] == 1) {
                encR = 0;
            }
        }
    }

    //debug for AES
    // encL = encR = 0;

    //TODO: add option to bypass enc with a toggle as well

    //CHEAT: Using the slot on/off, use that to set encL or encR back on
    //as a simple way to turn off voice synthesis in a particular slot
    //its not really 'disabled', we just aren't playing it
    // if (opts->slot1_on == 0) encL = 1;
    // if (opts->slot2_on == 0) encR = 1;

    unsigned long TGL = (unsigned long)state->lasttg;
    unsigned long TGR = (unsigned long)state->lasttgR;

    (void)dsd_audio_group_gate_dual(opts, state, TGL, TGR, encL, encR, &encL, &encR);

    //check to see if we need to enable slot and toggle slot preference here
    //this method will always favor slot 2 (this is a patch anyways, so....meh)
    // if (strcmp(modeL, "A") == 0)
    // {
    //   opts->slot1_on = 1;
    //   opts->slot_preference = 0;
    // }
    // if (strcmp(modeR, "A") == 0)
    // {
    //   opts->slot2_on = 1;
    //   opts->slot_preference = 1;
    // }

    //check to see if we need to enable slot and toggle slot preference here
    //if both groups allowed, then give no preference to either one (not sure this is needed now)
    // if ( (strcmp(modeL, "A") == 0) && (strcmp(modeR, "A") == 0) )
    // {
    //   opts->slot1_on = 1;
    //   opts->slot2_on = 1;
    //   opts->slot_preference = 2;
    // }
    // else if (strcmp(modeL, "A") == 0)
    // {
    //   opts->slot1_on = 1;
    //   opts->slot_preference = 0;
    // }
    // else if (strcmp(modeR, "A") == 0)
    // {
    //   opts->slot2_on = 1;
    //   opts->slot_preference = 1;
    // }
    // else //if any other condition, then give no preference to either one
    // {
    //   opts->slot1_on = 1;
    //   opts->slot2_on = 1;
    //   opts->slot_preference = 2;
    // }

    //if TG Hold in place, mute anything but that TG #132
    if (state->tg_hold != 0 && state->tg_hold != TGL) {
        encL = 1;
    }
    if (state->tg_hold != 0 && state->tg_hold != TGR) {
        encR = 1;
    }

    //likewise, override and unmute if TG hold matches TG (and turn on slot and set preference)
    if (state->tg_hold != 0 && state->tg_hold == TGL) {
        encL = 0;
        opts->slot1_on = 1;
        opts->slot_preference = 0;
    } else if (state->tg_hold != 0 && state->tg_hold == TGR) {
        encR = 0;
        opts->slot2_on = 1;
        opts->slot_preference = 1;
    } else //otherwise, reset slot preference to either or (both slots enabled)
    {
        opts->slot_preference = 2;
    }

    //test hpf
    if (opts->use_hpf_d == 1) {
        hpf_dL(state, state->s_l4[0], 160);
        hpf_dL(state, state->s_l4[1], 160);
        hpf_dL(state, state->s_l4[2], 160);

        hpf_dR(state, state->s_r4[0], 160);
        hpf_dR(state, state->s_r4[1], 160);
        hpf_dR(state, state->s_r4[2], 160);
    }

//convert the left or right channel to both channels if single voice under certain conditions, if defined to do so
#define DMR_STEREO_OUTPUT

#ifdef DMR_STEREO_OUTPUT
    if (encL) {
        memset(state->s_l4, 0, sizeof(state->s_l4));
    }
    if (encR) {
        memset(state->s_r4, 0, sizeof(state->s_r4));
    }
    //this is for playing single voice over both channels, or when to keep them separated
    if ((opts->slot1_on == 0 && opts->slot2_on == 1 && encR == 0)                     //slot 1 off, slot 2 on
        || (opts->slot_preference == 1 && state->dmrburstR == 16 && encR == 0)        //prefer slot 2, voice right
        || (state->dmrburstR == 16 && state->dmrburstL != 16 && encR == 0)) {         //voice right only
        memcpy(state->s_l4, state->s_r4, sizeof(state->s_l4));                        //copy right to left
    } else if ((opts->slot1_on == 1 && opts->slot2_on == 0 && encL == 0)              //slot 2 off, slot 1 on
               || (opts->slot_preference == 0 && state->dmrburstL == 16 && encL == 0) //prefer slot 1, voice left
               || (state->dmrburstL == 16 && state->dmrburstR != 16 && encL == 0)) {  //voice left only
        memcpy(state->s_r4, state->s_l4, sizeof(state->s_r4));                        //copy left to right
    }
    //else if voice in both, and both slots on, and no preference on slot, then regular stereo interleave (left and right channels)

    //if both slots are the same now, then let's decimate the audio to keep the audio level consistent
    // if (memcmp (state->s_l4, state->s_r4, sizeof(state->s_l4)) == 0)
    // {
    //   for (int j = 0; j < 3; j++)
    //   {
    //     for (i = 0; i < 160; i++)
    //     {
    //       state->s_l4[j][i] *= 0.85;
    //       state->s_r4[j][i] *= 0.85;
    //     }
    //   }
    // }

#endif

    //check this last
    if (opts->slot1_on == 0 && opts->slot2_on == 0) //both slots are hard off, disable playback
    {
        encL = 1;
        encR = 1;
    }

    //at this point, if both channels are still flagged as enc, then we can skip all playback/writing functions
    if (encL && encR) {
        goto SS3_END;
    }

    //test hpf
    // if (opts->use_hpf_d == 1)
    // {
    //   hpf_dL(state, state->s_l4[0], 160);
    //   hpf_dL(state, state->s_l4[1], 160);
    //   hpf_dL(state, state->s_l4[2], 160);

    //   hpf_dR(state, state->s_r4[0], 160);
    //   hpf_dR(state, state->s_r4[1], 160);
    //   hpf_dR(state, state->s_r4[2], 160);
    // }

    //interleave left and right channels from the short storage area
    {
#ifdef DMR_STEREO_OUTPUT
        // Under DMR_STEREO_OUTPUT the per-slot buffers are already zeroed
        // for muted slots, so always mix both channels.
        int mix_encL = 0;
        int mix_encR = 0;
#else
        int mix_encL = encL;
        int mix_encR = encR;
#endif
        audio_mix_interleave_stereo_s16(state->s_l4[0], state->s_r4[0], 160, mix_encL, mix_encR, stereo_samp1);
        audio_mix_interleave_stereo_s16(state->s_l4[1], state->s_r4[1], 160, mix_encL, mix_encR, stereo_samp2);
        audio_mix_interleave_stereo_s16(state->s_l4[2], state->s_r4[2], 160, mix_encL, mix_encR, stereo_samp3);
    }

    if (opts->audio_out == 1 && opts->audio_out_type == 0) //Pulse Audio
    {
        write_s16_audio(opts, stereo_samp1, 160);
        write_s16_audio(opts, stereo_samp2, 160);
        write_s16_audio(opts, stereo_samp3, 160);
    }

    if (opts->audio_out == 1 && opts->audio_out_type == 8) //UDP Audio
    {
        dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(short), stereo_samp1);
        dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(short), stereo_samp2);
        dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(short), stereo_samp3);
    }

    if (opts->audio_out == 1 && opts->audio_out_type == 1) {
        write_audio_out(opts->audio_out_fd, stereo_samp1, (size_t)320u * sizeof(short));
        write_audio_out(opts->audio_out_fd, stereo_samp2, (size_t)320u * sizeof(short));
        write_audio_out(opts->audio_out_fd, stereo_samp3, (size_t)320u * sizeof(short));
    }

    if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
        sf_write_short(opts->wav_out_f, stereo_samp1, 320);
        sf_write_short(opts->wav_out_f, stereo_samp2, 320);
        sf_write_short(opts->wav_out_f, stereo_samp3, 320);
    }

SS3_END:

    //run cleanup since we pulled stuff from processAudio
    state->audio_out_idx = 0;
    state->audio_out_idxR = 0;

    //set short temp buffer to baseline
    memset(state->s_l4, 0, sizeof(state->s_l4));
    memset(state->s_r4, 0, sizeof(state->s_r4));

    if (state->audio_out_idx2 >= 800000) {
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
    }

    if (state->audio_out_idx2R >= 800000) {
        state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
        state->audio_out_buf_pR = state->audio_out_bufR + 100;
        memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
        memset(state->audio_out_bufR, 0, 100 * sizeof(short));
        state->audio_out_idx2R = 0;
    }
}

//short stereo mix 4v2 P25p2
void
playSynthesizedVoiceSS4(dsd_opts* opts, dsd_state* state) {

    //NOTE: This runs once for every two timeslots, if we are in the BS voice loop
    //it doesn't matter if both slots have voice, or if one does, the slot without voice
    //will play silence while this runs if no voice present

    int i;
    int encL, encR;
    short stereo_samp1[320]; //8k 2-channel stereo interleave mix
    short stereo_samp2[320]; //8k 2-channel stereo interleave mix
    short stereo_samp3[320]; //8k 2-channel stereo interleave mix
    short stereo_samp4[320];
    short empss[160]; //this is used to see if we want to run HPF on an empty set
    short empty[320]; //this is used to see if we want to play a single 2v or double 2v or not

    memset(stereo_samp1, 0, sizeof(stereo_samp1));
    memset(stereo_samp2, 0, sizeof(stereo_samp2));
    memset(stereo_samp3, 0, sizeof(stereo_samp3));
    memset(stereo_samp4, 0, sizeof(stereo_samp4));

    memset(empty, 0, sizeof(empty));
    memset(empss, 0, sizeof(empss));

    // P25p2 per-slot gate: mirror FS4 float behavior using centralized
    // p25_p2_audio_allowed flags so ENC lockout on one slot never mutes the
    // clear slot in short/16-bit output modes either.
    encL = state->p25_p2_audio_allowed[0] ? 0 : 1;
    encR = state->p25_p2_audio_allowed[1] ? 0 : 1;

    //TODO: add option to bypass enc with a toggle as well

    //CHEAT: Using the slot on/off, use that to set encL or encR back on
    //as a simple way to turn off voice synthesis in a particular slot
    //its not really 'disabled', we just aren't playing it
    if (opts->slot1_on == 0) {
        encL = 1;
    }
    if (opts->slot2_on == 0) {
        encR = 1;
    }

    //WIP: Mute if on B list (or not W list)
    char modeL[8];
    sprintf(modeL, "%s", "");
    char modeR[8];
    sprintf(modeR, "%s", "");

    unsigned long TGL = (unsigned long)state->lasttg;
    unsigned long TGR = (unsigned long)state->lasttgR;

    //if we are using allow/whitelist mode, then write 'B' to mode for block
    //comparison below will look for an 'A' to write to mode if it is allowed
    if (opts->trunk_use_allow_list == 1) {
        sprintf(modeL, "%s", "B");
        sprintf(modeR, "%s", "B");
    }

    for (unsigned int gi = 0; gi < state->group_tally; gi++) {
        if (state->group_array[gi].groupNumber == TGL) {
            strncpy(modeL, state->group_array[gi].groupMode, sizeof(modeL) - 1);
            modeL[sizeof(modeL) - 1] = '\0';
            // break; //need to keep going to check other potential slot group
        }
        if (state->group_array[gi].groupNumber == TGR) {
            strncpy(modeR, state->group_array[gi].groupMode, sizeof(modeR) - 1);
            modeR[sizeof(modeR) - 1] = '\0';
            // break; //need to keep going to check other potential slot group
        }
    }

    //flag either left or right as 'enc' to mute if B
    if (strcmp(modeL, "B") == 0) {
        encL = 1;
    }
    if (strcmp(modeR, "B") == 0) {
        encR = 1;
    }

    //if TG Hold in place, mute anything but that TG #132
    if (state->tg_hold != 0 && state->tg_hold != (uint32_t)TGL) {
        encL = 1;
    }
    if (state->tg_hold != 0 && state->tg_hold != (uint32_t)TGR) {
        encR = 1;
    }
    //likewise, override and unmute if TG hold matches TG
    if (state->tg_hold != 0 && state->tg_hold == (uint32_t)TGL) {
        encL = 0;
    }
    if (state->tg_hold != 0 && state->tg_hold == (uint32_t)TGR) {
        encR = 0;
    }

    //test hpf
    if (opts->use_hpf_d == 1) {
        hpf_dL(state, state->s_l4[0], 160);
        hpf_dL(state, state->s_l4[1], 160);
        if (memcmp(empss, state->s_l4[2], sizeof(empss)) != 0) {
            hpf_dL(state, state->s_l4[2], 160);
        }
        if (memcmp(empss, state->s_l4[3], sizeof(empss)) != 0) {
            hpf_dL(state, state->s_l4[3], 160);
        }

        hpf_dR(state, state->s_r4[0], 160);
        hpf_dR(state, state->s_r4[1], 160);
        if (memcmp(empss, state->s_r4[2], sizeof(empss)) != 0) {
            hpf_dR(state, state->s_r4[2], 160);
        }
        if (memcmp(empss, state->s_r4[3], sizeof(empss)) != 0) {
            hpf_dR(state, state->s_r4[3], 160);
        }
    }

    //interleave left and right channels from the short storage area
    audio_mix_interleave_stereo_s16(state->s_l4[0], state->s_r4[0], 160, encL, encR, stereo_samp1);
    audio_mix_interleave_stereo_s16(state->s_l4[1], state->s_r4[1], 160, encL, encR, stereo_samp2);
    audio_mix_interleave_stereo_s16(state->s_l4[2], state->s_r4[2], 160, encL, encR, stereo_samp3);
    audio_mix_interleave_stereo_s16(state->s_l4[3], state->s_r4[3], 160, encL, encR, stereo_samp4);

    // If exactly one slot is active (the other enc-muted), duplicate the
    // active slot onto both channels so users with stereo sinks hear it.
    if (!encL && encR) {
        for (i = 0; i < 320; i += 2) {
            stereo_samp1[i + 1] = stereo_samp1[i + 0];
            stereo_samp2[i + 1] = stereo_samp2[i + 0];
            stereo_samp3[i + 1] = stereo_samp3[i + 0];
            stereo_samp4[i + 1] = stereo_samp4[i + 0];
        }
        encR = 0;
    } else if (encL && !encR) {
        for (i = 0; i < 320; i += 2) {
            stereo_samp1[i + 0] = stereo_samp1[i + 1];
            stereo_samp2[i + 0] = stereo_samp2[i + 1];
            stereo_samp3[i + 0] = stereo_samp3[i + 1];
            stereo_samp4[i + 0] = stereo_samp4[i + 1];
        }
        encL = 0;
    }

    //at this point, if both channels are still flagged as enc, then we can skip all playback/writing functions
    if (encL && encR) {
        goto SS4_END;
    }

    // Handle mono output by collapsing active slot(s) into a single channel
    if (opts->pulse_digi_out_channels == 1) {
        short mono1[160], mono2[160], mono3[160], mono4[160];
        memset(mono1, 0, sizeof(mono1));
        memset(mono2, 0, sizeof(mono2));
        memset(mono3, 0, sizeof(mono3));
        memset(mono4, 0, sizeof(mono4));
        for (i = 0; i < 160; i++) {
            int l1 = (!encL) ? state->s_l4[0][i] : 0;
            int r1 = (!encR) ? state->s_r4[0][i] : 0;
            int l2 = (!encL) ? state->s_l4[1][i] : 0;
            int r2 = (!encR) ? state->s_r4[1][i] : 0;
            int l3 = (!encL) ? state->s_l4[2][i] : 0;
            int r3 = (!encR) ? state->s_r4[2][i] : 0;
            int l4 = (!encL) ? state->s_l4[3][i] : 0;
            int r4 = (!encR) ? state->s_r4[3][i] : 0;
            int m1 = (l1 && !r1) ? l1 : (!l1 && r1) ? r1 : (l1 && r1) ? ((l1 + r1) / 2) : 0;
            int m2 = (l2 && !r2) ? l2 : (!l2 && r2) ? r2 : (l2 && r2) ? ((l2 + r2) / 2) : 0;
            int m3 = (l3 && !r3) ? l3 : (!l3 && r3) ? r3 : (l3 && r3) ? ((l3 + r3) / 2) : 0;
            int m4 = (l4 && !r4) ? l4 : (!l4 && r4) ? r4 : (l4 && r4) ? ((l4 + r4) / 2) : 0;
            // clamp to short
            if (m1 > 32767) {
                m1 = 32767;
            } else if (m1 < -32768) {
                m1 = -32768;
            }
            if (m2 > 32767) {
                m2 = 32767;
            } else if (m2 < -32768) {
                m2 = -32768;
            }
            if (m3 > 32767) {
                m3 = 32767;
            } else if (m3 < -32768) {
                m3 = -32768;
            }
            if (m4 > 32767) {
                m4 = 32767;
            } else if (m4 < -32768) {
                m4 = -32768;
            }
            mono1[i] = (short)m1;
            mono2[i] = (short)m2;
            mono3[i] = (short)m3;
            mono4[i] = (short)m4;
        }
        if (opts->audio_out == 1 && opts->audio_out_type == 0) { // Pulse mono
            write_s16_audio(opts, mono1, 160);
            write_s16_audio(opts, mono2, 160);
            if (memcmp(empss, mono3, sizeof(empss)) != 0) {
                write_s16_audio(opts, mono3, 160);
            }
            if (memcmp(empss, mono4, sizeof(empss)) != 0) {
                write_s16_audio(opts, mono4, 160);
            }
        } else if (opts->audio_out == 1 && opts->audio_out_type == 8) { // UDP mono
            dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(short), mono1);
            dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(short), mono2);
            if (memcmp(empss, mono3, sizeof(empss)) != 0) {
                dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(short), mono3);
            }
            if (memcmp(empss, mono4, sizeof(empss)) != 0) {
                dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(short), mono4);
            }
        } else if (opts->audio_out == 1 && opts->audio_out_type == 1) {
            write_audio_out(opts->audio_out_fd, mono1, (size_t)160u * sizeof(short));
            write_audio_out(opts->audio_out_fd, mono2, (size_t)160u * sizeof(short));
            if (memcmp(empss, mono3, sizeof(empss)) != 0) {
                write_audio_out(opts->audio_out_fd, mono3, (size_t)160u * sizeof(short));
            }
            if (memcmp(empss, mono4, sizeof(empss)) != 0) {
                write_audio_out(opts->audio_out_fd, mono4, (size_t)160u * sizeof(short));
            }
        }
        if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
            sf_write_short(opts->wav_out_f, mono1, 160);
            sf_write_short(opts->wav_out_f, mono2, 160);
            if (memcmp(empss, mono3, sizeof(empss)) != 0) {
                sf_write_short(opts->wav_out_f, mono3, 160);
            }
            if (memcmp(empss, mono4, sizeof(empss)) != 0) {
                sf_write_short(opts->wav_out_f, mono4, 160);
            }
        }
        goto SS4_END;
    }

    // Stereo output (2ch)
    if (opts->audio_out == 1 && opts->audio_out_type == 0) //Pulse Audio
    {
        write_s16_audio(opts, stereo_samp1, 160);
        write_s16_audio(opts, stereo_samp2, 160);
        if (memcmp(empty, stereo_samp3, sizeof(empty)) != 0) {
            write_s16_audio(opts, stereo_samp3, 160);
        }
        if (memcmp(empty, stereo_samp4, sizeof(empty)) != 0) {
            write_s16_audio(opts, stereo_samp4, 160);
        }
    }

    if (opts->audio_out == 1 && opts->audio_out_type == 8) //UDP Audio
    {
        dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(short), stereo_samp1);
        dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(short), stereo_samp2);
        if (memcmp(empty, stereo_samp3, sizeof(empty)) != 0) {
            dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(short), stereo_samp3);
        }
        if (memcmp(empty, stereo_samp4, sizeof(empty)) != 0) {
            dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(short), stereo_samp4);
        }
    }

    if (opts->audio_out == 1 && opts->audio_out_type == 1) {
        write_audio_out(opts->audio_out_fd, stereo_samp1, (size_t)320u * sizeof(short));
        write_audio_out(opts->audio_out_fd, stereo_samp2, (size_t)320u * sizeof(short));
        if (memcmp(empty, stereo_samp3, sizeof(empty)) != 0) {
            write_audio_out(opts->audio_out_fd, stereo_samp3, (size_t)320u * sizeof(short));
        }
        if (memcmp(empty, stereo_samp4, sizeof(empty)) != 0) {
            write_audio_out(opts->audio_out_fd, stereo_samp4, (size_t)320u * sizeof(short));
        }
    }

    if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
        sf_write_short(opts->wav_out_f, stereo_samp1, 320);
        sf_write_short(opts->wav_out_f, stereo_samp2, 320);
        if (memcmp(empty, stereo_samp3, sizeof(empty)) != 0) {
            sf_write_short(opts->wav_out_f, stereo_samp3, 320);
        }
        if (memcmp(empty, stereo_samp4, sizeof(empty)) != 0) {
            sf_write_short(opts->wav_out_f, stereo_samp4, 320);
        }
    }

SS4_END:

    //run cleanup since we pulled stuff from processAudio
    state->audio_out_idx = 0;
    state->audio_out_idxR = 0;

    //set short temp buffer to baseline
    memset(state->s_l4, 0, sizeof(state->s_l4));
    memset(state->s_r4, 0, sizeof(state->s_r4));

    if (state->audio_out_idx2 >= 800000) {
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
    }

    if (state->audio_out_idx2R >= 800000) {
        state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
        state->audio_out_buf_pR = state->audio_out_bufR + 100;
        memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
        memset(state->audio_out_bufR, 0, 100 * sizeof(short));
        state->audio_out_idx2R = 0;
    }
}

//short stereo mix 18v superframe
void
playSynthesizedVoiceSS18(dsd_opts* opts, dsd_state* state) {

    //NOTE: This will run once every superframe during a sacch field
    //exact implementation to be determined

    int j;
    int encL, encR;

    short stereo_sf[18][320]; //8k 2-channel stereo interleave mix for full superframe
    // memset (stereo_sf, 1, 18*sizeof(short)); //I don't think 18*sizeof(short) was large enough, should probably be 18*320*sizeof(short)
    memset(stereo_sf, 0, sizeof(stereo_sf));

    short empty[320];
    memset(empty, 0, sizeof(empty));

    // Per-slot audio gating (P25p2): start from per-slot allowed flags,
    // then apply whitelist/TG-hold rules shared with other mixers.
    encL = state->p25_p2_audio_allowed[0] ? 0 : 1;
    encR = state->p25_p2_audio_allowed[1] ? 0 : 1;

    unsigned long TGL = (unsigned long)state->lasttg;
    unsigned long TGR = (unsigned long)state->lasttgR;

    (void)dsd_audio_group_gate_dual(opts, state, TGL, TGR, encL, encR, &encL, &encR);

    //check to see if we need to enable slot and toggle slot preference here
    //this method will always favor slot 2 (this is a patch anyways, so....meh)
    // if (strcmp(modeL, "A") == 0)
    // {
    //   opts->slot1_on = 1;
    //   opts->slot_preference = 0;
    // }
    // if (strcmp(modeR, "A") == 0)
    // {
    //   opts->slot2_on = 1;
    //   opts->slot_preference = 1;
    // }

    //check to see if we need to enable slot and toggle slot preference here
    //if both groups allowed, then give no preference to either one (not sure this is needed now)
    // if ( (strcmp(modeL, "A") == 0) && (strcmp(modeR, "A") == 0) )
    // {
    //   opts->slot1_on = 1;
    //   opts->slot2_on = 1;
    //   opts->slot_preference = 2;
    // }
    // else if (strcmp(modeL, "A") == 0)
    // {
    //   opts->slot1_on = 1;
    //   opts->slot_preference = 0;
    // }
    // else if (strcmp(modeR, "A") == 0)
    // {
    //   opts->slot2_on = 1;
    //   opts->slot_preference = 1;
    // }
    // else //if any other condition, then give no preference to either one
    // {
    //   opts->slot1_on = 1;
    //   opts->slot2_on = 1;
    //   opts->slot_preference = 2;
    // }

    // TG hold still drives slot preference hints for UI.
    if (state->tg_hold != 0 && state->tg_hold == TGL) {
        opts->slot1_on = 1;
        opts->slot_preference = 0;
    } else if (state->tg_hold != 0 && state->tg_hold == TGR) {
        opts->slot2_on = 1;
        opts->slot_preference = 1;
    } else {
        opts->slot_preference = 2;
    }

    //run hpf_d filter, if enabled
    if (opts->use_hpf_d == 1) {
        for (j = 0; j < 18; j++) {
            hpf_dL(state, state->s_l4[j], 160);
            hpf_dR(state, state->s_r4[j], 160);
        }
    }

//convert the left or right channel to both channels if single voice under certain conditions, if defined to do so
#define P2_STEREO_OUTPUT

#ifdef P2_STEREO_OUTPUT
    if (encL) {
        memset(state->s_l4, 0, sizeof(state->s_l4));
    }
    if (encR) {
        memset(state->s_r4, 0, sizeof(state->s_r4));
    }
    //this is for playing single voice over both channels, or when to keep them separated
    if ((opts->slot1_on == 0 && opts->slot2_on == 1 && encR == 0)                     //slot 1 off, slot 2 on
        || (opts->slot_preference == 1 && state->dmrburstR == 21 && encR == 0)        //prefer slot 2, voice right
        || (state->dmrburstR == 21 && state->dmrburstL != 21 && encR == 0)) {         //voice right only
        memcpy(state->s_l4, state->s_r4, sizeof(state->s_l4));                        //copy right to left
    } else if ((opts->slot1_on == 1 && opts->slot2_on == 0 && encL == 0)              //slot 2 off, slot 1 on
               || (opts->slot_preference == 0 && state->dmrburstL == 21 && encL == 0) //prefer slot 1, voice left
               || (state->dmrburstL == 21 && state->dmrburstR != 21 && encL == 0)) {  //voice left only
        memcpy(state->s_r4, state->s_l4, sizeof(state->s_r4));                        //copy left to right
    }
    //else if voice in both, and both slots on, and no preference on slot, then regular stereo interleave (left and right channels)

    //if both slots are the same now, then let's decimate the audio to keep the audio level consistent
    // if (memcmp (state->s_l4, state->s_r4, sizeof(state->s_l4)) == 0)
    // {
    //   for (j = 0; j < 18; j++)
    //   {
    //     for (i = 0; i < 160; i++)
    //     {
    //       state->s_l4[j][i] *= 0.85;
    //       state->s_r4[j][i] *= 0.85;
    //     }
    //   }
    // }

#endif

    //check this last
    if (opts->slot1_on == 0 && opts->slot2_on == 0) //both slots are hard off, disable playback
    {
        encL = 1;
        encR = 1;
    }

    //at this point, if both channels are still flagged as enc, then we can skip all playback/writing functions
    if (encL && encR) {
        goto SS18_END;
    }

    //interleave left and right channels from the short storage area
    for (j = 0; j < 18; j++) {
#ifdef P2_STEREO_OUTPUT
        // Under P2_STEREO_OUTPUT, the per-slot buffers are already zeroed for
        // muted slots, so always mix both channels.
        int mix_encL = 0;
        int mix_encR = 0;
#else
        int mix_encL = encL;
        int mix_encR = encR;
#endif
        audio_mix_interleave_stereo_s16(state->s_l4[j], state->s_r4[j], 160, mix_encL, mix_encR, stereo_sf[j]);
    }

    if (opts->audio_out == 1) {
        if (opts->audio_out_type == 0) //Pulse Audio
        {
            for (j = 0; j < 18; j++) {
                if (memcmp(empty, stereo_sf[j], sizeof(empty))
                    != 0) { //may not work as intended because its stereo and one will have something in it most likely
                    write_s16_audio(opts, stereo_sf[j], 160);
                }
            }
        }

        if (opts->audio_out_type == 8) //UDP Audio
        {
            for (j = 0; j < 18; j++) {
                if (memcmp(empty, stereo_sf[j], sizeof(empty))
                    != 0) { //may not work as intended because its stereo and one will have something in it most likely
                    dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(short), stereo_sf[j]);
                }
            }
        }

        if (opts->audio_out_type == 1) {
            for (j = 0; j < 18; j++) {
                if (memcmp(empty, stereo_sf[j], sizeof(empty))
                    != 0) { //may not work as intended because its stereo and one will have something in it most likely
                    write_audio_out(opts->audio_out_fd, stereo_sf[j], (size_t)320u * sizeof(short));
                }
            }
        }
    }

    if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
        for (j = 0; j < 18; j++) {
            if (memcmp(empty, stereo_sf[j], sizeof(empty)) != 0) {
                sf_write_short(opts->wav_out_f, stereo_sf[j], 320);
            }
        }
    }

SS18_END:

    //run cleanup since we pulled stuff from processAudio
    state->audio_out_idx = 0;
    state->audio_out_idxR = 0;

    //set float temp buffer to baseline
    memset(state->s_l4, 0, sizeof(state->s_l4));
    memset(state->s_r4, 0, sizeof(state->s_r4));

    if (state->audio_out_idx2 >= 800000) {
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
    }

    if (state->audio_out_idx2R >= 800000) {
        state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
        state->audio_out_buf_pR = state->audio_out_bufR + 100;
        memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
        memset(state->audio_out_bufR, 0, 100 * sizeof(short));
        state->audio_out_idx2R = 0;
    }
}

//largely borrowed from Boatbod OP25 (simplified single tone ID version)
void
soft_tonef(float samp[160], int n, int ID, int AD) {
    int i;
    float step1, step2, amplitude, freq1, freq2;

    float gain = 1.0f;

    //needs opts and state to work....don't feel like doing the changes
    // if (opts->audio_gain != 0 && slot == 0)
    //   gain = state->aout_gain / 25.0f;

    // Synthesize tones
    freq1 = 31.25 * ID;
    freq2 = freq1;
    step1 = 2 * M_PI * freq1 / 8000.0f;
    step2 = 2 * M_PI * freq2 / 8000.0f;
    amplitude = AD * 75.0f;

    for (i = 0; i < 160; i++) {
        samp[i] = (float)(amplitude * (sin((n)*step1) / 2 + sin((n)*step2) / 2));
        samp[i] /= 8000.0f;
        samp[i] *= gain;
        n++;
    }
}

void
beeper(dsd_opts* opts, dsd_state* state, int lr, int id, int ad, int len) {
    UNUSED(state);
    int i, j, n;
    //use lr as left or right channel designation in stereo config
    float samp_f[160];  //mono float sample
    float samp_fs[320]; //stereo float sample
    short samp_s[160];  //mono short sample
    short samp_ss[320]; //stereo short sample

    n = 0; //rolling sine wave 'degree'

    //double len if not using Pulse Audio,
    //anything over UDP may
    //not clear the buffer at the shorter len
    if (opts->audio_out_type != 0) {
        len *= 2;
    }

    //each j increment is 20 ms at 160 samples / 8 kHz
    for (j = 0; j < len; j++) {
        //'zero' out stereo mix samples
        memset(samp_fs, 0, sizeof(samp_fs));
        memset(samp_ss, 0, sizeof(samp_ss));

        //generate a tone with supplied tone ID and AD value
        soft_tonef(samp_f, n, id, ad);

        //convert float to short if required
        if (opts->floating_point == 0) {
            mbe_floattoshort(samp_f, samp_s);
            for (i = 0; i < 160; i++) {
                samp_s[i] *= 4000; //apply gain
                samp_ss[(i * 2) + lr] = samp_s[i];
            }
        }

        //load returned tone sample into appropriate channel -- left = +0; right = +1;
        for (i = 0; i < 160; i++) {
            samp_fs[(i * 2) + lr] = samp_f[i];
        }

        //play sample 3 times (20ms x 3 = 60ms)
        if (opts->audio_out == 1) {
            if (opts->audio_out_type == 0) //Pulse Audio
            {
                if (opts->pulse_digi_out_channels == 2 && opts->floating_point == 1) {
                    write_float_audio(opts, samp_fs, 160);
                }

                if (opts->pulse_digi_out_channels == 1 && opts->floating_point == 1) {
                    write_float_audio(opts, samp_f, 160);
                }

                if (opts->pulse_digi_out_channels == 2 && opts->floating_point == 0) {
                    write_s16_audio(opts, samp_ss, 160);
                }

                if (opts->pulse_digi_out_channels == 1 && opts->floating_point == 0) {
                    write_s16_audio(opts, samp_s, 160);
                }

            }

            else if (opts->audio_out_type == 8) //UDP Audio
            {
                if (opts->pulse_digi_out_channels == 2 && opts->floating_point == 1) {
                    dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(float), samp_fs);
                }

                if (opts->pulse_digi_out_channels == 1 && opts->floating_point == 1) {
                    dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(float), samp_f);
                }

                if (opts->pulse_digi_out_channels == 2 && opts->floating_point == 0) {
                    dsd_udp_audio_hook_blast(opts, state, (size_t)320u * sizeof(short), samp_ss);
                }

                if (opts->pulse_digi_out_channels == 1 && opts->floating_point == 0) {
                    dsd_udp_audio_hook_blast(opts, state, (size_t)160u * sizeof(short), samp_s);
                }

            }

            else if (opts->audio_out_type == 1) //STDOUT
            {
                if (opts->pulse_digi_out_channels == 2 && opts->floating_point == 1) {
                    write_audio_out(opts->audio_out_fd, samp_fs, (size_t)320u * sizeof(float));
                }

                if (opts->pulse_digi_out_channels == 1 && opts->floating_point == 1) {
                    write_audio_out(opts->audio_out_fd, samp_f, (size_t)160u * sizeof(float));
                }

                if (opts->pulse_digi_out_channels == 2 && opts->floating_point == 0) {
                    write_audio_out(opts->audio_out_fd, samp_ss, (size_t)320u * 2u);
                }

                if (opts->pulse_digi_out_channels == 1 && opts->floating_point == 0) {
                    write_audio_out(opts->audio_out_fd, samp_s, (size_t)160u * 2u);
                }
            }
        }
    }
}
