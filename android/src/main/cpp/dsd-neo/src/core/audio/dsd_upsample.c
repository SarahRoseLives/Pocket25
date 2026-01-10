// SPDX-License-Identifier: ISC
/*-------------------------------------------------------------------------------
 * dsd_upsample.c
 * Simplified 8k to 48k Upsample Functions
 * Uses linear interpolation for smooth transitions without ringing.
 *
 *
 *
 * LWVMOBILE
 * 2024-03 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/state.h>

// Produce 6 short samples (48k) for every 1 short sample (8k)
// Uses linear interpolation between prev and invalue for smoother output.
// Interpolation coefficients: [0/6, 1/6, 2/6, 3/6, 4/6, 5/6] from prev to invalue
// The sample at coefficient 6/6 (= invalue) is the first sample of the next call.
void
upsampleS(short invalue, short prev, short outbuf[6]) {
    // Linear interpolation from prev toward invalue
    // outbuf[i] = prev + (invalue - prev) * (i / 6)
    float diff = (float)(invalue - prev);
    outbuf[0] = prev;                                 // 0/6 = prev
    outbuf[1] = (short)(prev + diff * (1.0f / 6.0f)); // 1/6
    outbuf[2] = (short)(prev + diff * (2.0f / 6.0f)); // 2/6
    outbuf[3] = (short)(prev + diff * (3.0f / 6.0f)); // 3/6
    outbuf[4] = (short)(prev + diff * (4.0f / 6.0f)); // 4/6
    outbuf[5] = (short)(prev + diff * (5.0f / 6.0f)); // 5/6
}

// Float version: produce 6 float samples for every 1 float sample.
// Uses linear interpolation for smooth output.
void
upsampleF(float invalue, float prev, float outbuf[6]) {
    float diff = invalue - prev;
    outbuf[0] = prev;                        // 0/6 = prev
    outbuf[1] = prev + diff * (1.0f / 6.0f); // 1/6
    outbuf[2] = prev + diff * (2.0f / 6.0f); // 2/6
    outbuf[3] = prev + diff * (3.0f / 6.0f); // 3/6
    outbuf[4] = prev + diff * (4.0f / 6.0f); // 4/6
    outbuf[5] = prev + diff * (5.0f / 6.0f); // 5/6
}

// Legacy 6x simplified version for the float_buf_p (sample repetition)
// Maintained for backward compatibility where interpolation isn't desired.
void
upsample(dsd_state* state, float invalue) {

    if (!state) {
        return;
    }

    float* outbuf1 = state->audio_out_float_buf_p;
    if (state->dmr_stereo == 1) {
        outbuf1 = (state->currentslot == 1) ? state->audio_out_float_buf_pR : state->audio_out_float_buf_p;
    }
    if (!outbuf1) {
        return; // nothing to write to
    }

    *outbuf1 = invalue;
    outbuf1++;
    *outbuf1 = invalue;
    outbuf1++;
    *outbuf1 = invalue;
    outbuf1++;
    *outbuf1 = invalue;
    outbuf1++;
    *outbuf1 = invalue;
    outbuf1++;
    *outbuf1 = invalue;
}
