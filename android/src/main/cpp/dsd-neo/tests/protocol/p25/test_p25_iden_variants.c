// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 IDEN variants: TDMA denominator behavior and map override
 */

#include <stdint.h>
#include <stdio.h>

// Test shim helper
int p25_test_frequency_for(int iden, int type, int tdma, long base, int spac, int chan16, long map_override,
                           long* out_freq);

// Minimal stubs to satisfy linked objects from the P25 proto library
typedef struct dsd_opts dsd_opts;
typedef struct dsd_state dsd_state;

void
unpack_byte_array_into_bit_array(uint8_t* input, uint8_t* output, int len) {
    (void)input;
    (void)output;
    (void)len;
}

void
apx_embedded_alias_header_phase2(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
apx_embedded_alias_blocks_phase2(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t* lc_bits) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)lc_bits;
}

void
l3h_embedded_alias_decode(dsd_opts* opts, dsd_state* state, uint8_t slot, int16_t len, uint8_t* input) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)len;
    (void)input;
}

void
nmea_harris(dsd_opts* opts, dsd_state* state, uint8_t* input, uint32_t src, int slot) {
    (void)opts;
    (void)state;
    (void)input;
    (void)src;
    (void)slot;
}

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}
struct RtlSdrContext* g_rtl_ctx = 0;

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

static int
expect_eq_long(const char* tag, long got, long want) {
    if (got != want) {
        fprintf(stderr, "%s: got %ld want %ld\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    long f10 = 0, f11 = 0;
    // TDMA iden with type=3 (slots_per_carrier=2): channels with LSB differing
    // map to same FDMA-equivalent frequency.
    // Use small base/spac to keep arithmetic simple; units are internal.
    if (p25_test_frequency_for(/*iden*/ 3, /*type*/ 3, /*tdma*/ 1, /*base*/ 1000, /*spac*/ 1,
                               /*chan16*/ (3 << 12) | 10, /*map_override*/ 0, &f10)
        != 0) {
        return 1;
    }
    if (p25_test_frequency_for(/*iden*/ 3, /*type*/ 3, /*tdma*/ 1, /*base*/ 1000, /*spac*/ 1,
                               /*chan16*/ (3 << 12) | 11, /*map_override*/ 0, &f11)
        != 0) {
        return 2;
    }
    if (expect_eq_long("TDMA denom eq", f10, f11)) {
        return 3;
    }

    // FDMA spacing: adjacent channels differ by spac*125 units.
    long fA = 0, fB = 0;
    int spac = 2;
    if (p25_test_frequency_for(/*iden*/ 1, /*type*/ 0, /*tdma*/ 0, /*base*/ 1000, /*spac*/ spac,
                               /*chan16*/ (1 << 12) | 20, /*map_override*/ 0, &fA)
        != 0) {
        return 4;
    }
    if (p25_test_frequency_for(/*iden*/ 1, /*type*/ 0, /*tdma*/ 0, /*base*/ 1000, /*spac*/ spac,
                               /*chan16*/ (1 << 12) | 21, /*map_override*/ 0, &fB)
        != 0) {
        return 5;
    }
    long diff = fB - fA;
    long want = 125L * spac;
    if (expect_eq_long("FDMA delta", diff, want)) {
        return 6;
    }

    // Direct channel map override wins regardless of iden params
    long fC = 0;
    long override = 123456789L;
    if (p25_test_frequency_for(/*iden*/ 2, /*type*/ 0, /*tdma*/ 0, /*base*/ 0, /*spac*/ 0,
                               /*chan16*/ (2 << 12) | 15, /*map_override*/ override, &fC)
        != 0) {
        return 7;
    }
    if (expect_eq_long("override", fC, override)) {
        return 8;
    }

    fprintf(stderr, "P25 IDEN variant checks passed\n");
    return 0;
}
