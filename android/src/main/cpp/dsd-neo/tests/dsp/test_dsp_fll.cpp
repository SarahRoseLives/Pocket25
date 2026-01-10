// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* Focused unit tests for FLL mix/update helpers with native float implementation. */

#include <dsd-neo/dsp/fll.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int
arrays_close(const float* a, const float* b, int n, float tol) {
    for (int i = 0; i < n; i++) {
        float d = a[i] - b[i];
        if (d < 0) {
            d = -d;
        }
        if (d > tol) {
            return 0;
        }
    }
    return 1;
}

int
main(void) {
    // Test 1: mix with freq=0 should be a no-op
    {
        fll_config_t cfg = {0};
        cfg.enabled = 1;

        fll_state_t st;
        fll_init_state(&st);
        st.freq = 0.0f; // no rotation (native float rad/sample)

        float x[20];
        for (int i = 0; i < 20; i++) {
            x[i] = (float)(i * 17 - 100);
        }
        float y[20];
        memcpy(y, x, sizeof(x));

        fll_mix_and_update(&cfg, &st, x, 20);
        if (!arrays_close(x, y, 20, 1)) {
            fprintf(stderr, "FLL mix (fast): freq=0 deviated >1 LSB\n");
            return 1;
        }

        /* Re-run once more to ensure determinism */
        memcpy(x, y, sizeof(x));
        fll_mix_and_update(&cfg, &st, x, 20);
        if (!arrays_close(x, y, 20, 1)) {
            fprintf(stderr, "FLL mix: freq=0 deviated >1 LSB\n");
            return 1;
        }
    }

    // Test 1b: phase accumulation wraps correctly on mix
    {
        fll_config_t cfg = {0};
        cfg.enabled = 1;

        fll_state_t st;
        fll_init_state(&st);

        // Choose a small positive freq, run enough complex samples to wrap
        st.freq = 0.05f;        // rad/sample increment per complex sample
        const int pairs = 1000; // complex samples
        float x[2 * pairs];
        memset(x, 0, sizeof(x)); // content doesn't matter for phase advance

        fll_mix_and_update(&cfg, &st, x, 2 * pairs);

        /* Phase should have accumulated pairs * freq, modulo 2π */
        float expected = fmodf(pairs * st.freq, (float)(2.0 * M_PI));
        /* Handle negative wrap */
        if (expected < 0.0f) {
            expected += (float)(2.0 * M_PI);
        }
        /* st.phase is native float rad, may have wrapped; compare modulo 2π */
        float got_phase = fmodf(st.phase, (float)(2.0 * M_PI));
        if (got_phase < 0.0f) {
            got_phase += (float)(2.0 * M_PI);
        }
        if (fabsf(got_phase - expected) > 0.01f) {
            fprintf(stderr, "FLL mix: phase wrap mismatch, got %f expected %f\n", got_phase, expected);
            return 1;
        }
    }

    // Test 1c: disabled config leaves buffers/state unchanged
    {
        fll_config_t cfg = {0};
        cfg.enabled = 0;

        fll_state_t st;
        fll_init_state(&st);
        st.freq = 0.015f; // native float rad/sample
        st.phase = 0.01f; // native float rad

        float x[8] = {10, 20, 30, 40, 50, 60, 70, 80};
        float y[8];
        memcpy(y, x, sizeof(x));

        fll_mix_and_update(&cfg, &st, x, 8);
        if (!arrays_close(x, y, 8, 0) || fabsf(st.phase - 0.01f) > 1e-6f) {
            fprintf(stderr, "FLL mix: disabled mode altered output/state\n");
            return 1;
        }

        // update_error should also not change freq/integrator when disabled
        st.integrator = 0.004f;
        fll_update_error(&cfg, &st, x, 8);
        if (!(fabsf(st.freq - 0.015f) < 1e-6f && fabsf(st.integrator - 0.004f) < 1e-6f)) {
            fprintf(stderr, "FLL update: disabled mode altered control state\n");
            return 1;
        }
    }

    // Test 2: update_error should move freq in sign of observed CFO
    {
        const int N = 100; // even
        float iq[2 * N];
        double r = 12000.0;                   // arbitrary radius within float range
        double dtheta = (2.0 * M_PI) / 200.0; // small positive rotation per complex sample
        for (int k = 0; k < N; k++) {
            double th = k * dtheta;
            float i = (float)(r * cos(th));
            float q = (float)(r * sin(th));
            iq[2 * k + 0] = i;
            iq[2 * k + 1] = q;
        }

        fll_config_t cfg = {0};
        cfg.enabled = 1;
        cfg.alpha = 0.04f;   // native float proportional gain
        cfg.beta = 0.025f;   // native float integral gain
        cfg.deadband = 0.0f; // respond to small errors
        cfg.slew_max = 1.0f; // effectively unlimited for test

        fll_state_t st;
        fll_init_state(&st);
        fll_update_error(&cfg, &st, iq, 2 * N);
        if (st.freq <= 0.0f) {
            fprintf(stderr, "FLL update: expected positive freq correction, got %f\n", st.freq);
            return 1;
        }

        // Negative rotation
        for (int k = 0; k < N; k++) {
            double th = -k * dtheta;
            float i = (float)(r * cos(th));
            float q = (float)(r * sin(th));
            iq[2 * k + 0] = i;
            iq[2 * k + 1] = q;
        }
        fll_init_state(&st);
        fll_update_error(&cfg, &st, iq, 2 * N);
        if (st.freq >= 0.0f) {
            fprintf(stderr, "FLL update: expected negative freq correction, got %f\n", st.freq);
            return 1;
        }
    }

    // Test 2b: update_error early return when N=2 and prev=0, integrator unchanged
    {
        fll_config_t cfg = {0};
        cfg.enabled = 1;
        cfg.alpha = 0.15f;
        cfg.beta = 0.15f;
        cfg.deadband = 0.0f;
        cfg.slew_max = 0.003f;

        fll_state_t st;
        fll_init_state(&st);
        st.integrator = 0.024f;

        float one[2] = {1234.0f, -5678.0f};
        fll_update_error(&cfg, &st, one, 2);
        if (!(fabsf(st.freq) < 1e-6f && fabsf(st.integrator - 0.024f) < 1e-6f)) {
            fprintf(stderr, "FLL small-N adj: unexpected change on first call\n");
            return 1;
        }
        if (!(fabsf(st.prev_r - 1234.0f) < 1e-3f && fabsf(st.prev_j + 5678.0f) < 1e-3f)) {
            fprintf(stderr, "FLL small-N adj: prev sample not latched\n");
            return 1;
        }
    }

    // Test 3: deadband holds control, integrator not advanced (leak only)
    {
        // Build constant sample stream -> zero phase difference (err=0)
        float iq[16];
        for (int i = 0; i < 16; i += 2) {
            iq[i + 0] = 10000.0f;
            iq[i + 1] = 0.0f;
        }

        fll_config_t cfg = {0};
        cfg.enabled = 1;
        cfg.alpha = 0.3f;
        cfg.beta = 0.3f;
        cfg.deadband = 0.001f; // any small nonzero value
        cfg.slew_max = 1.0f;

        fll_state_t st;
        fll_init_state(&st);
        st.freq = 0.024f;      // native float rad/sample
        st.integrator = 0.03f; // native float integrator

        fll_update_error(&cfg, &st, iq, 16);
        if (fabsf(st.freq - 0.024f) > 1e-6f) {
            fprintf(stderr, "FLL deadband: freq changed unexpectedly\n");
            return 1;
        }
        /* Integrator has very small leakage (~1-1/4096 per update), so allow
         * a tiny drift. For one call, drift is about 0.03 * (1/4096) ≈ 7e-6. */
        if (fabsf(st.integrator - 0.03f) > 1e-4f) {
            fprintf(stderr, "FLL deadband: integrator changed unexpectedly (%f)\n", st.integrator);
            return 1;
        }
    }

    // Test 4: slew limiting constrains per-update delta frequency
    {
        const int N = 64;
        float iq[2 * N];
        double r = 15000.0;
        double dtheta = (2.0 * M_PI) / 20.0; // large rotation to drive controller
        for (int k = 0; k < N; k++) {
            double th = k * dtheta;
            iq[2 * k + 0] = (float)(r * cos(th));
            iq[2 * k + 1] = (float)(r * sin(th));
        }

        fll_config_t cfg = {0};
        cfg.enabled = 1;
        cfg.alpha = 0.6f;
        cfg.beta = 0.6f;
        cfg.deadband = 0.0f;
        cfg.slew_max = 0.001f; // tight slew per update (native float rad/sample)

        fll_state_t st;
        fll_init_state(&st);

        fll_update_error(&cfg, &st, iq, 2 * N);
        if (fabsf(st.freq - 0.001f) > 1e-5f) {
            fprintf(stderr, "FLL slew: first step %f, want ~0.001\n", st.freq);
            return 1;
        }
        fll_update_error(&cfg, &st, iq, 2 * N);
        if (fabsf(st.freq - 0.002f) > 1e-5f) {
            fprintf(stderr, "FLL slew: second step %f, want ~0.002\n", st.freq);
            return 1;
        }
    }

    // Test 5: clamp bounds integrator and absolute frequency
    {
        const int N = 64;
        float iq[2 * N];
        double r = 16000.0;
        double dtheta = (2.0 * M_PI) / 8.0; // very large
        for (int k = 0; k < N; k++) {
            double th = k * dtheta;
            iq[2 * k + 0] = (float)(r * cos(th));
            iq[2 * k + 1] = (float)(r * sin(th));
        }

        fll_config_t cfg = {0};
        cfg.enabled = 1;
        cfg.alpha = 0.9f;
        cfg.beta = 0.9f;
        cfg.deadband = 0.0f;
        cfg.slew_max = 1.0f; // no slew limit, rely on absolute clamp

        fll_state_t st;
        fll_init_state(&st);
        fll_update_error(&cfg, &st, iq, 2 * N);
        /* Clamp at ~0.8 rad/sample (kFreqClamp in fll.cpp) */
        const float F_CLAMP = 0.8f;
        if (st.freq > F_CLAMP || st.freq < -F_CLAMP) {
            fprintf(stderr, "FLL clamp: freq exceeded clamp (%f)\n", st.freq);
            return 1;
        }
        if (st.integrator > F_CLAMP || st.integrator < -F_CLAMP) {
            fprintf(stderr, "FLL clamp: integrator exceeded clamp (%f)\n", st.integrator);
            return 1;
        }
    }

    // Test 6: small-N behavior uses prev sample across calls
    {
        // First call: only one complex sample -> no update
        fll_config_t cfg = {0};
        cfg.enabled = 1;
        cfg.alpha = 0.4f;
        cfg.beta = 0.25f;
        cfg.deadband = 0.0f;
        cfg.slew_max = 1.0f;

        fll_state_t st;
        fll_init_state(&st);

        float b1[2] = {16000.0f, 0.0f};
        fll_update_error(&cfg, &st, b1, 2);
        if (!(fabsf(st.freq) < 1e-6f && fabsf(st.prev_r - 16000.0f) < 1e-3f && fabsf(st.prev_j) < 1e-3f)) {
            fprintf(stderr, "FLL small-N: first call state wrong\n");
            return 1;
        }

        // Second call: one more sample with positive phase -> expect freq > 0
        float b2[2] = {0.0f, 16000.0f}; // +90 deg relative to previous
        fll_update_error(&cfg, &st, b2, 2);
        if (st.freq <= 0.0f) {
            fprintf(stderr, "FLL small-N: expected positive update after carry-over\n");
            return 1;
        }
    }

    // Test 7: magnitude preserved by pure rotation (energy invariant)
    {
        const int N = 64;
        float iq[2 * N];
        double r = 17000.0;
        double dtheta = (2.0 * M_PI) / 64.0;
        for (int k = 0; k < N; k++) {
            double th = k * dtheta;
            iq[2 * k + 0] = (float)(r * cos(th));
            iq[2 * k + 1] = (float)(r * sin(th));
        }

        fll_config_t cfg = {0};
        cfg.enabled = 1;
        fll_state_t st;
        fll_init_state(&st);
        st.freq = 0.06f; // native float rad/sample rotation

        // Compute energy before
        double e0 = 0.0, e1 = 0.0;
        for (int i = 0; i < 2 * N; i++) {
            e0 += (double)iq[i] * (double)iq[i];
        }
        fll_mix_and_update(&cfg, &st, iq, 2 * N);
        for (int i = 0; i < 2 * N; i++) {
            e1 += (double)iq[i] * (double)iq[i];
        }
        // Allow small numeric drift due to rounding (<0.2%)
        double diff = fabs(e0 - e1);
        if (diff > (e0 / 500.0)) {
            fprintf(stderr, "FLL mix: energy changed too much (|d|=%f)\n", diff);
            return 1;
        }
    }

    // Test 8: phase accumulation wraps with negative frequency
    {
        fll_config_t cfg = {0};
        cfg.enabled = 1;

        fll_state_t st;
        fll_init_state(&st);
        st.freq = -0.05f; // negative rad/sample
        const int pairs = 1000;
        float x[2 * pairs];
        memset(x, 0, sizeof(x));
        fll_mix_and_update(&cfg, &st, x, 2 * pairs);
        /* Phase should have accumulated pairs * freq, modulo 2π, negative wrap handled */
        float raw_expected = (float)(pairs * (-0.05));
        float expected = fmodf(raw_expected, (float)(2.0 * M_PI));
        if (expected < 0.0f) {
            expected += (float)(2.0 * M_PI);
        }
        float got_phase = fmodf(st.phase, (float)(2.0 * M_PI));
        if (got_phase < 0.0f) {
            got_phase += (float)(2.0 * M_PI);
        }
        if (fabsf(got_phase - expected) > 0.01f) {
            fprintf(stderr, "FLL mix neg: phase wrap mismatch, got %f expected %f\n", got_phase, expected);
            return 1;
        }
    }

    return 0;
}
