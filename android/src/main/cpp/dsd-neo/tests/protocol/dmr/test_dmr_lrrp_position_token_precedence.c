// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Regression/parity: SDRTrunk selects the first Point2d-derived token after sorting LRRP tokens by TokenType.
 * That yields a deterministic precedence among position tokens:
 *   CIRCLE_2D (0x51) > CIRCLE_3D (0x55) > POINT_2D (0x66) > POINT_3D (0x69)
 *
 * Ensure we mirror that selection even when multiple position tokens are present.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/bit_packing.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/unicode.h>

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
    if (len > 0) {
        memset(output, 0, (size_t)len);
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
watchdog_event_datacall(dsd_opts* opts, dsd_state* state, uint32_t src, uint32_t dst, char* str, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)src;
    (void)dst;
    (void)str;
    (void)slot;
}

// Deterministic system time stubs (not used: file output disabled)
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
expect_has_point(const char* s, double exp_lat, double exp_lon, const char* tag) {
    const char* p = strchr(s, '(');
    if (!p) {
        fprintf(stderr, "%s: missing '('\n", tag);
        return 1;
    }

    double lat = 0.0;
    double lon = 0.0;
    if (sscanf(p, "(%lf, %lf)", &lat, &lon) != 2) {
        fprintf(stderr, "%s: failed to parse coordinates from '%s'\n", tag, s);
        return 1;
    }

    double dlat = lat - exp_lat;
    if (dlat < 0.0) {
        dlat = -dlat;
    }
    double dlon = lon - exp_lon;
    if (dlon < 0.0) {
        dlon = -dlon;
    }
    if (dlat > 1e-5 || dlon > 1e-5) {
        fprintf(stderr, "%s: got (%.8lf, %.8lf) expected (%.8lf, %.8lf)\n", tag, lat, lon, exp_lat, exp_lon);
        return 1;
    }

    return 0;
}

static void
expected_from_raw(uint32_t lat_raw, uint32_t lon_raw, double* out_lat, double* out_lon) {
    const double lat_unit = 180.0 / 4294967295.0;
    const double lon_unit = 360.0 / 4294967295.0;

    int lat_sign = 1;
    uint32_t lat_mag = lat_raw;
    if (lat_mag & 0x80000000u) {
        lat_mag &= 0x7FFFFFFFu;
        lat_sign = -1;
    }

    *out_lat = (double)lat_mag * lat_unit * (double)lat_sign;
    *out_lon = (double)((int32_t)lon_raw) * lon_unit;
}

int
main(void) {
    int rc = 0;

    static dsd_opts opts;
    static dsd_state st;
    memset(&opts, 0, sizeof opts);
    memset(&st, 0, sizeof st);
    st.currentslot = 0;
    opts.lrrp_file_output = 0;

    // Two distinct positions: POINT_3D first, then CIRCLE_2D.
    uint32_t lat_p3d = 0x10000000u;
    uint32_t lon_p3d = 0x20000000u;
    uint32_t lat_c2d = 0x30000000u;
    uint32_t lon_c2d = 0x40000000u;

    double exp_lat = 0.0;
    double exp_lon = 0.0;
    expected_from_raw(lat_c2d, lon_c2d, &exp_lat, &exp_lon);

    uint8_t pdu[64];
    memset(pdu, 0, sizeof pdu);
    size_t i = 0;
    pdu[i++] = 0x07; // Immediate Location Response
    pdu[i++] = 23;   // payload length: POINT_3D (12) + CIRCLE_2D (11)

    // 0x69 POINT_3D (lat/lon + 24-bit altitude)
    pdu[i++] = 0x69;
    pdu[i++] = (lat_p3d >> 24) & 0xFF;
    pdu[i++] = (lat_p3d >> 16) & 0xFF;
    pdu[i++] = (lat_p3d >> 8) & 0xFF;
    pdu[i++] = (lat_p3d >> 0) & 0xFF;
    pdu[i++] = (lon_p3d >> 24) & 0xFF;
    pdu[i++] = (lon_p3d >> 16) & 0xFF;
    pdu[i++] = (lon_p3d >> 8) & 0xFF;
    pdu[i++] = (lon_p3d >> 0) & 0xFF;
    pdu[i++] = 0x00; // alt[23:16]
    pdu[i++] = 0x01; // alt[15:8]
    pdu[i++] = 0x02; // alt[7:0]

    // 0x51 CIRCLE_2D (lat/lon + 16-bit radius)
    pdu[i++] = 0x51;
    pdu[i++] = (lat_c2d >> 24) & 0xFF;
    pdu[i++] = (lat_c2d >> 16) & 0xFF;
    pdu[i++] = (lat_c2d >> 8) & 0xFF;
    pdu[i++] = (lat_c2d >> 0) & 0xFF;
    pdu[i++] = (lon_c2d >> 24) & 0xFF;
    pdu[i++] = (lon_c2d >> 16) & 0xFF;
    pdu[i++] = (lon_c2d >> 8) & 0xFF;
    pdu[i++] = (lon_c2d >> 0) & 0xFF;
    pdu[i++] = 0x00; // radius[15:8]
    pdu[i++] = 0x64; // radius[7:0] -> 1.00m (hundredths)

    dmr_lrrp(&opts, &st, (uint16_t)i, /*src*/ 123, /*dst*/ 456, pdu, 1);
    rc |= expect_has_point(st.dmr_lrrp_gps[0], exp_lat, exp_lon, "position precedence");

    return rc;
}
