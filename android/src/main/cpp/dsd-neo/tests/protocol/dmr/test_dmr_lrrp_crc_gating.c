// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Regression: LRRP position tokens from CRC-failed PDUs should not be written to
 * LRRP output files, to avoid emitting wildly incorrect coordinates on marginal
 * signals when CRC relaxation is enabled.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/events.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/runtime/unicode.h>

#include "test_support.h"

// Minimal stubs for direct link with dmr_pdu.c
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
    (void)input;
    (void)output;
    (void)len;
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
watchdog_event_datacall(dsd_opts* opts, dsd_state* state, uint32_t src, uint32_t dst, char* str, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)src;
    (void)dst;
    (void)str;
    (void)slot;
}

// Provide deterministic system time stubs for LRRP output (when enabled).
void
getTimeC_buf(char out[9]) {
    snprintf(out, 9, "%s", "11:22:33");
}

void
getDateS_buf(char out[11]) {
    snprintf(out, 11, "%s", "1999/01/02");
}

// Under test
void dmr_lrrp(dsd_opts* opts, dsd_state* state, uint16_t len, uint32_t source, uint32_t dest, uint8_t* DMR_PDU,
              uint8_t pdu_crc_ok);

static int
file_size_nonzero(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return (sz > 0) ? 1 : 0;
}

int
main(void) {
    static dsd_opts opts;
    static dsd_state st;
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);
    st.currentslot = 0;

    char outtmpl[DSD_TEST_PATH_MAX];
    int ofd = dsd_test_mkstemp(outtmpl, sizeof(outtmpl), "dmr_lrrp_crc_gating");
    if (ofd < 0) {
        return 100;
    }
    (void)dsd_close(ofd);
    snprintf(opts.lrrp_out_file, sizeof opts.lrrp_out_file, "%s", outtmpl);
    opts.lrrp_file_output = 1;

    // Minimal LRRP response with a POINT_2D token.
    uint8_t pdu[32];
    int i = 0;
    memset(pdu, 0, sizeof pdu);
    pdu[i++] = 0x07; // response
    pdu[i++] = 12;   // payload length (clamped by decoder anyway)
    pdu[i++] = 0x22; // pattern
    pdu[i++] = 0x00;

    // point-2d (lat/lon)
    pdu[i++] = 0x66;
    pdu[i++] = 0x10;
    pdu[i++] = 0x00;
    pdu[i++] = 0x00;
    pdu[i++] = 0x00; // lat raw
    pdu[i++] = 0x20;
    pdu[i++] = 0x00;
    pdu[i++] = 0x00;
    pdu[i++] = 0x00; // lon raw

    // CRC-failed PDU: should suppress file output of coordinates.
    dmr_lrrp(&opts, &st, (uint16_t)i, /*src*/ 111, /*dst*/ 222, pdu, /*pdu_crc_ok*/ 0);
    int nz = file_size_nonzero(outtmpl);
    if (nz != 0) {
        fprintf(stderr, "Expected empty LRRP file on CRC fail; got non-empty\n");
        remove(outtmpl);
        return 1;
    }

    // CRC-ok PDU: should write one LRRP line.
    dmr_lrrp(&opts, &st, (uint16_t)i, /*src*/ 111, /*dst*/ 222, pdu, /*pdu_crc_ok*/ 1);
    nz = file_size_nonzero(outtmpl);
    if (nz != 1) {
        fprintf(stderr, "Expected non-empty LRRP file on CRC ok; got empty\n");
        remove(outtmpl);
        return 2;
    }

    remove(outtmpl);
    return 0;
}
