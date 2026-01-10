// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Quantify gains of Viterbi (hard and soft) under injected dibit noise.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/dmr/r34_viterbi.h>

// Simple deterministic RNG
static uint32_t rng_state = 0xC0FFEEU;

static inline uint32_t
xrng(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state;
}

static inline int
popcnt8(uint8_t v) {
    v = (uint8_t)(v - ((v >> 1) & 0x55));
    v = (uint8_t)((v & 0x33) + ((v >> 2) & 0x33));
    return (int)((((v + (v >> 4)) & 0x0F) * 0x01u) & 0x1Fu);
}

static int
bit_errors_144(const uint8_t ref[18], const uint8_t got[18]) {
    int e = 0;
    for (int i = 0; i < 18; i++) {
        e += popcnt8((uint8_t)(ref[i] ^ got[i]));
    }
    return e;
}

static void
gen_payload(uint8_t out[18]) {
    for (int i = 0; i < 18; i++) {
        out[i] = (uint8_t)(xrng() & 0xFFu);
    }
}

static void
inject_noise_dibits(uint8_t dibits[98], uint8_t reliab[98], int flips_per_1000) {
    // For each dibit, flip with probability flips_per_1000 / 1000.
    for (int i = 0; i < 98; i++) {
        uint32_t r = xrng() % 1000u;
        if ((int)r < flips_per_1000) {
            // flip to a random different dibit value
            uint8_t cur = dibits[i] & 0x3u;
            uint8_t alt = (uint8_t)(xrng() & 0x3u);
            if (alt == cur) {
                alt = (uint8_t)((alt + 1) & 0x3u);
            }
            dibits[i] = alt;
            reliab[i] = 24; // low confidence
        } else {
            reliab[i] = 240; // high confidence
        }
    }
}

int
main(void) {
    const int trials = 64;
    const int noise = 50; // ~5% dibit flips
    int total_err_hard = 0;
    int total_err_soft = 0;

    for (int t = 0; t < trials; t++) {
        uint8_t payload[18];
        gen_payload(payload);

        // Encode to dibits
        uint8_t clean[98];
        int erc = dmr_r34_encode(payload, clean);
        assert(erc == 0);

        // Create noisy copies + reliabilities
        uint8_t noisy[98];
        memcpy(noisy, clean, sizeof(noisy));
        uint8_t reliab[98];
        inject_noise_dibits(noisy, reliab, noise);

        // Decode (hard)
        uint8_t dec_hard[18];
        memset(dec_hard, 0, sizeof(dec_hard));
        int rc_h = dmr_r34_viterbi_decode(noisy, dec_hard);
        assert(rc_h == 0);

        // Decode (soft)
        uint8_t dec_soft[18];
        memset(dec_soft, 0, sizeof(dec_soft));
        int rc_s = dmr_r34_viterbi_decode_soft(noisy, reliab, dec_soft);
        assert(rc_s == 0);

        // Compare to truth
        total_err_hard += bit_errors_144(payload, dec_hard);
        total_err_soft += bit_errors_144(payload, dec_soft);
    }

    printf("DMR R3/4 noise trials=%d hard_err=%d soft_err=%d\n", trials, total_err_hard, total_err_soft);
    // Soft should not be worse than hard under this synthetic noise model
    assert(total_err_soft <= total_err_hard);
    return 0;
}
