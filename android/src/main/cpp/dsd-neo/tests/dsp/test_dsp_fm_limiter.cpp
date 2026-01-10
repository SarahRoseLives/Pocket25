// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Unit test: fm_constant_envelope_limiter scales samples outside [0.5, 2.0] of target
   magnitude to near the target magnitude, and leaves in-range samples unchanged. */

#include <cmath>
#include <cstdlib>
#include <dsd-neo/dsp/demod_pipeline.h>
#include <dsd-neo/dsp/demod_state.h>
#include <stdio.h>
#include <string.h>

static double
rms_mag_iq(const float* iq, int pairs) {
    double acc = 0.0;
    for (int n = 0; n < pairs; n++) {
        double I = iq[(size_t)(2 * n) + 0];
        double Q = iq[(size_t)(2 * n) + 1];
        acc += I * I + Q * Q;
    }
    return (pairs > 0) ? std::sqrt(acc / pairs) : 0.0;
}

static int
approx_eq(float a, float b, float tol) {
    float d = a - b;
    if (d < 0) {
        d = -d;
    }
    return d <= tol;
}

int
main(void) {
    demod_state* s = (demod_state*)malloc(sizeof(demod_state));
    if (!s) {
        return 1;
    }
    memset(s, 0, sizeof(*s));

    const int pairs = 256;
    static float buf[(size_t)pairs * 2];
    const float target = 0.30f;

    // Build two-level magnitude block on I axis (Q=0): low=0.2x target, high=2.2x target to trigger clamp
    for (int n = 0; n < pairs; n++) {
        float amp = (n < pairs / 2) ? (target * 0.2f) : (target * 2.2f);
        buf[(size_t)(2 * n) + 0] = amp;
        buf[(size_t)(2 * n) + 1] = 0;
    }

    s->lowpassed = buf;
    s->lp_len = pairs * 2;
    s->mode_demod = &raw_demod; // copy lowpassed -> result, then return
    s->fm_agc_enable = 0;       // isolate limiter behavior
    s->fm_limiter_enable = 1;
    s->cqpsk_enable = 0;
    s->iqbal_enable = 0;
    s->squelch_level = 0.0f;
    s->fll_enabled = 0;
    s->ted_enabled = 0;
    s->fm_agc_target_rms = target; // default target
    s->squelch_gate_open = 1;
    s->squelch_env = 1.0f;
    s->squelch_env_attack = 0.125f;
    s->squelch_env_release = 0.03125f;

    double pre_rms = rms_mag_iq(buf, pairs);
    (void)pre_rms; // informational

    full_demod(s);

    // Verify both halves near the target magnitude on I, Q remains ~0
    for (int n = 0; n < pairs; n++) {
        float I = s->result[(size_t)(2 * n) + 0];
        float Q = s->result[(size_t)(2 * n) + 1];
        if (!approx_eq(I, target, 0.02f)) {
            fprintf(stderr, "Limiter: sample %d I=%f not near target %.2f\n", n, I, target);
            free(s);
            return 1;
        }
        if (!approx_eq(Q, 0.0f, 0.01f)) {
            fprintf(stderr, "Limiter: sample %d Q=%f deviates from 0\n", n, Q);
            free(s);
            return 1;
        }
    }

    // Second scenario: in-range magnitude should remain unchanged (~7000)
    for (int n = 0; n < pairs; n++) {
        buf[(size_t)(2 * n) + 0] = target * 0.7f;
        buf[(size_t)(2 * n) + 1] = 0.0f;
    }
    s->lowpassed = buf;
    s->lp_len = pairs * 2;
    full_demod(s);
    for (int n = 0; n < pairs; n++) {
        float I = s->result[(size_t)(2 * n) + 0];
        float Q = s->result[(size_t)(2 * n) + 1];
        if (!approx_eq(I, target * 0.7f, 0.02f)) {
            fprintf(stderr, "Limiter in-range: sample %d I=%f changed too much\n", n, I);
            free(s);
            return 1;
        }
        if (!approx_eq(Q, 0.0f, 0.01f)) {
            fprintf(stderr, "Limiter in-range: sample %d Q=%f deviates\n", n, Q);
            free(s);
            return 1;
        }
    }

    // Third scenario: mixed-phase tones with magnitudes outside the band should be normalized to target
    for (int n = 0; n < pairs; n++) {
        double th = (2.0 * M_PI * n) / pairs;
        double A = (n < pairs / 2) ? (0.3 * target) : (2.5 * target); // below 0.5x and above 2x target
        buf[(size_t)(2 * n) + 0] = (float)(A * cos(th));
        buf[(size_t)(2 * n) + 1] = (float)(A * sin(th));
    }
    s->lowpassed = buf;
    s->lp_len = pairs * 2;
    full_demod(s);
    for (int n = 0; n < pairs; n++) {
        float I = s->result[(size_t)(2 * n) + 0];
        float Q = s->result[(size_t)(2 * n) + 1];
        double mag = std::sqrt((double)I * I + (double)Q * Q);
        if (!(mag > 0.28 && mag < 0.32)) {
            fprintf(stderr, "Limiter mixed-phase: sample %d |z|=%.4f not near target %.2f\n", n, mag, target);
            free(s);
            return 1;
        }
    }

    // Fourth scenario: near-boundary behavior — just inside band unchanged; just outside clamps to target
    auto gen_rot = [&](double A) {
        for (int n = 0; n < pairs; n++) {
            double th = (2.0 * M_PI * n) / (pairs - 1);
            buf[(size_t)(2 * n) + 0] = (float)(A * cos(th));
            buf[(size_t)(2 * n) + 1] = (float)(A * sin(th));
        }
    };

    // Inside low boundary: 0.51x target
    gen_rot(0.51 * target);
    s->lowpassed = buf;
    s->lp_len = pairs * 2;
    full_demod(s);
    for (int n = 0; n < pairs; n++) {
        double preI = 0.51 * target * cos((2.0 * M_PI * n) / (pairs - 1));
        double preQ = 0.51 * target * sin((2.0 * M_PI * n) / (pairs - 1));
        float I = s->result[(size_t)(2 * n) + 0];
        float Q = s->result[(size_t)(2 * n) + 1];
        if (!approx_eq(I, (float)preI, 0.01f) || !approx_eq(Q, (float)preQ, 0.01f)) {
            fprintf(stderr, "Limiter boundary in-low: sample %d changed too much\n", n);
            free(s);
            return 1;
        }
    }

    // Outside low boundary: 0.49x target → clamp
    gen_rot(0.49 * target);
    s->lowpassed = buf;
    s->lp_len = pairs * 2;
    full_demod(s);
    for (int n = 0; n < pairs; n++) {
        float I = s->result[(size_t)(2 * n) + 0];
        float Q = s->result[(size_t)(2 * n) + 1];
        double mag = std::sqrt((double)I * I + (double)Q * Q);
        if (!(mag > 0.28 && mag < 0.32)) {
            fprintf(stderr, "Limiter boundary out-low: sample %d |z|=%.4f not clamped\n", n, mag);
            free(s);
            return 1;
        }
    }

    // Inside high boundary: 1.99x target
    gen_rot(1.99 * target);
    s->lowpassed = buf;
    s->lp_len = pairs * 2;
    full_demod(s);
    for (int n = 0; n < pairs; n++) {
        double preI = 1.99 * target * cos((2.0 * M_PI * n) / (pairs - 1));
        double preQ = 1.99 * target * sin((2.0 * M_PI * n) / (pairs - 1));
        float I = s->result[(size_t)(2 * n) + 0];
        float Q = s->result[(size_t)(2 * n) + 1];
        if (!approx_eq(I, (float)preI, 0.02f) || !approx_eq(Q, (float)preQ, 0.02f)) {
            fprintf(stderr, "Limiter boundary in-high: sample %d changed too much\n", n);
            free(s);
            return 1;
        }
    }

    // Outside high boundary: 2.01x target → clamp
    gen_rot(2.01 * target);
    s->lowpassed = buf;
    s->lp_len = pairs * 2;
    full_demod(s);
    for (int n = 0; n < pairs; n++) {
        float I = s->result[(size_t)(2 * n) + 0];
        float Q = s->result[(size_t)(2 * n) + 1];
        double mag = std::sqrt((double)I * I + (double)Q * Q);
        if (!(mag > 0.28 && mag < 0.32)) {
            fprintf(stderr, "Limiter boundary out-high: sample %d |z|=%.4f not clamped\n", n, mag);
            free(s);
            return 1;
        }
    }

    free(s);
    return 0;
}
