// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 channel→frequency mapping tests (FDMA/TDMA + overrides).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Use shim to avoid pulling in external deps like mbelib.
int p25_test_frequency_for(int iden, int type, int tdma, long base, int spac, int chan16, long map_override,
                           long* out_freq);

// Stubs to satisfy linked objects from p25 proto lib
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

bool
SetFreq(int sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return false;
}

bool
SetModulation(int sockfd, int bw) {
    (void)sockfd;
    (void)bw;
    return false;
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
    int rc = 0;
    // Case 1: FDMA mapping
    {
        long f0 = 0, fA = 0;
        p25_test_frequency_for(1, /*type*/ 1, /*tdma*/ 0, 851000000 / 5, 100, 0x1000, 0, &f0);
        p25_test_frequency_for(1, /*type*/ 1, /*tdma*/ 0, 851000000 / 5, 100, 0x100A, 0, &fA);
        rc |= expect_eq_long("FDMA ch=0", f0, 851000000);
        rc |= expect_eq_long("FDMA ch=10", fA, 851000000 + 10 * 100 * 125);
    }

    // Case 2: TDMA mapping with slots-per-carrier division (type=4 => denom=4)
    {
        long f = 0;
        p25_test_frequency_for(2, /*type*/ 4, /*tdma*/ 1, 935000000 / 5, 100, 0x2004, 0, &f);
        rc |= expect_eq_long("TDMA type4 ch=4", f, 935000000 + 1 * 100 * 125);
    }

    // Case 3: trunk_chan_map override
    {
        long f = 0;
        p25_test_frequency_for(1, /*type*/ 1, /*tdma*/ 0, 851000000 / 5, 100, 0x1005, 762000000, &f);
        rc |= expect_eq_long("map override", f, 762000000);
    }

    return rc;
}
