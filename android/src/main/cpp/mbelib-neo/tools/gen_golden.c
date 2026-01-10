// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Utility: generate golden FNV-1a hashes for deterministic synthesis.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mbelib-neo/mbelib.h"

/**
 * @brief Compute 32-bit FNV-1a hash of a byte buffer.
 * @param data Pointer to input buffer.
 * @param len  Buffer length in bytes.
 * @return 32-bit FNV-1a hash.
 */
static uint32_t
fnv1a32(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = 2166136261u; // FNV offset basis
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 16777619u; // FNV prime
    }
    return h;
}

/**
 * @brief Initialize deterministic synthesis parameters for hashing.
 * @param cur  Output current parameter set.
 * @param prev Output previous parameter set (copy of current).
 */
static void
fill_params(mbe_parms* cur, mbe_parms* prev) {
    mbe_parms enh;
    mbe_initMbeParms(cur, prev, &enh);
    cur->w0 = 0.105f;
    cur->L = 36;
    for (int l = 1; l <= cur->L; ++l) {
        cur->Vl[l] = (l % 4) ? 1 : 0;
        cur->Ml[l] = 0.035f + 0.0015f * (float)l;
        cur->PHIl[l] = (float)l * 0.03f;
        cur->PSIl[l] = (float)l * 0.02f;
    }
    *prev = *cur;
}

/**
 * @brief Program entry: prints float and int16 golden hashes to stdout.
 */
int
main(void) {
    float out_f[160];
    short out_s[160];
    mbe_parms cur, prev;

    mbe_setThreadRngSeed(0xC0FFEEu);
    fill_params(&cur, &prev);
    mbe_synthesizeSpeechf(out_f, &cur, &prev, 8);

    // Hash float bytes and also short bytes after conversion
    uint32_t hf = fnv1a32(out_f, sizeof(out_f));
    mbe_floattoshort(out_f, out_s);
    uint32_t hs = fnv1a32(out_s, sizeof(out_s));

    printf("GOLDEN_F32_FNV1A=0x%08X\n", (unsigned)hf);
    printf("GOLDEN_S16_FNV1A=0x%08X\n", (unsigned)hs);
    return 0;
}
