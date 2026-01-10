// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25p2 VPDU sequence: ensure ordered handling of PTT -> ACTIVE -> END
 * across successive MAC PDUs and stable JSON emission per step.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

#define setenv dsd_test_setenv

typedef struct dsd_opts dsd_opts;
typedef struct dsd_state dsd_state;
typedef struct dsdneoRuntimeConfig dsdneoRuntimeConfig;

void dsd_neo_config_init(const dsd_opts* opts);
const dsdneoRuntimeConfig* dsd_neo_get_config(void);

// Test shim(s)
void p25_test_process_mac_vpdu_ex(int type, const unsigned char* mac_bytes, int mac_len, int is_lcch, int currentslot);

// Minimal stubs referenced by linked code paths
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
expect_true(const char* tag, int cond) {
    if (!cond) {
        fprintf(stderr, "%s: expected true\n", tag);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    setenv("DSD_NEO_PDU_JSON", "1", 1);
    dsd_neo_config_init(NULL);

    dsd_test_capture_stderr cap;
    if (dsd_test_capture_stderr_begin(&cap, "p25_p2_vpdu_sequence") != 0) {
        fprintf(stderr, "Failed to capture stderr: %s\n", strerror(errno));
        return 101;
    }

    unsigned char mac[24];
    memset(mac, 0, sizeof mac);

    // Step 1: SACCH slot 0: PTT
    mac[1] = 0x01;
    mac[2] = 0x00;
    p25_test_process_mac_vpdu_ex(1, mac, 24, 0, 0);
    // Step 2: FACCH slot 0: ACTIVE
    memset(mac, 0, sizeof mac);
    mac[0] = 1;
    mac[1] = 0x04;
    mac[2] = 0x00;
    p25_test_process_mac_vpdu_ex(0, mac, 24, 0, 0);
    // Step 3: SACCH slot 1: END
    memset(mac, 0, sizeof mac);
    mac[1] = 0x02;
    mac[2] = 0x00;
    p25_test_process_mac_vpdu_ex(1, mac, 24, 0, 1);

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
    if (!buf) {
        fclose(rf);
        return 103;
    }
    (void)fread(buf, 1, alloc - 1, rf);
    fclose(rf);

    // Order check: first occurrence indices must be increasing
    const char* s1 = strstr(buf, "\"op\":1");
    const char* s2 = strstr(buf, "\"op\":4");
    const char* s3 = strstr(buf, "\"op\":2");
    rc |= expect_true("PTT present", s1 != NULL);
    rc |= expect_true("ACTIVE present", s2 != NULL);
    rc |= expect_true("END present", s3 != NULL);
    if (s1 && s2) {
        rc |= expect_true("PTT before ACTIVE", s1 < s2);
    }
    if (s2 && s3) {
        rc |= expect_true("ACTIVE before END", s2 < s3);
    }
    free(buf);
    (void)remove(cap.path);
    return rc;
}
