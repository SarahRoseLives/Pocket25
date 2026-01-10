// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Focused P25p2 MAC VPDU tests to exercise additional opcode paths
 * and unknown-length fallback handling.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

#define setenv dsd_test_setenv

// Minimal forward types for config access
typedef struct dsd_opts dsd_opts;
typedef struct dsd_state dsd_state;
typedef struct dsdneoRuntimeConfig dsdneoRuntimeConfig;

// Runtime config
void dsd_neo_config_init(const dsd_opts* opts);
const dsdneoRuntimeConfig* dsd_neo_get_config(void);

// Test shims
void p25_test_process_mac_vpdu(int type, const unsigned char* mac_bytes, int mac_len);
void p25_test_process_mac_vpdu_ex(int type, const unsigned char* mac_bytes, int mac_len, int is_lcch, int currentslot);

// Stubs referenced by MAC VPDU path
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

// Rigctl/rtl stubs
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
run_cases(void) {
    // Case 1: SACCH, PTT opcode (0x01) with basic header → JSON emits summary "PTT"
    {
        unsigned char mac[24];
        memset(mac, 0, sizeof mac);
        mac[1] = 0x01; // PTT
        mac[2] = 0x00; // standard MFID
        p25_test_process_mac_vpdu(1 /*SACCH*/, mac, 24);
    }

    // Case 2: FACCH, IDLE opcode (0x03)
    {
        unsigned char mac[24];
        memset(mac, 0, sizeof mac);
        mac[0] = 1;    // header-present hint
        mac[1] = 0x03; // IDLE
        mac[2] = 0x00;
        p25_test_process_mac_vpdu(0 /*FACCH*/, mac, 24);
    }

    // Case 3: Unknown opcode with no header (no MCO) to trigger unknown-length path
    // Use opcode 0x07 (reserved), MFID 0x00; MAC[0]==0 (no header) so table=0, MCO skip → unknown length warning path.
    {
        unsigned char mac[24];
        memset(mac, 0, sizeof mac);
        mac[1] = 0x07; // reserved/unknown
        mac[2] = 0x00; // standard MFID
        p25_test_process_mac_vpdu_ex(0 /*FACCH*/, mac, 24, /*is_lcch*/ 0, /*slot*/ 0);
    }

    // Case 4: LCCH label with SIGNAL opcode to exercise LCCH gating inside VPDU
    {
        unsigned char mac[24];
        memset(mac, 0, sizeof mac);
        mac[1] = 0x00; // SIGNAL
        mac[2] = 0x00;
        p25_test_process_mac_vpdu_ex(0 /*FACCH*/, mac, 24, /*is_lcch*/ 1, /*slot*/ 1);
    }

    return 0;
}

int
main(void) {
    // Enable JSON emission to exercise emit paths
    setenv("DSD_NEO_PDU_JSON", "1", 1);
    dsd_neo_config_init(NULL);

    // Capture stderr to a temp file to avoid polluting test logs; we don't need to parse it here.
    dsd_test_capture_stderr cap;
    if (dsd_test_capture_stderr_begin(&cap, "p25_p2_vpdu_core") != 0) {
        fprintf(stderr, "Failed to capture stderr: %s\n", strerror(errno));
        return 101;
    }
    int rc = run_cases();
    dsd_test_capture_stderr_end(&cap);
    (void)remove(cap.path);
    (void)rc;
    return 0;
}
