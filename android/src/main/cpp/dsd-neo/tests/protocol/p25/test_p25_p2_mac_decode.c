// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Verify P25 Phase 2 MAC VPDU length derivation and MCO fallback via JSON.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declare minimal types to avoid heavy deps
typedef struct dsd_opts dsd_opts;
typedef struct dsd_state dsd_state;

// Runtime config (JSON enable)
typedef struct dsdneoRuntimeConfig dsdneoRuntimeConfig;
void dsd_neo_config_init(const dsd_opts* opts);
const dsdneoRuntimeConfig* dsd_neo_get_config(void);

// Test shim: calls process_MAC_VPDU and emits JSON when enabled
void p25_test_process_mac_vpdu(int type, const unsigned char* mac_bytes, int mac_len);

// Stubs required by MAC VPDU path (alias decode helpers)
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
extract_last_fields_from_json(const char* buf, int len, int* out_b, int* out_c, int* out_slot, char* out_xch,
                              size_t xch_cap) {
    if (!buf || len <= 0) {
        return -1;
    }
    // Find start of last line
    const char* last_brace = strrchr(buf, '{');
    if (!last_brace) {
        return -1;
    }
    const char* line = last_brace;

    int b = -1, c = -1, s = -1;
    const char* p;

    p = strstr(line, "\"lenB\":");
    if (!p || sscanf(p, "\"lenB\":%d", &b) != 1) {
        return -2;
    }
    p = strstr(line, "\"lenC\":");
    if (!p || sscanf(p, "\"lenC\":%d", &c) != 1) {
        return -3;
    }
    p = strstr(line, "\"slot\":");
    if (p) {
        p += 7; // skip "slot":
        s = (int)strtol(p, NULL, 10);
    }

    *out_b = b;
    *out_c = c;
    if (out_slot) {
        *out_slot = s;
    }

    if (out_xch && xch_cap > 0) {
        const char* xs = strstr(line, "\"xch\":\"");
        if (xs) {
            xs += 7;
            size_t i = 0;
            while (xs[i] && xs[i] != '"' && i + 1 < xch_cap) {
                out_xch[i] = xs[i];
                i++;
            }
            out_xch[i] = '\0';
        } else {
            out_xch[0] = '\0';
        }
    }
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

int
main(void) {
    int rc = 0;

    // Enable MAC JSON emission
    setenv("DSD_NEO_PDU_JSON", "1", 1);
    dsd_neo_config_init(NULL);

    // Redirect stderr to a temporary file to capture JSON
    char tmpl[] = "/tmp/p25_mac_json_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        fprintf(stderr, "mkstemp failed: %s\n", strerror(errno));
        return 100;
    }
    FILE* fp = fdopen(fd, "w+");
    if (!fp) {
        fprintf(stderr, "fdopen failed: %s\n", strerror(errno));
        return 101;
    }
    if (!freopen(tmpl, "w+", stderr)) {
        fprintf(stderr, "freopen stderr failed\n");
        fclose(fp);
        return 102;
    } else {
        /* close the original stream associated with fd */
        fclose(fp);
    }

    // Case 1: FACCH, unknown opcode → derive from MCO; expect lenB=9 (mco=10), lenC=(16-9)=7
    {
        unsigned char mac[24];
        memset(mac, 0, sizeof(mac));
        mac[0] = 1;     // header-present hint for FACCH MCO fallback
        mac[1] = 10;    // opcode byte with MCO=10 (low 6 bits)
        mac[2] = 0x00;  // MFID (standard)
        mac[10] = 0xFF; // second message opcode unknown → lenC fallback to remaining capacity
        p25_test_process_mac_vpdu(0 /*FACCH*/, mac, 24);
    }

    // Case 2: SACCH, unknown opcode, MCO=15 → lenB=14, lenC=(19-14)=5 (no header hint needed)
    {
        unsigned char mac[24];
        memset(mac, 0, sizeof(mac));
        mac[0] = 0;     // SACCH path allows MCO use without header hint
        mac[1] = 15;    // MCO=15
        mac[2] = 0x00;  // MFID (standard)
        mac[15] = 0xFF; // second message opcode unknown
        p25_test_process_mac_vpdu(1 /*SACCH*/, mac, 24);
    }

    // Read back JSON and verify the last record (SACCH case)
    fflush(stderr);
    FILE* rf = fopen(tmpl, "rb");
    if (!rf) {
        fprintf(stderr, "fopen read failed\n");
        return 103;
    }
    fseek(rf, 0, SEEK_END);
    long sz = ftell(rf);
    if (sz < 0) {
        fprintf(stderr, "ftell failed\n");
        fclose(rf);
        return 104;
    }
    fseek(rf, 0, SEEK_SET);
    size_t alloc = (size_t)sz + 1;
    char* buf = (char*)calloc(alloc, 1);
    if (!buf) {
        fprintf(stderr, "malloc failed\n");
        fclose(rf);
        return 104;
    }
    size_t nread = fread(buf, 1, alloc - 1, rf);
    if (nread >= alloc) {
        nread = alloc - 1;
    }
    fclose(rf);

    int lenB = -1, lenC = -1, slot = -1;
    char xch[8] = {0};
    int er = extract_last_fields_from_json(buf, (int)nread, &lenB, &lenC, &slot, xch, sizeof(xch));
    free(buf);
    if (er != 0) {
        fprintf(stderr, "failed to parse JSON len fields (er=%d)\n", er);
        return 105;
    }

    // Last record corresponds to SACCH case
    rc |= expect_eq_int("SACCH lenB", lenB, 14);
    rc |= expect_eq_int("SACCH lenC", lenC, 5);
    rc |= expect_eq_int("SACCH slot flip", slot, 1);
    if (strcmp(xch, "SACCH") != 0) {
        fprintf(stderr, "xch: got %s want SACCH\n", xch);
        rc |= 1;
    }

    return rc;
}
