// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Shared gain and autogain helpers for float and short audio paths.
 *
 * This module centralizes gain logic so that the core mixers in
 * dsd_audio2.c can act as thin orchestrators that delegate to these
 * helpers instead of inlining gain math.
 */

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

#include <math.h>
#include <stddef.h>
#include <string.h>

// Return 1 if all elements are effectively zero (|x| < 1e-12f)
static inline int
audio_is_all_zero_f(const float* buf, size_t n) {
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

void
audio_apply_gain_f32(float* buf, size_t n, float gain) {
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] *= gain;
    }
}

void
audio_apply_gain_s16(short* buf, size_t n, float gain) {
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] = (short)(buf[i] * gain);
    }
}

// Older float-path autogain used by DMR/P25 mixers. Kept byte-for-byte
// identical to the original implementation in dsd_audio2.c.
void
agf(dsd_opts* opts, dsd_state* state, float samp[160], int slot) {
    int i, j, run;
    run = 1;
    float empty[160];
    memset(empty, 0.0f, sizeof(empty));

    float mmax = 0.90f;
    float mmin = -0.90f;
    float aavg = 0.0f; //average of the absolute value
    float df;          //decimation value
    df = 3277.0f;      //test value

    //trying things
    float gain = 1.0f;

    //test increasing gain on DMR EP samples with degraded AMBE samples
    if (state->payload_algid == 0x21 || state->payload_algidR == 0x21) {
        gain = 1.75f;
    }

    if (opts->audio_gain != 0) {
        gain = opts->audio_gain / 25.0f;
    }

    // Determine whether or not to run gain on 'empty' floating samples
    if (audio_is_all_zero_f(samp, 160)) {
        run = 0;
    }
    if (run == 0) {
        goto AGF_END;
    }

    for (j = 0; j < 8; j++) {

        if (slot == 0) {
            df = 384.0f * (50.0f - state->aout_gain);
        }
        if (slot == 1) {
            df = 384.0f * (50.0f - state->aout_gainR);
        }

        for (i = 0; i < 20; i++) {

            samp[(j * 20) + i] = samp[(j * 20) + i] / df;

            //simple clipping
            if (samp[(j * 20) + i] > mmax) {
                samp[(j * 20) + i] = mmax;
            }
            if (samp[(j * 20) + i] < mmin) {
                samp[(j * 20) + i] = mmin;
            }

            aavg += fabsf(samp[i]);

            samp[(j * 20) + i] *= gain * 0.8f;

        } //i loop

        aavg /= 20.0f; //average of the 20 samples

        //debug
        // fprintf (stderr, "\nS%d - DF = %f AAVG = %f", slot, df, aavg);

        if (slot == 0) {
            if (aavg < 0.075f && state->aout_gain < 46.0f) {
                state->aout_gain += 0.5f;
            }
            if (aavg >= 0.075f && state->aout_gain > 1.0f) {
                state->aout_gain -= 0.5f;
            }
        }

        if (slot == 1) {
            if (aavg < 0.075f && state->aout_gainR < 46.0f) {
                state->aout_gainR += 0.5f;
            }
            if (aavg >= 0.075f && state->aout_gainR > 1.0f) {
                state->aout_gainR -= 0.5f;
            }
        }

        aavg = 0.0f; //reset

    } //j loop

AGF_END:; //do nothing
}

// Automatic gain for short mono paths (analog and some digital mono).
// Kept identical to the original implementation in dsd_audio2.c.
void
agsm(dsd_opts* opts, dsd_state* state, short* input, int len) {
    int i;

    UNUSED(opts);

    //NOTE: This seems to be doing better now that I got it worked out properly
    //This may produce a mild buzz sound though on the low end

    // float avg = 0.0f;    //average of 20 samples (unused)
    float coeff = 0.0f;  //gain coeffiecient
    float max = 0.0f;    //the highest sample value
    float nom = 4800.0f; //nominator value for 48k
    float samp[960];
    memset(samp, 0.0f, 960 * sizeof(float));

    //assign internal float from short input
    for (i = 0; i < len; i++) {
        samp[i] = (float)input[i];
    }

    for (i = 0; i < len; i++) {
        if (fabsf(samp[i]) > max) {
            max = fabsf(samp[i]);
        }
    }

    /* average not used; remove to avoid dead store */

    coeff = fabsf(nom / max);

    //keep coefficient with tolerable range when silence to prevent crackle/buzz
    if (coeff > 3.0f) {
        coeff = 3.0f;
    }

    //apply the coefficient to bring the max value to our desired maximum value
    for (i = 0; i < 20; i++) {
        samp[i] *= coeff;
    }

    //return new sample values post agc
    for (i = 0; i < len; i++) {
        input[i] = (short)samp[i];
    }

    state->aout_gainA = coeff; //store for internal use
}

// Manual analog gain control; uses a simple scalar derived from opts.
void
analog_gain(dsd_opts* opts, dsd_state* state, short* input, int len) {

    int i;
    UNUSED(state);

    float gain = (opts->audio_gainA / 100.0f) * 5.0f; //scale 0x - 5x

    for (i = 0; i < len; i++) {
        input[i] = (short)(input[i] * gain);
    }
}

// Automatic gain for float mono paths (analog monitor).
// Native float version avoids repeated short<->float conversions.
// Input is expected to be normalized ~[-1, 1] from the RTL demodulator.
// Output should be scaled to int16 range for PulseAudio playback.
void
agsm_f(dsd_opts* opts, dsd_state* state, float* input, int len) {
    int i;

    UNUSED(opts);

    float coeff = 0.0f;  //gain coefficient
    float max = 0.0f;    //the highest sample value
    float nom = 4800.0f; //target output level (int16 scale)

    // Find max absolute value
    for (i = 0; i < len; i++) {
        if (fabsf(input[i]) > max) {
            max = fabsf(input[i]);
        }
    }

    // Avoid division by zero
    if (max < 1e-6f) {
        max = 1e-6f;
    }

    coeff = fabsf(nom / max);

    // For normalized float input ~[-1,1], we need higher gain to reach int16 levels.
    // Cap at 6000 to prevent extreme amplification on very quiet signals.
    if (coeff > 6000.0f) {
        coeff = 6000.0f;
    }

    // Apply the coefficient to bring the max value to our desired maximum value
    for (i = 0; i < len; i++) {
        input[i] *= coeff;
    }

    state->aout_gainA = coeff; //store for internal use
}

// Manual analog gain control for float paths.
// Input may be normalized ~[-1, 1] (RTL) or PCM16-scale (WAV/other inputs).
// Uses audio_in_type to determine if base scaling is needed.
void
analog_gain_f(dsd_opts* opts, dsd_state* state, float* input, int len) {

    int i;
    UNUSED(state);

    // RTL input (type 3) produces normalized [-1,1] samples and needs base scaling.
    // All other input types (WAV, Pulse, TCP, etc.) produce PCM16-scale samples.
    float base_scale = (opts->audio_in_type == AUDIO_IN_RTL) ? 4800.0f : 1.0f;

    // User gain: 0% to 100% maps to 0x to 5x
    float user_gain = (opts->audio_gainA / 100.0f) * 5.0f;
    float gain = base_scale * user_gain;

    for (i = 0; i < len; i++) {
        input[i] *= gain;
    }
}
