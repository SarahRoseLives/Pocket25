// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Verify MAC VPDU length inference from MCO for unknown opcode and capacity capping.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

#define setenv dsd_test_setenv

// Forward declare config init to keep test dependencies narrow.
struct dsd_opts;
void dsd_neo_config_init(const struct dsd_opts* opts);

// Test shim entrypoint (provided by dsd-neo_proto_p25)
void p25_test_process_mac_vpdu(int type, const unsigned char* mac_bytes, int mac_len);

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
parse_len_fields(const char* s, int* lenB, int* lenC) {
    const char* p = strstr(s, "\"lenB\":");
    const char* q = strstr(s, "\"lenC\":");
    if (!p || !q) {
        return -1;
    }
    *lenB = atoi(p + 7);
    *lenC = atoi(q + 7);
    return 0;
}

static int
run_case(int type, uint8_t opcode, int expectB, int expectC) {
    // Ensure JSON output is enabled
    setenv("DSD_NEO_PDU_JSON", "1", 1);
    dsd_neo_config_init(NULL);

    dsd_test_capture_stderr cap;
    if (dsd_test_capture_stderr_begin(&cap, "p25_mac_segment") != 0) {
        fprintf(stderr, "Failed to capture stderr: %s\n", strerror(errno));
        return 100;
    }

    unsigned char mac[24];
    memset(mac, 0, sizeof(mac));
    mac[0] = 1;      // mark header present so MCO heuristic applies on FACCH
    mac[1] = opcode; // opcode with low 6 bits interpreted as MCO
    p25_test_process_mac_vpdu(type, mac, 24);

    dsd_test_capture_stderr_end(&cap);

    // Read file back
    FILE* f = fopen(cap.path, "r");
    if (!f) {
        fprintf(stderr, "Failed to open %s\n", cap.path);
        return 101;
    }
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    int lenB = -1, lenC = -1;
    if (parse_len_fields(buf, &lenB, &lenC) != 0) {
        fprintf(stderr, "JSON parse failed: %s\n", buf);
        return 102;
    }
    if (lenB != expectB) {
        fprintf(stderr, "lenB mismatch type=%d op=%02X got B=%d want B=%d (C=%d)\n", type, opcode, lenB, expectB, lenC);
        return 103;
    }
    if (lenC != expectC) {
        fprintf(stderr, "lenC mismatch type=%d op=%02X got C=%d want C=%d\n", type, opcode, lenC, expectC);
        return 104;
    }
    (void)remove(cap.path);
    return 0;
}

int
main(void) {
    // FACCH capacity = 16 octets (after opcode). Choose opcode 0x23 (base table 0), MCO=35 → infer 34 → cap 16.
    int rc = run_case(/*FACCH*/ 0, 0x23, /*B*/ 16, /*C*/ 0);
    if (rc != 0) {
        return rc;
    }
    fprintf(stderr, "P25p2 MAC MCO->length inference (FACCH) passed\n");
    return 0;
}
