// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Core audio API surface for DSD-neo.
 *
 * Exposes device open/close helpers, drain/flush routines, and playback
 * helpers shared across audio backends. Kept separate so
 * modules that only need audio APIs can avoid pulling in the full core header.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>
#include <dsd-neo/platform/audio.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Process one block of dPMR voice through the decoder/synth path. */
void processdPMRvoice(dsd_opts* opts, dsd_state* state);
/** @brief Core audio processing entry point (slot 1). */
void processAudio(dsd_opts* opts, dsd_state* state);
/** @brief Core audio processing entry point (slot 2 / right). */
void processAudioR(dsd_opts* opts, dsd_state* state);

/** @brief Open audio input stream based on opts. Returns 0 on success. */
int openAudioInput(dsd_opts* opts);
/** @brief Open audio output stream based on opts. Returns 0 on success. */
int openAudioOutput(dsd_opts* opts);
/** @brief Close audio input stream if open. */
void closeAudioInput(dsd_opts* opts);
/** @brief Close audio output stream if open. */
void closeAudioOutput(dsd_opts* opts);

/** @brief Best-effort drain of audio output buffers. Safe no-op when disabled. */
void dsd_drain_audio_output(dsd_opts* opts);

/** @brief Write synthesized mono voice samples for slot 1. */
void writeSynthesizedVoice(dsd_opts* opts, dsd_state* state);
/** @brief Write synthesized mono voice samples for slot 2. */
void writeSynthesizedVoiceR(dsd_opts* opts, dsd_state* state);
/** @brief Write synthesized mono mixed voice samples. */
void writeSynthesizedVoiceMS(dsd_opts* opts, dsd_state* state);

/** @brief Play synthesized voice (float stereo mix) for slot 1. */
void playSynthesizedVoiceFS(dsd_opts* opts, dsd_state* state); // float stereo mix
/** @brief Play synthesized voice (float stereo mix 3v2 DMR). */
void playSynthesizedVoiceFS3(dsd_opts* opts, dsd_state* state); // float stereo mix 3v2 DMR
/** @brief Play synthesized voice (float stereo mix 4v2 P25p2). */
void playSynthesizedVoiceFS4(dsd_opts* opts, dsd_state* state); // float stereo mix 4v2 P25p2
/** @brief Play synthesized voice (float mono). */
void playSynthesizedVoiceFM(dsd_opts* opts, dsd_state* state); // float mono

/** @brief Play synthesized voice (short mono output slot 1). */
void playSynthesizedVoice(dsd_opts* opts, dsd_state* state); // short mono output slot 1
/** @brief Play synthesized voice (short mono output slot 2). */
void playSynthesizedVoiceR(dsd_opts* opts, dsd_state* state); // short mono output slot 2
/** @brief Play synthesized voice (short mono mix). */
void playSynthesizedVoiceMS(dsd_opts* opts, dsd_state* state); // short mono mix
/** @brief Play synthesized voice (short stereo mix). */
void playSynthesizedVoiceSS(dsd_opts* opts, dsd_state* state); // short stereo mix
/** @brief Play synthesized voice (short stereo mix 3v2 DMR). */
void playSynthesizedVoiceSS3(dsd_opts* opts, dsd_state* state); // short stereo mix 3v2 DMR
/** @brief Play synthesized voice (short stereo mix 4v2 P25p2). */
void playSynthesizedVoiceSS4(dsd_opts* opts, dsd_state* state); // short stereo mix 4v2 P25p2
/** @brief Play synthesized voice (short stereo mix 18V superframe). */
void playSynthesizedVoiceSS18(dsd_opts* opts, dsd_state* state); // short stereo mix 18V Superframe

/** @brief Apply float-domain gain to 160-sample block for given slot. */
void agf(dsd_opts* opts, dsd_state* state, float samp[160], int slot); // float gain control
/** @brief Apply short-domain gain to buffer of given length. */
void agsm(dsd_opts* opts, dsd_state* state, short* input, int len); // short gain control
/** @brief Apply float-domain auto gain control for analog monitor path. */
void agsm_f(dsd_opts* opts, dsd_state* state, float* input, int len); // float gain control for analog
/** @brief Apply manual analog gain to short buffer. */
void analog_gain(dsd_opts* opts, dsd_state* state, short* input, int len); // manual gain for analog paths
/** @brief Apply manual analog gain to float buffer. */
void analog_gain_f(dsd_opts* opts, dsd_state* state, float* input, int len); // float manual gain for analog

/** @brief Multiply float buffer by gain factor in-place. */
void audio_apply_gain_f32(float* buf, size_t n, float gain);
/** @brief Multiply int16 buffer by gain factor in-place. */
void audio_apply_gain_s16(short* buf, size_t n, float gain);

/** @brief Simple legacy 8k→48k upsampler (6:1) with linear interpolation. */
void upsampleS(short invalue, short prev, short outbuf[6]);
/** @brief Float 8k→48k upsampler (6:1) with linear interpolation. */
void upsampleF(float invalue, float prev, float outbuf[6]);
/** @brief Legacy analog monitor 6x upsampler (sample repetition). */
void upsample(dsd_state* state, float invalue);

/** @brief Convert float samples to int16 with scaling. */
void audio_float_to_s16(const float* in, short* out, size_t n, float scale);
/** @brief Convert int16 samples to float with scaling. */
void audio_s16_to_float(const short* in, float* out, size_t n, float scale);
/** @brief Duplicate mono float samples into interleaved stereo buffer. */
void audio_mono_to_stereo_f32(const float* in, float* out, size_t n);
/** @brief Duplicate mono int16 samples into interleaved stereo buffer. */
void audio_mono_to_stereo_s16(const short* in, short* out, size_t n);

/** @brief Mix two float channels with per-channel mute flags into interleaved stereo. */
void audio_mix_interleave_stereo_f32(const float* left, const float* right, size_t n, int encL, int encR,
                                     float* stereo_out);
/** @brief Mix two int16 channels with per-channel mute flags into interleaved stereo. */
void audio_mix_interleave_stereo_s16(const short* left, const short* right, size_t n, int encL, int encR,
                                     short* stereo_out);
/** @brief Mix two float channels with mute flags into mono output. */
void audio_mix_mono_from_slots_f32(const float* left, const float* right, size_t n, int l_on, int r_on,
                                   float* mono_out);

/** @brief Simple P25 P2 per-slot mixer gate used by tests (maps p25_p2_audio_allowed -> enc flags). */
int dsd_p25p2_mixer_gate(const dsd_state* state, int* encL, int* encR);

/** @brief Flush partially buffered P25p2 SS18 audio on call end/release. */
void dsd_p25p2_flush_partial_audio(dsd_opts* opts, dsd_state* state);

/** @brief Talkgroup/whitelist/TG-hold gating for mono mix (enc flags 0=unmuted,1=muted). */
int dsd_audio_group_gate_mono(const dsd_opts* opts, const dsd_state* state, unsigned long tg, int enc_in, int* enc_out);
/** @brief Talkgroup/whitelist/TG-hold gating for dual/slot mix (enc flags 0=unmuted,1=muted). */
int dsd_audio_group_gate_dual(const dsd_opts* opts, const dsd_state* state, unsigned long tgL, unsigned long tgR,
                              int encL_in, int encR_in, int* encL_out, int* encR_out);

/** @brief Legacy UI beeper helper (used by ncurses call-alert and events). */
void beeper(dsd_opts* opts, dsd_state* state, int lr, int id, int ad, int len);

/** @brief Open output audio device at requested speed. */
void openAudioOutDevice(dsd_opts* opts, int speed);
/** @brief Open input audio device based on opts. Returns 0 on success. */
int openAudioInDevice(dsd_opts* opts);

/** @brief Parse audio input device string and update opts. */
void parse_audio_input_string(dsd_opts* opts, char* input);
/** @brief Parse audio output device string and update opts. */
void parse_audio_output_string(dsd_opts* opts, char* input);

/** @brief Print available audio devices to stdout. */
int audio_list_devices(void);

#ifdef __cplusplus
}
#endif
