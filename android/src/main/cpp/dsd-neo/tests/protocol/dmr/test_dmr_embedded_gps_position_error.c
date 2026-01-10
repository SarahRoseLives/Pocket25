// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Parity: DMR embedded GPS Position Error (ETSI TS 102 361-2 7.2.15) matches SDRTrunk.
 * - 0..5: less than 2*10^n meters
 * - 6:    more than 200 kilometers
 * - 7:    unknown
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/protocol/dmr/dmr_utils_api.h>
#include <dsd-neo/runtime/unicode.h>

void dmr_embedded_gps(dsd_opts* opts, dsd_state* state, uint8_t lc_bits[]);

// Minimal stubs for direct link with dsd_gps.c
uint64_t
ConvertBitIntoBytes(uint8_t* BufferIn, uint32_t BitLength) {
    uint64_t out = 0;
    uint8_t* p = BufferIn;
    uint32_t n = BitLength;

    while (n--) {
        out = (out << 1) | (uint64_t)(*p++ & 1);
    }

    return out;
}

const char*
dsd_degrees_glyph(void) {
    return "";
}

char*
getTime(void) {
    return NULL;
}

char*
getDate(void) {
    return NULL;
}

void
getTime_buf(char out[7]) {
    snprintf(out, 7, "%s", "000000");
}

void
getDate_buf(char out[9]) {
    snprintf(out, 9, "%s", "00000000");
}

void
utf8_to_text(dsd_state* state, uint8_t wr, uint16_t len, uint8_t* input) {
    (void)state;
    (void)wr;
    (void)len;
    (void)input;
}

static int
expect_has_substr(const char* buf, const char* needle, const char* tag) {
    if (!buf || !strstr(buf, needle)) {
        fprintf(stderr, "%s: missing '%s' in '%s'\n", tag, needle, buf ? buf : "(null)");
        return 1;
    }
    return 0;
}

static void
set_pos_err(uint8_t* lc_bits, uint8_t pos_err) {
    // pos_err is 3 bits at positions 20..22, MSB-first.
    lc_bits[20] = (pos_err >> 2) & 1;
    lc_bits[21] = (pos_err >> 1) & 1;
    lc_bits[22] = (pos_err >> 0) & 1;
}

int
main(void) {
    int rc = 0;

    static dsd_opts opts;
    static dsd_state st;
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);
    opts.lrrp_file_output = 0;
    st.currentslot = 0;

    st.event_history_s = (Event_History_I*)calloc(2u, sizeof(Event_History_I));
    if (!st.event_history_s) {
        return 100;
    }

    uint8_t lc_bits[80];
    memset(lc_bits, 0, sizeof lc_bits);

    // pos_err == 5: less than 200km (200000m)
    {
        memset(st.dmr_embedded_gps[0], 0, sizeof st.dmr_embedded_gps[0]);
        set_pos_err(lc_bits, 5);
        dmr_embedded_gps(&opts, &st, lc_bits);
        rc |= expect_has_substr(st.dmr_embedded_gps[0], "Err: 200000m", "pos_err=5");
    }

    // pos_err == 6: more than 200km
    {
        memset(st.dmr_embedded_gps[0], 0, sizeof st.dmr_embedded_gps[0]);
        set_pos_err(lc_bits, 6);
        dmr_embedded_gps(&opts, &st, lc_bits);
        rc |= expect_has_substr(st.dmr_embedded_gps[0], "Err: >200km", "pos_err=6");
    }

    // pos_err == 7: unknown
    {
        memset(st.dmr_embedded_gps[0], 0, sizeof st.dmr_embedded_gps[0]);
        set_pos_err(lc_bits, 7);
        dmr_embedded_gps(&opts, &st, lc_bits);
        rc |= expect_has_substr(st.dmr_embedded_gps[0], "Unknown Pos Err", "pos_err=7");
    }

    free(st.event_history_s);
    st.event_history_s = NULL;

    return rc;
}
