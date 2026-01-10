// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Small mixing primitives for float and short audio paths.
 *
 * These helpers encapsulate common slot→stereo/mono mixing patterns so
 * that the higher-level mixers in dsd_audio2.c can delegate the inner
 * loops here.
 */

#include <dsd-neo/core/audio.h>

#include <stddef.h>

void
audio_mix_interleave_stereo_f32(const float* left, const float* right, size_t n, int encL, int encR,
                                float* stereo_out) {
    if (!stereo_out || !left || !right) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        stereo_out[i * 2 + 0] = encL ? 0.0f : left[i];
        stereo_out[i * 2 + 1] = encR ? 0.0f : right[i];
    }
}

void
audio_mix_interleave_stereo_s16(const short* left, const short* right, size_t n, int encL, int encR,
                                short* stereo_out) {
    if (!stereo_out || !left || !right) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        stereo_out[i * 2 + 0] = encL ? 0 : left[i];
        stereo_out[i * 2 + 1] = encR ? 0 : right[i];
    }
}

void
audio_mix_mono_from_slots_f32(const float* left, const float* right, size_t n, int l_on, int r_on, float* mono_out) {
    if (!mono_out || !left || !right) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        if (l_on && !r_on) {
            mono_out[i] = left[i];
        } else if (!l_on && r_on) {
            mono_out[i] = right[i];
        } else if (l_on && r_on) {
            mono_out[i] = 0.5f * (left[i] + right[i]);
        } else {
            mono_out[i] = 0.0f;
        }
    }
}
