// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Focused checks for M17 LSF parsing helpers.
 *
 * Builds a synthetic LSF bit buffer with known dst/src IDs, type
 * fields, and META bytes and verifies m17_parse_lsf() decodes them
 * into the expected m17_lsf_result.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/m17/m17_parse.h>

// Minimal MSB-first bit-to-integer helper matching the DMR utils API.
uint64_t
ConvertBitIntoBytes(uint8_t* bits, uint32_t n) {
    uint64_t v = 0ULL;
    for (uint32_t i = 0; i < n; i++) {
        v = (v << 1) | (uint64_t)(bits[i] & 1U);
    }
    return v;
}

static void
write_bits_from_u64(uint8_t* dst, uint64_t value, uint32_t nbits) {
    for (uint32_t i = 0; i < nbits; i++) {
        uint32_t shift = nbits - 1U - i;
        dst[i] = (uint8_t)((value >> shift) & 1U);
    }
}

static int
expect_eq_u64(const char* tag, uint64_t got, uint64_t want) {
    if (got != want) {
        fprintf(stderr, "%s: got %llu want %llu\n", tag, (unsigned long long)got, (unsigned long long)want);
        return 1;
    }
    return 0;
}

static int
expect_eq_u8(const char* tag, uint8_t got, uint8_t want) {
    if (got != want) {
        fprintf(stderr, "%s: got %u want %u\n", tag, (unsigned)got, (unsigned)want);
        return 1;
    }
    return 0;
}

int
main(void) {
    uint8_t lsf_bits[240];
    memset(lsf_bits, 0, sizeof(lsf_bits));

    // Choose arbitrary but distinct dst/src values within the valid range.
    const uint64_t dst = 0x0000ABCDEF12ULL;
    const uint64_t src = 0x000012345678ULL;

    // Type word fields (packed into lsf_type as in m17_parse_lsf).
    const uint8_t dt = 2U;
    const uint8_t et = 1U;
    const uint8_t es = 3U;
    const uint8_t cn = 9U;
    const uint8_t rs = 18U;

    uint16_t lsf_type = 0;
    lsf_type |= (uint16_t)((uint16_t)dt << 1);
    lsf_type |= (uint16_t)((uint16_t)et << 3);
    lsf_type |= (uint16_t)((uint16_t)es << 5);
    lsf_type |= (uint16_t)((uint16_t)cn << 7);
    lsf_type |= (uint16_t)((uint16_t)rs << 11);

    // META/IV bytes: make the first byte non-zero so has_meta=1.
    uint8_t meta[14];
    memset(meta, 0, sizeof(meta));
    meta[0] = 0x42U;
    meta[1] = 0x99U;

    // Layout matches m17_parse_lsf expectations:
    //  - bits 0..47   : dst (48 bits)
    //  - bits 48..95  : src (48 bits)
    //  - bits 96..111 : type word (16 bits)
    //  - bits 112..223: META (14 octets)

    write_bits_from_u64(&lsf_bits[0], dst, 48U);
    write_bits_from_u64(&lsf_bits[48], src, 48U);
    write_bits_from_u64(&lsf_bits[96], (uint64_t)lsf_type, 16U);

    for (int i = 0; i < 14; i++) {
        write_bits_from_u64(&lsf_bits[112 + (i * 8)], meta[i], 8U);
    }

    struct m17_lsf_result res;
    int rc = m17_parse_lsf(lsf_bits, sizeof(lsf_bits), &res);
    if (rc != 0) {
        fprintf(stderr, "m17_parse_lsf failed: rc=%d\n", rc);
        return 1;
    }

    int err = 0;
    err |= expect_eq_u64("dst", res.dst, dst);
    err |= expect_eq_u64("src", res.src, src);
    err |= expect_eq_u8("dt", res.dt, dt);
    err |= expect_eq_u8("et", res.et, et);
    err |= expect_eq_u8("es", res.es, es);
    err |= expect_eq_u8("cn", res.cn, cn);
    err |= expect_eq_u8("rs", res.rs, rs);
    err |= expect_eq_u8("has_meta", res.has_meta, 1U);

    if (res.meta[0] != meta[0] || res.meta[1] != meta[1]) {
        fprintf(stderr, "meta[0..1]: got %02X %02X want %02X %02X\n", res.meta[0], res.meta[1], meta[0], meta[1]);
        err |= 1;
    }

    if (err == 0) {
        printf("M17_LSF_PARSE: OK\n");
    }
    return err;
}
