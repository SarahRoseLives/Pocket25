// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief DES/3DES keystream helpers.
 *
 * Declares the DES and Triple-DES keystream generators implemented in
 * `src/crypto/crypt-des.c`.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void des_multi_keystream_output(unsigned long long int mi, unsigned long long int key_ulli, uint8_t* output, int type,
                                int len);
void tdea_multi_keystream_output(unsigned long long int mi, uint8_t* key, uint8_t* output, int type, int len);

#ifdef __cplusplus
}
#endif
