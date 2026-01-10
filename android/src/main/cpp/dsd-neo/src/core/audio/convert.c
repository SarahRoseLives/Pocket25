// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Lightweight audio conversion helpers (float⇄short and mono→stereo).
 */

#include <dsd-neo/core/audio.h>

#include <stddef.h>

void
audio_float_to_s16(const float* in, short* out, size_t n, float scale) {
    if (!in || !out) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        float v = in[i] * scale;
        if (v > 32767.0f) {
            v = 32767.0f;
        } else if (v < -32768.0f) {
            v = -32768.0f;
        }
        out[i] = (short)v;
    }
}

void
audio_s16_to_float(const short* in, float* out, size_t n, float scale) {
    if (!in || !out) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        out[i] = (float)in[i] * scale;
    }
}

void
audio_mono_to_stereo_f32(const float* in, float* out, size_t n) {
    if (!in || !out) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        float v = in[i];
        out[i * 2 + 0] = v;
        out[i * 2 + 1] = v;
    }
}

void
audio_mono_to_stereo_s16(const short* in, short* out, size_t n) {
    if (!in || !out) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        short v = in[i];
        out[i * 2 + 0] = v;
        out[i * 2 + 1] = v;
    }
}
