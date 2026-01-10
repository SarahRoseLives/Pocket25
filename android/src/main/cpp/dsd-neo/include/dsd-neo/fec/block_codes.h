// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Legacy block code (Hamming/Golay/QR) helpers used by DMR/dPMR/NXDN paths.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Hamming_7_4_init(void);
void Hamming_7_4_encode(unsigned char* origBits, unsigned char* encodedBits);
bool Hamming_7_4_decode(unsigned char* rxBits);

void Hamming_12_8_init(void);
void Hamming_12_8_encode(unsigned char* origBits, unsigned char* encodedBits);
bool Hamming_12_8_decode(unsigned char* rxBits, unsigned char* decodedBits, int nbCodewords);

void Hamming_13_9_init(void);
void Hamming_13_9_encode(unsigned char* origBits, unsigned char* encodedBits);
bool Hamming_13_9_decode(unsigned char* rxBits, unsigned char* decodedBits, int nbCodewords);

void Hamming_15_11_init(void);
void Hamming_15_11_encode(unsigned char* origBits, unsigned char* encodedBits);
bool Hamming_15_11_decode(unsigned char* rxBits, unsigned char* decodedBits, int nbCodewords);

void Hamming_16_11_4_init(void);
void Hamming_16_11_4_encode(unsigned char* origBits, unsigned char* encodedBits);
bool Hamming_16_11_4_decode(unsigned char* rxBits, unsigned char* decodedBits, int nbCodewords);

void Golay_20_8_init(void);
void Golay_20_8_encode(unsigned char* origBits, unsigned char* encodedBits);
bool Golay_20_8_decode(unsigned char* rxBits);

void Golay_23_12_init(void);
void Golay_23_12_encode(unsigned char* origBits, unsigned char* encodedBits);
bool Golay_23_12_decode(unsigned char* rxBits);

void Golay_24_12_init(void);
void Golay_24_12_encode(unsigned char* origBits, unsigned char* encodedBits);
bool Golay_24_12_decode(unsigned char* rxBits);

void QR_16_7_6_init(void);
void QR_16_7_6_encode(unsigned char* origBits, unsigned char* encodedBits);
bool QR_16_7_6_decode(unsigned char* rxBits);

void InitAllFecFunction(void);

#ifdef __cplusplus
}
#endif
