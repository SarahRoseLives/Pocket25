// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Bit/byte packing helpers shared across modules.
 *
 * Declares conversion utilities implemented in core.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse a user-supplied hex string into an octet buffer.
 *
 * Converts ASCII hex into bytes with bounds checking; excess characters are
 * ignored once @p out_cap is reached.
 *
 * @param input  Null-terminated hex string.
 * @param output Destination buffer for parsed bytes.
 * @param out_cap Capacity of @p output in bytes.
 * @return Number of bytes written (<= out_cap).
 */
uint16_t parse_raw_user_string(char* input, uint8_t* output, size_t out_cap);

/** @brief Convert a packed bit array into an integer output value (MSB-first). */
uint64_t convert_bits_into_output(uint8_t* input, int len);
/** @brief Pack an array of bits into bytes (length multiple of 8). */
void pack_bit_array_into_byte_array(uint8_t* input, uint8_t* output, int len);
/** @brief Pack bits into bytes when length is not a multiple of 8. */
void pack_bit_array_into_byte_array_asym(uint8_t* input, uint8_t* output, int len);
/** @brief Unpack a byte array into individual bits (MSB-first per octet). */
void unpack_byte_array_into_bit_array(uint8_t* input, uint8_t* output, int len);

/** @brief Pack AMBE bits into a contiguous byte buffer. */
void pack_ambe(char* input, uint8_t* output, int len);
/** @brief Unpack AMBE bytes back into bit form. */
void unpack_ambe(uint8_t* input, char* ambe);

#ifdef __cplusplus
}
#endif
