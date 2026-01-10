// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Minimal DMR utility CRC/bit helper API used by tests.
 */

/** @brief Pack a bit buffer into bytes (little-endian bits). */
uint64_t ConvertBitIntoBytes(uint8_t* BufferIn, uint32_t BitLength);

/** @brief DMR SlotType Hamming(17,12,3) decode/correction helper. */
bool Hamming17123(uint8_t* d);

/** @brief Compute CCITT CRC-16 over DMR data buffer. */
uint16_t ComputeCrcCCITT(uint8_t* DMRData);
/** @brief Compute CCITT CRC-16 over an arbitrary bit buffer (bit array). */
uint16_t ComputeCrcCCITT16d(const uint8_t* buf, uint32_t len);
/** @brief Compute and optionally correct CRC for full link control block. */
uint32_t ComputeAndCorrectFullLinkControlCrc(uint8_t* FullLinkControlDataBytes, uint32_t* CRCComputed,
                                             uint32_t CRCMask);
/** @brief Compute 5-bit CRC used in certain DMR headers. */
uint8_t ComputeCrc5Bit(uint8_t* DMRData);
/** @brief Compute 9-bit CRC used by certain DMR payloads. */
uint16_t ComputeCrc9Bit(uint8_t* DMRData, uint32_t NbData);
/** @brief Compute CRC-32 over a DMR buffer. */
uint32_t ComputeCrc32Bit(uint8_t* DMRData, uint32_t NbData);

/** @brief Compute 3-bit CRC for provided bit array. */
uint8_t crc3(uint8_t bits[], unsigned int len);
/** @brief Compute 4-bit CRC for provided bit array. */
uint8_t crc4(uint8_t bits[], unsigned int len);
/** @brief Compute 7-bit CRC for provided bit array. */
uint8_t crc7(uint8_t bits[], unsigned int len);
/** @brief Compute 8-bit CRC for provided bit array. */
uint8_t crc8(uint8_t bits[], unsigned int len);
/** @brief Check 8-bit CRC over provided bit array. */
bool crc8_ok(uint8_t bits[], unsigned int len);

#ifdef __cplusplus
}
#endif
