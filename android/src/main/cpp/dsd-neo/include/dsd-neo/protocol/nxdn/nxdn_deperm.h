// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief NXDN deinterleave/depunc/CRC helper APIs.
 *
 * Declares the NXDN helpers implemented in `src/protocol/nxdn/nxdn_deperm.c`.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void nxdn_descramble(uint8_t dibits[], int len);

void nxdn_deperm_facch(dsd_opts* opts, dsd_state* state, uint8_t bits[144]);
void nxdn_deperm_facch_soft(dsd_opts* opts, dsd_state* state, uint8_t bits[144], const uint8_t reliab[144]);

void nxdn_deperm_sacch(dsd_opts* opts, dsd_state* state, uint8_t bits[60]);
void nxdn_deperm_sacch_soft(dsd_opts* opts, dsd_state* state, uint8_t bits[60], const uint8_t reliab[60]);

void nxdn_deperm_sacch2(dsd_opts* opts, dsd_state* state, uint8_t bits[60]);
void nxdn_deperm_sacch2_soft(dsd_opts* opts, dsd_state* state, uint8_t bits[60], const uint8_t reliab[60]);

void nxdn_deperm_pich_tch(dsd_opts* opts, dsd_state* state, uint8_t bits[144]);
void nxdn_deperm_pich_tch_soft(dsd_opts* opts, dsd_state* state, uint8_t bits[144], const uint8_t reliab[144]);

void nxdn_deperm_facch2_udch(dsd_opts* opts, dsd_state* state, uint8_t bits[348], uint8_t type);
void nxdn_deperm_facch2_udch_soft(dsd_opts* opts, dsd_state* state, uint8_t bits[348], const uint8_t reliab[348],
                                  uint8_t type);

void nxdn_deperm_facch3_udch2(dsd_opts* opts, dsd_state* state, uint8_t bits[288], uint8_t type);
void nxdn_deperm_facch3_udch2_soft(dsd_opts* opts, dsd_state* state, uint8_t bits[288], const uint8_t reliab[288],
                                   uint8_t type);

void nxdn_deperm_scch(dsd_opts* opts, dsd_state* state, uint8_t bits[60], uint8_t direction);
void nxdn_deperm_scch_soft(dsd_opts* opts, dsd_state* state, uint8_t bits[60], const uint8_t reliab[60],
                           uint8_t direction);

void nxdn_deperm_cac(dsd_opts* opts, dsd_state* state, uint8_t bits[300]);
void nxdn_deperm_cac_soft(dsd_opts* opts, dsd_state* state, uint8_t bits[300], const uint8_t reliab[300]);

void nxdn_message_type(dsd_opts* opts, dsd_state* state, uint8_t MessageType);

int load_i(const uint8_t val[], int len);
uint8_t crc6(const uint8_t buf[], int len);
uint16_t crc12f(const uint8_t buf[], int len);
uint16_t crc15(const uint8_t buf[], int len);
uint16_t crc16cac(const uint8_t buf[], int len);
uint8_t crc7_scch(uint8_t bits[], int len);

#ifdef __cplusplus
}
#endif
