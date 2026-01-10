// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Exercise P25p1 PDU Data path through Extended Address (SAP 31)
 * followed by Encryption Sync Header (SAP 1) that signals aux SAP 32.
 * This drives p25_decode_extended_address and p25_decode_es_header paths.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward minimal types and hooks
#include "test_support.h"

#define setenv dsd_test_setenv

typedef struct dsd_opts dsd_opts;
typedef struct dsd_state dsd_state;
typedef struct dsdneoRuntimeConfig dsdneoRuntimeConfig;
void dsd_neo_config_init(const dsd_opts* opts);
const dsdneoRuntimeConfig* dsd_neo_get_config(void);

// Shim to invoke real decoder
void p25_test_p1_pdu_data_decode(const unsigned char* input, int len);

// Stubs required by linked decoder units
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
watchdog_event_history(dsd_opts* opts, dsd_state* state, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)slot;
}

void
watchdog_event_current(dsd_opts* opts, dsd_state* state, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)slot;
}

void
utf8_to_text(dsd_state* state, uint8_t wr, uint16_t len, uint8_t* input) {
    (void)state;
    (void)wr;
    (void)len;
    (void)input;
}

// Bit helpers expected by p25p1_pdu_data.c implementation
void
unpack_byte_array_into_bit_array(uint8_t* input, uint8_t* output, int len) {
    // MSB-first bit unpack
    for (int i = 0; i < len * 8; i++) {
        int byte = i / 8;
        int bit = 7 - (i % 8);
        output[i] = (input[byte] >> bit) & 1;
    }
}

uint64_t
ConvertBitIntoBytes(uint8_t* BufferIn, uint32_t BitLength) {
    uint64_t v = 0;
    for (uint32_t i = 0; i < BitLength; i++) {
        v = (v << 1) | (BufferIn[i] & 1);
    }
    return v;
}

void
decode_ip_pdu(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* input) {
    (void)opts;
    (void)state;
    (void)len;
    (void)input;
}

// Additional stubs for rigctl path
bool
SetFreq(int sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return false;
}

bool
SetModulation(int sockfd, int bandwidth) {
    (void)sockfd;
    (void)bandwidth;
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

static void
pack_bits(uint8_t* dst, size_t cap_bytes, int bit_offset, int width, uint32_t value) {
    // MSB-first placement into dst byte array
    for (int i = 0; i < width; i++) {
        int b = (value >> (width - 1 - i)) & 1;
        int pos = bit_offset + i;
        int byte = pos / 8;
        int bit = 7 - (pos % 8);
        if ((size_t)byte >= cap_bytes) {
            break; // guard against overflow when called with bad params
        }
        dst[byte] = (uint8_t)(dst[byte] & ~(1u << bit));
        dst[byte] = (uint8_t)(dst[byte] | (b << bit));
    }
}

static int
parse_last_json(const char* buf, int len, int* out_sap) {
    if (!buf || len <= 0) {
        return -1;
    }
    const char* last_nl = strrchr(buf, '\n');
    const char* line = last_nl ? (last_nl + 1) : buf;
    int sap = -1;
    const char* q = strstr(line, "\"sap\":");
    if (!q || sscanf(q, "\"sap\":%d", &sap) != 1) {
        return -1;
    }
    if (out_sap) {
        *out_sap = sap;
    }
    return 0;
}

int
main(void) {
    // Enable JSON emission
    setenv("DSD_NEO_PDU_JSON", "1", 1);
    dsd_neo_config_init(NULL);

    dsd_test_capture_stderr cap;
    if (dsd_test_capture_stderr_begin(&cap, "p25_p1_pdu_es_ext") != 0) {
        fprintf(stderr, "Failed to capture stderr: %s\n", strerror(errno));
        return 101;
    }

    // Build PDU with initial SAP=31 (extended address), which sets aux SAP=1,
    // then ES header sets aux_sap=32 (RegAuth). Follow with small payload.
    uint8_t pdu[96];
    memset(pdu, 0, sizeof(pdu));

    pdu[0] = 0x10; // fmt=16, io=0
    pdu[1] = 31;   // SAP 31 triggers extended addressing
    pdu[2] = 0x22; // MFID (header level)
    pdu[6] = 0x02; // blks
    pdu[7] = 0x00; // pad
    pdu[9] = 0x00; // offset

    // Extended Address header (12 bytes) at offset 12
    // Layout: ea_sap @ bit 10 (6b) → 1; ea_mfid @16 (6b) → 0x15; ea_llid @24 (24b) → 0x000102
    uint8_t* ext = pdu + 12;
    memset(ext, 0, 12);
    pack_bits(ext, 12, 10, 6, 1);         // ea_sap = 1 (encryption header follows)
    pack_bits(ext, 12, 16, 6, 0x15);      // ea_mfid
    pack_bits(ext, 12, 24, 24, 0x000102); // ea_llid

    // ES header (13 bytes) immediately after ext
    uint8_t* es = pdu + 12 + 12;
    memset(es, 0, 13);
    // MI 64-bit: 0x0102030405060708
    es[0] = 0x01;
    es[1] = 0x02;
    es[2] = 0x03;
    es[3] = 0x04;
    es[4] = 0x05;
    es[5] = 0x06;
    es[6] = 0x07;
    es[7] = 0x08;
    // mi_res (8b) = 0 (already zero)
    // alg_id (8b) at bits[72..79] = 0x80 (clear)
    es[9] = 0x80; // aligns with unpack→bits mapping for subsequent fields
    // key_id (16b) bits[80..95] = 0x1234
    es[10] = 0x12;
    es[11] = 0x34;
    // aux_res (2b) at [96..97] = 3, aux_sap (6b) [98..103] = 32
    pack_bits(es, 13, 96, 2, 3);
    pack_bits(es, 13, 98, 6, 32);

    // Minimal payload for SAP 32 after headers (place at current index)
    int payload_off = 12 + 12 + 13;
    pdu[payload_off + 0] = 0x42;
    pdu[payload_off + 1] = 0x99;
    pdu[payload_off + 2] = 0x00;

    int total_len = payload_off + 3 + 4; // +CRC
    p25_test_p1_pdu_data_decode(pdu, total_len);

    dsd_test_capture_stderr_end(&cap);

    FILE* rf = fopen(cap.path, "rb");
    if (!rf) {
        fprintf(stderr, "fopen read failed\n");
        return 102;
    }
    fseek(rf, 0, SEEK_END);
    long sz = ftell(rf);
    if (sz < 0) {
        fclose(rf);
        return 103;
    }
    fseek(rf, 0, SEEK_SET);
    size_t alloc = (size_t)sz + 1;
    char* buf = (char*)calloc(alloc, 1);
    size_t nread = fread(buf, 1, alloc - 1, rf);
    if (nread >= alloc) {
        nread = alloc - 1;
    }
    fclose(rf);

    int sap = -1;
    if (parse_last_json(buf, (int)nread, &sap) != 0) {
        free(buf);
        return 103;
    }
    free(buf);
    // Expect aux_sap=32 (RegAuth) after ES header
    if (sap != 32) {
        fprintf(stderr, "expected SAP 32 after ES header, got %d\n", sap);
        return 1;
    }
    (void)remove(cap.path);
    return 0;
}
