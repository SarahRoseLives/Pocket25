// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief AES helper entrypoints.
 *
 * Declares the AES wrapper helpers implemented in `src/crypto/crypt-aes.c`.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Generate AES OFB keystream blocks for the given IV/key. */
void aes_ofb_keystream_output(uint8_t* iv, uint8_t* key, uint8_t* output, int type, int nblocks);
/** @brief Encrypt/decrypt payload in AES-ECB mode (byte-wise). */
void aes_ecb_bytewise_payload_crypt(uint8_t* input, uint8_t* key, uint8_t* output, int type, int de);
/** @brief Encrypt/decrypt payload in AES-CBC mode (byte-wise). */
void aes_cbc_bytewise_payload_crypt(uint8_t* iv, uint8_t* key, uint8_t* in, uint8_t* out, int type, int nblocks,
                                    int de);
/** @brief Encrypt/decrypt payload in AES-CFB mode (byte-wise). */
void aes_cfb_bytewise_payload_crypt(uint8_t* iv, uint8_t* key, uint8_t* in, uint8_t* out, int type, int nblocks,
                                    int de);
/** @brief Encrypt/decrypt payload in AES-CTR mode (byte-wise counter). */
void aes_ctr_bytewise_payload_crypt(uint8_t* iv, uint8_t* key, uint8_t* payload, int type);
/** @brief Encrypt/decrypt payload in AES-CTR mode (bit-wise counter). */
void aes_ctr_bitwise_payload_crypt(uint8_t* iv, uint8_t* key, uint8_t* payload, int type);

#ifdef __cplusplus
}
#endif
