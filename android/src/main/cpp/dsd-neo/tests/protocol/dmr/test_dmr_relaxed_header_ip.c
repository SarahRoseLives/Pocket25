// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

// Smoke test: relaxed header acceptance for SAP=4 (IP-based) with CRC fail

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <dsd-neo/core/events.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/fec/rs_12_9.h>
#include <dsd-neo/runtime/unicode.h>

// Forward under test
extern void dmr_dheader(dsd_opts* opts, dsd_state* state, uint8_t dheader[], uint8_t dheader_bits[],
                        uint32_t CRCCorrect, uint32_t IrrecoverableErrors);

// Provide local stubs to avoid pulling full core/audio deps during link
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
watchdog_event_datacall(dsd_opts* opts, dsd_state* state, uint32_t src, uint32_t dst, char* data_string, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)src;
    (void)dst;
    (void)data_string;
    (void)slot;
}

void
lip_protocol_decoder(dsd_opts* opts, dsd_state* state, uint8_t* input) {
    (void)opts;
    (void)state;
    (void)input;
}

void
nmea_iec_61162_1(dsd_opts* opts, dsd_state* state, uint8_t* input, uint32_t src, int type) {
    (void)opts;
    (void)state;
    (void)input;
    (void)src;
    (void)type;
}

// Stubs to avoid linking runtime/core
int
dsd_unicode_supported(void) {
    return 0;
}

const char*
dsd_degrees_glyph(void) {
    return "";
}

// FEC RS(12,9) stubs used by dmr_utils.c
void
rs_12_9_calc_syndrome(rs_12_9_codeword_t* codeword, rs_12_9_poly_t* syndrome) {
    (void)codeword;
    (void)syndrome;
}

uint8_t
rs_12_9_check_syndrome(rs_12_9_poly_t* syndrome) {
    (void)syndrome;
    return 0;
}

rs_12_9_correct_errors_result_t
rs_12_9_correct_errors(rs_12_9_codeword_t* codeword, rs_12_9_poly_t* syndrome, uint8_t* errors_found) {
    (void)codeword;
    (void)syndrome;
    if (errors_found) {
        *errors_found = 0;
    }
    return RS_12_9_CORRECT_ERRORS_RESULT_NO_ERRORS_FOUND;
}

// Crypto and PDU helpers (stubs)
void
LFSR128d(dsd_state* state) {
    (void)state;
}

void
rc4_block_output(int drop, int keylen, int meslen, uint8_t* key, uint8_t* output_blocks) {
    (void)drop;
    (void)keylen;
    (void)meslen;
    (void)key;
    (void)output_blocks;
}

void
aes_ofb_keystream_output(uint8_t* iv, uint8_t* key, uint8_t* output, int type, int nblocks) {
    (void)iv;
    (void)key;
    (void)output;
    (void)type;
    (void)nblocks;
}

void
des_multi_keystream_output(unsigned long long int mi, unsigned long long int key_ulli, uint8_t* output, int type,
                           int nblocks) {
    (void)mi;
    (void)key_ulli;
    (void)output;
    (void)type;
    (void)nblocks;
}

void
decode_ip_pdu(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* input) {
    (void)opts;
    (void)state;
    (void)len;
    (void)input;
}

void
dmr_sd_pdu(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* DMR_PDU) {
    (void)opts;
    (void)state;
    (void)len;
    (void)DMR_PDU;
}

void
dmr_udp_comp_pdu(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* DMR_PDU) {
    (void)opts;
    (void)state;
    (void)len;
    (void)DMR_PDU;
}

void
dmr_lrrp(dsd_opts* opts, dsd_state* state, uint16_t len, uint32_t source, uint32_t dest, uint8_t* DMR_PDU,
         uint8_t pdu_crc_ok) {
    (void)opts;
    (void)state;
    (void)len;
    (void)source;
    (void)dest;
    (void)DMR_PDU;
    (void)pdu_crc_ok;
}

void
dmr_cspdu(dsd_opts* opts, dsd_state* state, uint8_t* bits, uint8_t* bytes, uint32_t CRCCorrect,
          uint32_t IrrecoverableErrors) {
    (void)opts;
    (void)state;
    (void)bits;
    (void)bytes;
    (void)CRCCorrect;
    (void)IrrecoverableErrors;
}

void
utf8_to_text(dsd_state* state, uint8_t wr, uint16_t len, uint8_t* input) {
    (void)state;
    (void)wr;
    (void)len;
    (void)input;
}

void
dmr_locn(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* DMR_PDU) {
    (void)opts;
    (void)state;
    (void)len;
    (void)DMR_PDU;
}

static void
set_bits(uint8_t* bits, int start, uint32_t value, int nbits) {
    for (int i = 0; i < nbits; i++) {
        int bit = (int)((value >> (nbits - 1 - i)) & 1U);
        bits[start + i] = (uint8_t)bit;
    }
}

int
main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    static dsd_opts opts;
    static dsd_state state;
    memset(&opts, 0, sizeof(opts));
    memset(&state, 0, sizeof(state));
    state.currentslot = 0;

    // Craft a minimal DMR Data Header bit array (MSB-first) with:
    // - DPF=2 (Unconfirmed Delivery)
    // - SAP=4 (IP Based)
    // - Non-zero Source/Target
    // We will call dmr_dheader with CRCCorrect=0 and IrrecoverableErrors=0.
    // Expect: strict mode rejects; relaxed mode accepts and stores SAP=4 and DPF=2.

    uint8_t dheader[12];
    uint8_t bits[196];
    memset(dheader, 0, sizeof(dheader));
    memset(bits, 0, sizeof(bits));

    // gi(0), a(0), ab(0)
    // mpoc at bit 3 -> 0
    // dpf at [4..7] = 2
    set_bits(bits, 4, 2U, 4);
    // sap at [8..11] = 4 (IP Based)
    set_bits(bits, 8, 4U, 4);
    // poc at [12..15] = 0

    // target [16..39] (24 bits) and source [40..63] (24 bits)
    set_bits(bits, 16, 0x000123U, 24);
    set_bits(bits, 40, 0x000456U, 24);

    // f at [64] = 0, bf [65..71] = 1 (non-zero blocks)
    set_bits(bits, 65, 1U, 7);

    // Strict (aggressive) mode: should NOT accept when CRC fails
    opts.aggressive_framesync = 1;
    uint8_t before_format = state.data_header_format[state.currentslot];
    uint8_t before_sap = state.data_header_sap[state.currentslot];
    dmr_dheader(&opts, &state, dheader, bits, /*CRCCorrect=*/0, /*IrrecoverableErrors=*/0);
    assert(state.data_header_format[state.currentslot] == before_format); // unchanged
    assert(state.data_header_sap[state.currentslot] == before_sap);

    // Relaxed mode: should accept header despite CRC failure
    memset(&state, 0, sizeof(state));
    state.currentslot = 0;
    opts.aggressive_framesync = 0;
    dmr_dheader(&opts, &state, dheader, bits, /*CRCCorrect=*/0, /*IrrecoverableErrors=*/0);

    assert(state.data_header_format[state.currentslot] == 2); // DPF accepted
    assert(state.data_header_sap[state.currentslot] == 4);    // SAP=4 stored
    assert(state.dmr_lrrp_target[state.currentslot] != 0);
    assert(state.dmr_lrrp_source[state.currentslot] != 0);

    return 0;
}
