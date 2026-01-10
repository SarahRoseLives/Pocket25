// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Verify P25 Phase 1 PDU JSON emission for data SAPs (RegAuth, SysCfg).
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Call into shim to keep dependencies narrow.

#include "test_support.h"

#define setenv dsd_test_setenv

typedef struct dsdneoRuntimeConfig dsdneoRuntimeConfig;
typedef struct dsd_opts dsd_opts;
typedef struct dsd_state dsd_state;
void dsd_neo_config_init(const dsd_opts* opts);
const dsdneoRuntimeConfig* dsd_neo_get_config(void);

// Use a local shim that sets up real opts/state in a separate TU.
void p25_test_p1_pdu_data_decode(const unsigned char* input, int len);

// Stubs referenced by PDU data path
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

uint64_t
ConvertBitIntoBytes(uint8_t* BufferIn, uint32_t BitLength) {
    // Simple MSB-first packer
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

// Additional stubs referenced by linked objects (rigctl/rtl streaming)
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

static int
expect_eq_int(const char* tag, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: got %d want %d\n", tag, got, want);
        return 1;
    }
    return 0;
}

static int
expect_str_contains(const char* tag, const char* hay, const char* needle) {
    if (!strstr(hay, needle)) {
        fprintf(stderr, "%s: missing '%s' in '%s'\n", tag, needle, hay);
        return 1;
    }
    return 0;
}

static int
parse_last_json(const char* buf, int len, int* out_sap, int* out_mfid, int* out_io, int* out_len, char* out_summary,
                size_t sum_cap) {
    // find last JSON line
    if (!buf || len <= 0) {
        return -1;
    }
    const char* last_nl = strrchr(buf, '\n');
    const char* line = last_nl ? (last_nl + 1) : buf;

    int sap = -1, mfid = -1, io = -1, jlen = -1;
    const char* q = strstr(line, "\"sap\":");
    if (!q || sscanf(q, "\"sap\":%d", &sap) != 1) {
        return -1;
    }
    q = strstr(line, "\"mfid\":");
    if (!q || sscanf(q, "\"mfid\":%d", &mfid) != 1) {
        return -2;
    }
    q = strstr(line, "\"io\":");
    if (!q || sscanf(q, "\"io\":%d", &io) != 1) {
        return -3;
    }
    q = strstr(line, "\"len\":");
    if (!q || sscanf(q, "\"len\":%d", &jlen) != 1) {
        return -4;
    }

    if (out_sap) {
        *out_sap = sap;
    }
    if (out_mfid) {
        *out_mfid = mfid;
    }
    if (out_io) {
        *out_io = io;
    }
    if (out_len) {
        *out_len = jlen;
    }
    if (out_summary && sum_cap > 0) {
        const char* s = strstr(line, "\"summary\":\"");
        if (s) {
            s += 11;
            size_t i = 0;
            while (s[i] && s[i] != '"' && i + 1 < sum_cap) {
                out_summary[i] = s[i];
                i++;
            }
            out_summary[i] = '\0';
        } else {
            out_summary[0] = '\0';
        }
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    setenv("DSD_NEO_PDU_JSON", "1", 1);
    dsd_neo_config_init(NULL);

    dsd_test_capture_stderr cap;
    if (dsd_test_capture_stderr_begin(&cap, "p25_p1_pdu_json") != 0) {
        fprintf(stderr, "Failed to capture stderr: %s\n", strerror(errno));
        return 101;
    }

    // Case 1: SAP 32 RegAuth
    {
        uint8_t pdu[64];
        memset(pdu, 0, sizeof(pdu));
        // fmt/io (bit1), sap, mfid, llid
        pdu[0] = 0x10; // fmt=16, io=0
        pdu[1] = 32;   // SAP 32
        pdu[2] = 0xAA; // MFID
        pdu[3] = 0x00;
        pdu[4] = 0x01;
        pdu[5] = 0x02; // LLID
        pdu[6] = 0x03; // blks
        pdu[7] = 0x00; // pad
        pdu[9] = 0x00; // offset
        // payload at index 12
        pdu[12] = 0x42;
        pdu[13] = 0x11;
        pdu[14] = 0x22;
        pdu[15] = 0x33;
        pdu[16] = 0x44;             // 5 bytes
        int total_len = 12 + 5 + 4; // header + payload + CRC
        p25_test_p1_pdu_data_decode(pdu, total_len);
    }

    // Case 2: SAP 34 SysCfg
    {
        uint8_t pdu[64];
        memset(pdu, 0, sizeof(pdu));
        pdu[0] = 0x12; // fmt=18, io=1
        pdu[1] = 34;   // SAP 34
        pdu[2] = 0x55; // MFID
        pdu[3] = 0x00;
        pdu[4] = 0x00;
        pdu[5] = 0x10; // LLID
        pdu[6] = 0x02; // blks
        pdu[7] = 0x00; // pad
        pdu[9] = 0x00; // offset
        pdu[12] = 0x07;
        pdu[13] = 0x66;
        pdu[14] = 0x77;             // 3 bytes
        int total_len = 12 + 3 + 4; // header + payload + CRC
        p25_test_p1_pdu_data_decode(pdu, total_len);
    }

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

    // Parse last (SysCfg)
    int sap = -1, mfid = -1, io = -1, jlen = -1;
    char summary[128];
    int er = parse_last_json(buf, (int)nread, &sap, &mfid, &io, &jlen, summary, sizeof(summary));
    free(buf);
    if (er != 0) {
        fprintf(stderr, "parse_last_json er=%d\n", er);
        return 103;
    }
    rc |= expect_eq_int("SysCfg sap", sap, 34);
    rc |= expect_eq_int("SysCfg mfid", mfid, 0x55);
    rc |= expect_eq_int("SysCfg io", io, 1);
    rc |= expect_eq_int("SysCfg len", jlen, 3);
    rc |= expect_str_contains("SysCfg summary", summary, "SysCfg");

    (void)remove(cap.path);
    return rc;
}
