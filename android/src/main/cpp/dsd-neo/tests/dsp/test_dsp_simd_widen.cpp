// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Focused unit test for SIMD u8->float widening and 90° rotate+widen. */

#include <dsd-neo/dsp/simd_widen.h>
#include <math.h>
#include <stdio.h>

static int
arrays_close(const float* a, const float* b, int n, float tol) {
    for (int i = 0; i < n; i++) {
        if (fabsf(a[i] - b[i]) > tol) {
            return 0;
        }
    }
    return 1;
}

int
main(void) {
    // 4 complex samples (8 bytes)
    const unsigned char src[8] = {127, 127, 130, 130, 255, 0, 0, 255};
    float dst[8] = {0};
    float ref[8] = {0};

    // widen center 127 → normalized float around zero (center at 127.5)
    widen_u8_to_f32_bias127(src, dst, 8);
    const float inv = 1.0f / 127.5f;
    for (int i = 0; i < 8; i++) {
        ref[i] = ((float)src[i] - 127.5f) * inv;
    }
    if (!arrays_close(dst, ref, 8, 1e-6f)) {
        fprintf(stderr, "SIMD widen: mismatch\n");
        return 1;
    }

    // rotate 90° with pattern per implementation: (I0,Q0),(I1,Q1)->(-Q1, I1),(I2,Q2)->(-I2,-Q2),(I3,Q3)->(Q3,-I3)
    for (int i = 0; i < 8; i++) {
        dst[i] = 0;
    }
    widen_rotate90_u8_to_f32_bias127(src, dst, 8);
    float i0 = ((float)src[0] - 127.5f) * inv, q0 = ((float)src[1] - 127.5f) * inv;
    float i1 = ((float)src[2] - 127.5f) * inv, q1 = ((float)src[3] - 127.5f) * inv;
    float i2 = ((float)src[4] - 127.5f) * inv, q2 = ((float)src[5] - 127.5f) * inv;
    float i3 = ((float)src[6] - 127.5f) * inv, q3 = ((float)src[7] - 127.5f) * inv;
    float ref_rot[8] = {i0, q0, -q1, i1, -i2, -q2, q3, -i3};
    if (!arrays_close(dst, ref_rot, 8, 1e-6f)) {
        fprintf(stderr, "SIMD rotate+widen: mismatch\n");
        return 1;
    }

    return 0;
}
