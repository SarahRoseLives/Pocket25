// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#ifndef DSD_NEO_PROTOCOL_DSTAR_HEADER_UTILS_H
#define DSD_NEO_PROTOCOL_DSTAR_HEADER_UTILS_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file
 * @brief D-STAR header scrambling, interleaving, and CRC helpers.
 */

// D-STAR radio header parameters (fixed by the spec).
#define DSD_DSTAR_HEADER_INFO_BITS  330
#define DSD_DSTAR_HEADER_CODED_BITS 660
#define DSD_DSTAR_SCRAMBLER_PERIOD  127

/** @brief Scramble header bits using the fixed D-STAR PN sequence. */
void dstar_scramble_header_bits(const int* in, int* out, size_t bit_count);
/** @brief Scramble soft costs with PN sequence (XOR inverts the soft cost). */
void dstar_scramble_soft_costs(const uint16_t* in, uint16_t* out, size_t bit_count);
/** @brief Deinterleave coded header bits (reverse of transmit interleaver). */
void dstar_deinterleave_header_bits(const int* in, int* out, size_t bit_count);
/** @brief Deinterleave soft costs (reverse of transmit interleaver). */
void dstar_deinterleave_soft_costs(const uint16_t* in, uint16_t* out, size_t bit_count);
/** @brief Viterbi-decode coded header symbols into bits; returns bits written. */
size_t dstar_header_viterbi_decode(const int* symbols, size_t symbol_count, int* out_bits, size_t out_capacity);
/** @brief Soft-decision Viterbi-decode using float soft costs; returns bits written. */
size_t dstar_header_viterbi_decode_soft(const uint16_t* soft_symbols, size_t symbol_count, int* out_bits,
                                        size_t out_capacity);
/** @brief Compute D-STAR CRC16 over header bytes. */
uint16_t dstar_crc16(const uint8_t* data, size_t len);

#endif // DSD_NEO_PROTOCOL_DSTAR_HEADER_UTILS_H
