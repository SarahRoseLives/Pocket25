// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Regression: IP/UDP LRRP decoding should use IPv4 IHL and UDP length fields to
 * locate and bound the UDP payload, matching SDRTrunk.
 *
 * Historically we assumed a fixed 20-byte IPv4 header (offset +28 to UDP
 * payload) and applied hard-coded length trimming, which can truncate tokens
 * (eg SPEED/HEADING) or fail when IPv4 options are present.
 */

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

void
getTimeC_buf(char out[9]) {
    snprintf(out, 9, "%s", "11:22:33");
}

void
getDateS_buf(char out[11]) {
    snprintf(out, 11, "%s", "1999/01/02");
}

// Under test
void decode_ip_pdu(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* input);

static int
expect_has_substr(const char* buf, const char* needle, const char* tag) {
    if (!buf || !strstr(buf, needle)) {
        fprintf(stderr, "%s: missing '%s' in '%s'\n", tag, needle, buf ? buf : "(null)");
        return 1;
    }
    return 0;
}

static int
expect_nonempty(const char* buf, const char* tag) {
    if (!buf || buf[0] == '\0') {
        fprintf(stderr, "%s: empty output\n", tag);
        return 1;
    }
    return 0;
}

static size_t
build_ipv4_udp_lrrp(uint8_t* out, size_t cap, uint8_t ihl_words) {
    memset(out, 0, cap);

    const size_t ip_header_len = (size_t)ihl_words * 4u;
    const size_t lrrp_len = 16u;          // LRRP header (2) + token stream (14)
    const size_t udp_len = 8u + lrrp_len; // UDP header + payload
    const size_t ip_total_len = ip_header_len + udp_len;

    if (cap < ip_total_len || ihl_words < 5) {
        return 0;
    }

    // IPv4 header
    out[0] = (uint8_t)((4u << 4) | (ihl_words & 0x0Fu)); // Version + IHL
    out[1] = 0x00;                                       // TOS
    out[2] = (uint8_t)((ip_total_len >> 8) & 0xFFu);
    out[3] = (uint8_t)(ip_total_len & 0xFFu);
    out[8] = 0x40; // TTL
    out[9] = 0x11; // UDP

    // Src IP 1.2.3.4 / Dst IP 5.6.7.8
    out[12] = 1;
    out[13] = 2;
    out[14] = 3;
    out[15] = 4;
    out[16] = 5;
    out[17] = 6;
    out[18] = 7;
    out[19] = 8;

    // IPv4 options, if any (zero-filled)
    for (size_t i = 20; i < ip_header_len; i++) {
        out[i] = 0x00;
    }

    // UDP header
    const size_t udp_off = ip_header_len;
    const uint16_t port = 4001;
    out[udp_off + 0] = (uint8_t)((port >> 8) & 0xFFu);
    out[udp_off + 1] = (uint8_t)(port & 0xFFu);
    out[udp_off + 2] = (uint8_t)((port >> 8) & 0xFFu);
    out[udp_off + 3] = (uint8_t)(port & 0xFFu);
    out[udp_off + 4] = (uint8_t)((udp_len >> 8) & 0xFFu);
    out[udp_off + 5] = (uint8_t)(udp_len & 0xFFu);
    out[udp_off + 6] = 0x00; // checksum
    out[udp_off + 7] = 0x00;

    // UDP payload (LRRP)
    size_t p = udp_off + 8u;
    out[p++] = 0x07; // Immediate Location Response
    out[p++] = 14;   // token stream length (bytes)

    // 0x66 POINT_2D: lat/lon (big-endian)
    out[p++] = 0x66;
    out[p++] = 0x10;
    out[p++] = 0x00;
    out[p++] = 0x00;
    out[p++] = 0x00; // lat = 0x10000000
    out[p++] = 0x20;
    out[p++] = 0x00;
    out[p++] = 0x00;
    out[p++] = 0x00; // lon = 0x20000000

    // 0x6C SPEED: raw units 1/100 mph -> 10.00 mph (0x03E8)
    out[p++] = 0x6C;
    out[p++] = 0x03;
    out[p++] = 0xE8;

    // 0x56 HEADING: 2-degree increments -> 90 degrees (45)
    out[p++] = 0x56;
    out[p++] = 0x2D;

    return ip_total_len;
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

    st.event_history_s = (Event_History_I*)calloc(1u, sizeof(Event_History_I));
    if (!st.event_history_s) {
        return 100;
    }

    uint8_t pkt[128];

    // Case 1: standard IPv4 header (IHL=5). Ensure SPEED/HEADING are not truncated.
    {
        size_t plen = build_ipv4_udp_lrrp(pkt, sizeof pkt, 5);
        st.dmr_lrrp_gps[0][0] = '\0';
        decode_ip_pdu(&opts, &st, (uint16_t)plen, pkt);
        rc |= expect_nonempty(st.dmr_lrrp_gps[0], "ihl=5 decoded");
        rc |= expect_has_substr(st.dmr_lrrp_gps[0], " km/h 90", "ihl=5 has speed+heading");
    }

    // Case 2: IPv4 options present (IHL=6). Decoder must honor IHL to locate UDP.
    {
        size_t plen = build_ipv4_udp_lrrp(pkt, sizeof pkt, 6);
        st.dmr_lrrp_gps[0][0] = '\0';
        decode_ip_pdu(&opts, &st, (uint16_t)plen, pkt);
        rc |= expect_nonempty(st.dmr_lrrp_gps[0], "ihl=6 decoded");
        rc |= expect_has_substr(st.dmr_lrrp_gps[0], " km/h 90", "ihl=6 has speed+heading");
    }

    free(st.event_history_s);
    st.event_history_s = NULL;

    return rc;
}
