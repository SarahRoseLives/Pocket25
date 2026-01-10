// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Reed-Solomon (12,9) helper types and APIs (DMR AI).
 */

#pragma once

#include <stdint.h>

// Reed Solomon (12,9) constants
#define RS_12_9_DATASIZE     9
#define RS_12_9_CHECKSUMSIZE 3

// Reed Solomon (12,9) codeword: 9 data + 3 checksum bytes
typedef struct {
    uint8_t data[RS_12_9_DATASIZE + RS_12_9_CHECKSUMSIZE];
} rs_12_9_codeword_t;

// Maximum degree of various polynomials.
#define RS_12_9_POLY_MAXDEG (RS_12_9_CHECKSUMSIZE * 2)

typedef struct {
    uint8_t data[RS_12_9_POLY_MAXDEG];
} rs_12_9_poly_t;

#define RS_12_9_CORRECT_ERRORS_RESULT_NO_ERRORS_FOUND          0
#define RS_12_9_CORRECT_ERRORS_RESULT_ERRORS_CORRECTED         1
#define RS_12_9_CORRECT_ERRORS_RESULT_ERRORS_CANT_BE_CORRECTED 2
typedef uint8_t rs_12_9_correct_errors_result_t;

typedef struct {
    uint8_t bytes[RS_12_9_CHECKSUMSIZE];
} rs_12_9_checksum_t;

#ifdef __cplusplus
extern "C" {
#endif

void rs_12_9_calc_syndrome(rs_12_9_codeword_t* codeword, rs_12_9_poly_t* syndrome);
uint8_t rs_12_9_check_syndrome(rs_12_9_poly_t* syndrome);
rs_12_9_correct_errors_result_t rs_12_9_correct_errors(rs_12_9_codeword_t* codeword, rs_12_9_poly_t* syndrome,
                                                       uint8_t* errors_found);
rs_12_9_checksum_t* rs_12_9_calc_checksum(rs_12_9_codeword_t* codeword);

#ifdef __cplusplus
}
#endif
