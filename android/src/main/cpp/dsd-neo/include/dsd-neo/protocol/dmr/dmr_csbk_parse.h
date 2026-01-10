// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief CSBK parsing helpers shared across DMR control code.
 */
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * DMR CSBK parsing and dispatch helpers.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dmr_csbk_result {
    uint8_t lb;
    uint8_t pf;
    uint8_t opcode;
    uint8_t fid;

    /* Channel grant-related fields (when applicable). */
    uint16_t lpcn;
    uint16_t pluschannum;
    uint8_t lcn;
    uint8_t st1;
    uint8_t st2;
    uint8_t st3;
    uint32_t target;
    uint32_t source;

    /* Optional resolved frequency in Hz when known (0 if unknown). */
    long freq_hz;

    const uint8_t* bits;
    const uint8_t* bytes;
};

/**
 * Decode basic CSBK header fields and, for channel-grant opcodes, the
 * common grant fields used by trunking logic.
 *
 * @return 0 on success, negative on error.
 */
int dmr_csbk_parse(const uint8_t* cs_pdu_bits, const uint8_t* cs_pdu, struct dmr_csbk_result* out);

/**
 * Central dispatch helper for CSBK trunking actions.
 *
 * Currently routes channel-grant CSBKs to the DMR Tier III trunking
 * state machine using the parsed fields in @p res.
 */
void dmr_csbk_handle(const struct dmr_csbk_result* res, dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
