// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief BPTC (block product turbo code) helpers for DMR payload extraction.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t BPTCInterleavingIndex[196];
extern const uint8_t BPTCDeInterleavingIndex[196];
extern const uint8_t DeInterleaveReverseChannelBptc[32];
extern const uint8_t DeInterleaveReverseChannelBptcPlacement[32];

void BPTCDeInterleaveDMRData(uint8_t* Input, uint8_t* Output);
uint32_t BPTC_196x96_Extract_Data(uint8_t InputDeInteleavedData[196], uint8_t DMRDataExtracted[96], uint8_t R[3]);
uint32_t BPTC_128x77_Extract_Data(uint8_t InputDataMatrix[8][16], uint8_t DMRDataExtracted[77]);
uint32_t BPTC_16x2_Extract_Data(uint8_t InputInterleavedData[32], uint8_t DMRDataExtracted[32],
                                uint32_t ParityCheckTypeOdd);

#ifdef __cplusplus
}
#endif
