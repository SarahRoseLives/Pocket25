// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Validate LOCN (NMEA-like) date handling: out-of-range decoded dates should
 * be ignored and system time used instead in the LRRP output file.
 */

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/bit_packing.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/runtime/unicode.h>

#include "test_support.h"

// Minimal stubs required by dmr_pdu.c when linked directly
const char*
dsd_degrees_glyph(void) {
    return "";
}

int
dsd_unicode_supported(void) {
    return 0;
}

void
unpack_byte_array_into_bit_array(uint8_t* input, uint8_t* output, int len) {
    if (!input || !output || len <= 0) {
        return;
    }

    int k = 0;
    for (int i = 0; i < len; i++) {
        output[k++] = (input[i] >> 7) & 1;
        output[k++] = (input[i] >> 6) & 1;
        output[k++] = (input[i] >> 5) & 1;
        output[k++] = (input[i] >> 4) & 1;
        output[k++] = (input[i] >> 3) & 1;
        output[k++] = (input[i] >> 2) & 1;
        output[k++] = (input[i] >> 1) & 1;
        output[k++] = (input[i] >> 0) & 1;
    }
}

void
lip_protocol_decoder(dsd_opts* opts, dsd_state* state, uint8_t* input) {
    (void)opts;
    (void)state;
    (void)input;
}

void
decode_cellocator(dsd_opts* opts, dsd_state* state, uint8_t* input, int len) {
    (void)opts;
    (void)state;
    (void)input;
    (void)len;
}

void
watchdog_event_datacall(dsd_opts* opts, dsd_state* state, uint32_t src, uint32_t dst, char* data_string, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)src;
    (void)dst;
    (void)data_string;
    (void)slot;
}

// Deterministic system time fallback for file writer
void
getTimeC_buf(char out[9]) {
    snprintf(out, 9, "%s", "01:23:45");
}

void
getDateS_buf(char out[11]) {
    snprintf(out, 11, "%s", "2004/05/06");
}

// Under test
void dmr_locn(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* DMR_PDU);

static int
expect_nonempty(const char* buf, const char* tag) {
    if (!buf || buf[0] == '\0') {
        fprintf(stderr, "%s: empty\n", tag);
        return 1;
    }
    return 0;
}

static int
expect_no_substr(const char* buf, const char* needle, const char* tag) {
    if (strstr(buf, needle)) {
        fprintf(stderr, "%s: found unexpected '%s'\n", tag, needle);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;

    static dsd_opts opts;
    static dsd_state st;
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);
    st.currentslot = 0;
    st.dmr_lrrp_source[0] = 0x12345678; // any non-zero

    // Temp LRRP output path
    char outtmpl[DSD_TEST_PATH_MAX];
    int ofd = dsd_test_mkstemp(outtmpl, sizeof(outtmpl), "dmr_locn_time_fallback");
    if (ofd < 0) {
        return 100;
    }
    (void)dsd_close(ofd);
    snprintf(opts.lrrp_out_file, sizeof opts.lrrp_out_file, "%s", outtmpl);
    opts.lrrp_file_output = 1;

    // Build LOCN payload with invalid BCD year 2038
    uint8_t pdu[64];
    int i = 0;
    memset(pdu, 0, sizeof pdu);

    // 'A' time/date token with invalid year 38 (-> 2038)
    pdu[i++] = 0x41; // 'A'
    pdu[i++] = '1';
    pdu[i++] = '2'; // hour 12
    pdu[i++] = '3';
    pdu[i++] = '4'; // minute 34
    pdu[i++] = '5';
    pdu[i++] = '6'; // second 56
    pdu[i++] = '0';
    pdu[i++] = '7'; // day 07
    pdu[i++] = '0';
    pdu[i++] = '8'; // month 08
    pdu[i++] = '3';
    pdu[i++] = '8'; // year 38 -> 2038 (invalid)

    // 'N' latitude: ddmm.mmmm -> using dd=12, mm=34, mmmm=5678
    pdu[i++] = 0x4E; // 'N'
    pdu[i++] = '1';
    pdu[i++] = '2';
    pdu[i++] = '3';
    pdu[i++] = '4';
    pdu[i++] = '.'; // dot ignored by decoder (will be treated as non-digit; but decoder reads separate sec digits)
    pdu[i++] = '5';
    pdu[i++] = '6';
    pdu[i++] = '7';
    pdu[i++] = '8';

    // 'E' longitude: dddmm.mmmm -> 123,45,6789
    pdu[i++] = 0x45; // 'E'
    pdu[i++] = '1';
    pdu[i++] = '2';
    pdu[i++] = '3';
    pdu[i++] = '4';
    pdu[i++] = '5';
    pdu[i++] = '.';
    pdu[i++] = '6';
    pdu[i++] = '7';
    pdu[i++] = '8';
    pdu[i++] = '9';

    dmr_locn(&opts, &st, (uint16_t)i, pdu);

    // Read LRRP file content
    FILE* f = fopen(outtmpl, "rb");
    if (!f) {
        return 101;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t psz = 0;
    if (sz > 0 && (unsigned long)sz <= (unsigned long)(SIZE_MAX - 1)) {
        psz = (size_t)sz;
    }
    char* buf = calloc(psz + 1u, 1u);
    if (!buf) {
        fclose(f);
        remove(outtmpl);
        return 102;
    }
    fread(buf, 1, psz, f);
    fclose(f);

    rc |= expect_nonempty(buf, "LOCN LRRP file non-empty");
    rc |= expect_no_substr(buf, "2038/", "LOCN excludes bogus decoded year");
    free(buf);

    remove(outtmpl);
    return rc;
}
