// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/fec/block_codes.h>
#include <dsd-neo/fec/bptc.h>
#include <dsd-neo/fec/rs_12_9.h>

static void
set_bits_from_u32_u8(uint8_t* dst_bits, int nbits, unsigned int v) {
    for (int i = 0; i < nbits; i++) {
        dst_bits[i] = (uint8_t)((v >> i) & 1U);
    }
}

static void
compute_even_parity_row(uint8_t mat[8][16]) {
    for (int col = 0; col < 16; col++) {
        int sum = 0;
        for (int row = 0; row < 7; row++) {
            sum += mat[row][col] & 1U;
        }
        mat[7][col] = (uint8_t)(sum & 1U);
    }
}

static int
test_bptc_128x77(void) {
    InitAllFecFunction();
    // Build a valid 8x16 matrix (7 data rows with Hamming(16,11,4), last row parity of columns)
    uint8_t mat[8][16] = {0};
    uint8_t enc[16];

    // Message layout per extractor:
    // rows 0..1: 11 bits each; rows 2..6: 10 bits each; CRC bits at mat[i][10] for i=2..6
    uint8_t data_bits72[72];
    uint8_t crc_bits5[5] = {0, 0, 0, 0, 0}; // choose all-zero CRC placeholder
    for (int i = 0; i < 72; i++) {
        data_bits72[i] = (uint8_t)((0xA5 >> (i % 8)) & 1U);
    }

    int k = 0;
    for (int row = 0; row < 7; row++) {
        uint8_t orig[11];
        memset(orig, 0, sizeof(orig));
        if (row < 2) {
            for (int j = 0; j < 11; j++, k++) {
                orig[j] = data_bits72[k];
            }
        } else {
            // 10 data + 1 CRC placeholder (use 0 for CRC bit in orig[10])
            for (int j = 0; j < 10; j++, k++) {
                orig[j] = data_bits72[k];
            }
            // Place chosen CRC bit in orig[10] so encoding carries it
            orig[10] = crc_bits5[row - 2];
        }
        Hamming_16_11_4_encode(orig, enc);
        for (int col = 0; col < 16; col++) {
            mat[row][col] = enc[col] & 1U;
        }
    }

    compute_even_parity_row(mat);

    // Call extractor
    uint8_t extracted[77] = {0};
    uint32_t irr = BPTC_128x77_Extract_Data(mat, extracted);
    assert(irr == 0);

    // Verify mapping of data part (first 72 bits) as implemented in extractor
    int idx = 0;
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 11; col++, idx++) {
            assert(extracted[idx] == (mat[row][col] & 1U));
        }
    }
    for (int row = 2; row < 7; row++) {
        for (int col = 0; col < 10; col++, idx++) {
            assert(extracted[idx] == (mat[row][col] & 1U));
        }
    }
    // Last 5 bits are column 10 of rows 2..6 and equal chosen crc_bits5
    for (int row = 2; row < 7; row++, idx++) {
        assert(extracted[idx] == crc_bits5[row - 2]);
    }

    // Additionally ensure extracted data equals our intended data_bits72
    for (int i = 0; i < 72; i++) {
        assert(extracted[i] == (data_bits72[i] & 1U));
    }

    // Inject a single-bit error in a data row and ensure correction path yields no irrecoverables
    uint8_t mat_err[8][16];
    memcpy(mat_err, mat, sizeof(mat_err));
    mat_err[1][3] ^= 1U; // single flip in row 1
    irr = BPTC_128x77_Extract_Data(mat_err, extracted);
    assert(irr == 0);

    return 0;
}

static int
test_bptc_16x2(void) {
    InitAllFecFunction();

    // Build a 32-bit vector with first 16 = valid Hamming(16,11,4) codeword
    uint8_t info[11];
    set_bits_from_u32_u8(info, 11, 0x3AB);
    uint8_t enc16[16];
    Hamming_16_11_4_encode(info, enc16);

    // Build DataMatrix (deinterleaved logical order)
    uint8_t dmat[32] = {0};
    for (int i = 0; i < 16; i++) {
        dmat[i] = enc16[i] & 1U;
    }

    // Case 1: odd parity (second half is complement)
    for (int i = 0; i < 16; i++) {
        dmat[16 + i] = dmat[i] ^ 1U;
    }

    // Build InputInterleavedData by inverting placement applied in BPTC_16x2_Extract_Data
    uint8_t interleaved[32];
    for (int i = 0; i < 32; i++) {
        interleaved[i] = dmat[DeInterleaveReverseChannelBptcPlacement[DeInterleaveReverseChannelBptc[i]]];
    }

    uint8_t outbits[32];
    uint32_t irr = BPTC_16x2_Extract_Data(interleaved, outbits, 1 /* odd */);
    assert(irr == 0);
    for (int i = 0; i < 11; i++) {
        assert(outbits[i] == (info[i] & 1U));
    }

    // Case 2: even parity (second half equals first half)
    for (int i = 0; i < 16; i++) {
        dmat[16 + i] = dmat[i];
    }
    for (int i = 0; i < 32; i++) {
        interleaved[i] = dmat[DeInterleaveReverseChannelBptcPlacement[DeInterleaveReverseChannelBptc[i]]];
    }
    irr = BPTC_16x2_Extract_Data(interleaved, outbits, 0 /* even */);
    assert(irr == 0);
    for (int i = 0; i < 11; i++) {
        assert(outbits[i] == (info[i] & 1U));
    }

    return 0;
}

static int
test_bptc_196x96_deinterleave(void) {
    // Simple mapping property test
    uint8_t in[196], out[196];
    for (int i = 0; i < 196; i++) {
        in[i] = (uint8_t)((i * 37) & 1U);
    }
    BPTCDeInterleaveDMRData(in, out);
    for (int i = 0; i < 196; i++) {
        uint32_t j = BPTCDeInterleavingIndex[i];
        assert(out[j] == (in[i] & 1U));
    }
    return 0;
}

static int
test_rs_12_9(void) {
    // Build a codeword = 9 data + 3 checksum
    rs_12_9_codeword_t cw = {0};
    for (int i = 0; i < RS_12_9_DATASIZE; i++) {
        cw.data[i] = (uint8_t)(i * 17 + 3);
    }
    rs_12_9_checksum_t* cks = rs_12_9_calc_checksum(&cw);
    cw.data[9] = cks->bytes[0];
    cw.data[10] = cks->bytes[1];
    cw.data[11] = cks->bytes[2];

    rs_12_9_poly_t syn = {0};
    rs_12_9_calc_syndrome(&cw, &syn);
    assert(rs_12_9_check_syndrome(&syn) == 0);

    // Single erroneous byte -> corrected
    cw.data[2] ^= 0x55;
    rs_12_9_calc_syndrome(&cw, &syn);
    assert(rs_12_9_check_syndrome(&syn) == 1);
    uint8_t fixed = 0;
    rs_12_9_correct_errors_result_t rc = rs_12_9_correct_errors(&cw, &syn, &fixed);
    assert(rc == RS_12_9_CORRECT_ERRORS_RESULT_ERRORS_CORRECTED);

    // Two erroneous bytes -> uncorrectable
    cw.data[1] ^= 0x22;
    cw.data[7] ^= 0x11;
    rs_12_9_calc_syndrome(&cw, &syn);
    assert(rs_12_9_check_syndrome(&syn) == 1);
    rc = rs_12_9_correct_errors(&cw, &syn, &fixed);
    assert(rc == RS_12_9_CORRECT_ERRORS_RESULT_ERRORS_CANT_BE_CORRECTED);

    return 0;
}

int
main(void) {
    if (test_bptc_128x77() != 0) {
        return 1;
    }
    if (test_bptc_16x2() != 0) {
        return 1;
    }
    if (test_bptc_196x96_deinterleave() != 0) {
        return 1;
    }
    if (test_rs_12_9() != 0) {
        return 1;
    }
    printf("FEC BPTC+RS tests passed.\n");
    return 0;
}
