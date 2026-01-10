// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Scalar helpers to widen RTL u8 IQ into normalized float samples.
 *
 * Converts unsigned 8-bit I/Q into centered float in [-1.0, 1.0] with an
 * unbiased midpoint at 127.5. No clamping is applied so headroom is retained
 * for downstream float processing.
 */

#include <dsd-neo/dsp/simd_widen.h>

#include <stdint.h>

void
widen_u8_to_f32_bias127(const unsigned char* src, float* dst, uint32_t len) {
    if (!src || !dst || len == 0) {
        return;
    }
    const float inv = 1.0f / 127.5f;
    for (uint32_t i = 0; i < len; i++) {
        float v = ((float)src[i] - 127.5f) * inv;
        dst[i] = v;
    }
}

void
widen_u8_to_f32_bias128_scalar(const unsigned char* src, float* dst, uint32_t len) {
    if (!src || !dst || len == 0) {
        return;
    }
    const float inv = 1.0f / 127.5f;
    for (uint32_t i = 0; i < len; i++) {
        float v = ((float)src[i] - 128.0f) * inv;
        dst[i] = v;
    }
}

void
widen_rotate90_u8_to_f32_bias127(const unsigned char* src, float* dst, uint32_t len) {
    if (!src || !dst || len < 2) {
        return;
    }
    const float inv = 1.0f / 127.5f;
    uint32_t pairs = len >> 1;
    for (uint32_t n = 0; n < pairs; n++) {
        uint32_t idx = n << 1;
        float i_raw = ((float)src[idx + 0] - 127.5f) * inv;
        float q_raw = ((float)src[idx + 1] - 127.5f) * inv;
        float ri = i_raw;
        float rq = q_raw;
        switch (n & 3U) {
            case 1:
                ri = -q_raw;
                rq = i_raw;
                break; /* +90° */
            case 2:
                ri = -i_raw;
                rq = -q_raw;
                break; /* 180° */
            case 3:
                ri = q_raw;
                rq = -i_raw;
                break;      /* -90° */
            default: break; /* 0° */
        }
        dst[idx + 0] = ri;
        dst[idx + 1] = rq;
    }
}
