// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * M17 LSF parsing helpers.
 */

#include <string.h>

#include <dsd-neo/protocol/dmr/dmr_utils_api.h>
#include <dsd-neo/protocol/m17/m17_parse.h>
#include <dsd-neo/protocol/m17/m17_tables.h>

int
m17_parse_lsf(const uint8_t* lsf_bits, size_t bit_len, struct m17_lsf_result* out) {
    if (out == NULL || lsf_bits == NULL) {
        return -1;
    }
    if (bit_len < 240) {
        return -2;
    }

    memset(out, 0, sizeof(*out));

    unsigned long long lsf_dst = (unsigned long long)ConvertBitIntoBytes(
        (uint8_t*)&lsf_bits[0], 48); // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
    unsigned long long lsf_src = (unsigned long long)ConvertBitIntoBytes((uint8_t*)&lsf_bits[48], 48); // NOLINT
    uint16_t lsf_type = (uint16_t)ConvertBitIntoBytes((uint8_t*)&lsf_bits[96], 16);

    out->dst = lsf_dst;
    out->src = lsf_src;

    out->dt = (uint8_t)((lsf_type >> 1) & 0x3);
    out->et = (uint8_t)((lsf_type >> 3) & 0x3);
    out->es = (uint8_t)((lsf_type >> 5) & 0x3);
    out->cn = (uint8_t)((lsf_type >> 7) & 0xF);
    out->rs = (uint8_t)((lsf_type >> 11) & 0x1F);

    /* Decode base-40 CSD strings for destination and source. */
    memset(out->dst_csd, 0, sizeof(out->dst_csd));
    memset(out->src_csd, 0, sizeof(out->src_csd));

    if (lsf_dst != 0 && lsf_dst != 0xFFFFFFFFFFFFULL && lsf_dst < 0xEE6B28000000ULL) {
        for (int i = 0; i < 9; i++) {
            if (lsf_dst == 0) {
                break;
            }
            int idx = (int)(lsf_dst % 40ULL);
            if (idx < 0) {
                break;
            }
            out->dst_csd[i] = b40[idx];
            lsf_dst /= 40ULL;
        }
    }

    if (lsf_src != 0 && lsf_src != 0xFFFFFFFFFFFFULL && lsf_src < 0xEE6B28000000ULL) {
        for (int i = 0; i < 9; i++) {
            if (lsf_src == 0) {
                break;
            }
            int idx = (int)(lsf_src % 40ULL);
            if (idx < 0) {
                break;
            }
            out->src_csd[i] = b40[idx];
            lsf_src /= 40ULL;
        }
    }

    /* Extract Meta/IV bytes starting at bit 112 (14 octets). */
    uint8_t meta[14];
    memset(meta, 0, sizeof(meta));
    uint32_t meta_sum = 0;
    for (int i = 0; i < 14; i++) {
        meta[i] = (uint8_t)ConvertBitIntoBytes((uint8_t*)&lsf_bits[((size_t)i * 8U) + 112U], 8U);
        meta_sum += meta[i];
    }

    if (meta_sum != 0U) {
        out->has_meta = 1U;
        memcpy(out->meta, meta, sizeof(meta));
    } else {
        out->has_meta = 0U;
    }

    return 0;
}
