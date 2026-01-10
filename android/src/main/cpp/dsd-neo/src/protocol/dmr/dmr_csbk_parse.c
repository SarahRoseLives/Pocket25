// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * DMR CSBK parsing and dispatch helpers.
 */

#include <string.h>

#include <dsd-neo/protocol/dmr/dmr_csbk_parse.h>
#include <dsd-neo/protocol/dmr/dmr_csbk_tables.h>
#include <dsd-neo/protocol/dmr/dmr_trunk_sm.h>
#include <dsd-neo/protocol/dmr/dmr_utils_api.h>

int
dmr_csbk_parse(const uint8_t* cs_pdu_bits, const uint8_t* cs_pdu, struct dmr_csbk_result* out) {
    if (out == NULL || cs_pdu_bits == NULL || cs_pdu == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->bits = cs_pdu_bits;
    out->bytes = cs_pdu;

    out->lb = (uint8_t)((cs_pdu[0] & 0x80U) >> 7);
    out->pf = (uint8_t)((cs_pdu[0] & 0x40U) >> 6);
    out->opcode = (uint8_t)(cs_pdu[0] & 0x3FU);
    out->fid = (uint8_t)cs_pdu[1];

    /* For channel grant CSBK/MBC PDUs (ETSI 7.1.1.1.1), decode common fields. */
    if (out->opcode >= 48 && out->opcode <= 56) {
        out->lpcn = (uint16_t)ConvertBitIntoBytes((uint8_t*)&cs_pdu_bits[16], 12U);
        out->pluschannum = (uint16_t)ConvertBitIntoBytes((uint8_t*)&cs_pdu_bits[16], 13U);
        out->pluschannum = (uint16_t)(out->pluschannum + 1U);
        out->lcn = cs_pdu_bits[28];
        out->st1 = cs_pdu_bits[29];
        out->st2 = cs_pdu_bits[30];
        out->st3 = cs_pdu_bits[31];
        out->target = (uint32_t)ConvertBitIntoBytes((uint8_t*)&cs_pdu_bits[32], 24U);
        out->source = (uint32_t)ConvertBitIntoBytes((uint8_t*)&cs_pdu_bits[56], 24U);
    }

    return 0;
}

void
dmr_csbk_handle(const struct dmr_csbk_result* res, dsd_opts* opts, dsd_state* state) {
    if (res == NULL || opts == NULL || state == NULL) {
        return;
    }

    /* Only channel grants are routed through the Tier III trunk SM here. */
    if (res->opcode >= 48 && res->opcode <= 56) {
        long freq = res->freq_hz;
        int lpcn = (int)res->lpcn;

        /* Voice vs data grant distinction is made on opcode. */
        if (res->opcode == 49) {
            dmr_sm_emit_group_grant(opts, state, freq, lpcn, (int)res->target, (int)res->source);
        } else {
            dmr_sm_emit_indiv_grant(opts, state, freq, lpcn, (int)res->target, (int)res->source);
        }
    }
}
