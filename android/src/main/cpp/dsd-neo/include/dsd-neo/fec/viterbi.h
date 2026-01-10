// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Viterbi decoder helpers.
 *
 * Declares the Viterbi routines implemented in `src/core/util/dsd_misc.c`.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t viterbi_decode(uint8_t* out, const uint16_t* in, const uint16_t len);
uint32_t viterbi_decode_punctured(uint8_t* out, const uint16_t* in, const uint8_t* punct, const uint16_t in_len,
                                  const uint16_t p_len);
void viterbi_decode_bit(uint16_t s0, uint16_t s1, const size_t pos);
uint32_t viterbi_chainback(uint8_t* out, size_t pos, uint16_t len);
void viterbi_reset(void);
uint16_t q_abs_diff(const uint16_t v1, const uint16_t v2);

#ifdef __cplusplus
}
#endif
