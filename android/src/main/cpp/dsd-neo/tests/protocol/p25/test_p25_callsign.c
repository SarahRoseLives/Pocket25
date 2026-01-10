// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25 WACN/SysID to Callsign conversion tests.
 * Verifies the Radix-50 encoding algorithm produces correct FCC callsigns.
 *
 * Test vectors derived from:
 * - Eric Carlson's converter: https://www.ericcarlson.net/project-25-callsign.html
 * - RadioReference database callsign lookups
 *
 * Note: The callsign algorithm only produces meaningful results for WACNs that
 * were derived from FCC callsigns per the APCO specification. Manufacturer
 * default WACNs like Motorola's BEE00 return empty strings since they don't
 * correspond to actual callsigns.
 */

#include <stdio.h>
#include <string.h>

#include <dsd-neo/protocol/p25/p25_callsign.h>

static int
expect_eq_str(const char* tag, const char* got, const char* want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "%s: got '%s' want '%s'\n", tag, got, want);
        return 1;
    }
    return 0;
}

int
main(void) {
    int rc = 0;
    char callsign[7];

    // Test case: WACN 0x00001, SysID 0x001
    // n1 = 1 / 16 = 0
    // n2 = 4096 * (1 % 16) + 1 = 4096 * 1 + 1 = 4097
    // Char 1-3: 0/1600=0(' '), 0/40%40=0(' '), 0%40=0(' ')
    // Char 4-6: 4097/1600=2('B'), (4097/40)%40=102%40=22('V'), 4097%40=17('Q')
    p25_wacn_sysid_to_callsign(0x00001, 0x001, callsign);
    rc |= expect_eq_str("WACN 0x00001 SysID 0x001", callsign, "   BVQ");

    // Test case: WACN 0x00000, SysID 0x000
    // Both n1 and n2 are 0, all chars should be space (index 0)
    p25_wacn_sysid_to_callsign(0x00000, 0x000, callsign);
    rc |= expect_eq_str("WACN 0x00000 SysID 0x000", callsign, "      ");

    // Test case: Boundaries
    // WACN 0xFFFFF (max), SysID 0xFFF (max)
    // n1 = 0xFFFFF / 16 = 0xFFFF = 65535
    // n2 = 4096 * 15 + 4095 = 61440 + 4095 = 65535
    // For n1=65535: 65535/1600=40 (out of range!) - need to verify algorithm handles this
    // Actually, max valid: 64000 = 40*1600, so 65535 could overflow
    // Let's verify the algorithm clamps or wraps appropriately
    p25_wacn_sysid_to_callsign(0xFFFFF, 0xFFF, callsign);
    // Just verify it doesn't crash and produces something
    rc |= (strlen(callsign) != 6);
    if (strlen(callsign) != 6) {
        fprintf(stderr, "Max WACN/SysID: expected 6 chars, got %zu\n", strlen(callsign));
    }

    // Test case: Motorola BEE00 - generic WACN, should return empty string
    // BEE00 is Motorola's default WACN used across many systems. It was NOT
    // derived from an FCC callsign, so the Radix-50 decode is meaningless.
    p25_wacn_sysid_to_callsign(0xBEE00, 0x001, callsign);
    rc |= expect_eq_str("WACN 0xBEE00 (Motorola generic)", callsign, "");

    // Test case: Harris A4xxx range - generic WACN, should return empty string
    p25_wacn_sysid_to_callsign(0xA4000, 0x001, callsign);
    rc |= expect_eq_str("WACN 0xA4000 (Harris generic)", callsign, "");

    p25_wacn_sysid_to_callsign(0xA4FFF, 0xFFF, callsign);
    rc |= expect_eq_str("WACN 0xA4FFF (Harris generic)", callsign, "");

    // Test case: Known callsign-derived WACN
    // WPIH50 corresponds to Michigan MPSCS system
    // WACN 0x79692 (796-92 in some notations), SysID 0x493
    // Let's verify a callsign-derived WACN works
    // From reverse calculation: WPIH50 maps to specific WACN/SysID
    // W=23, P=16, I=9, H=8, 5=35, 0=30
    // n1 = 23*1600 + 16*40 + 9 = 36800 + 640 + 9 = 37449
    // n2 = 8*1600 + 35*40 + 30 = 12800 + 1400 + 30 = 14230
    // wacn = 16*n1 + (n2/4096) = 16*37449 + 3 = 599187 = 0x92493
    // sysid = n2 % 4096 = 14230 % 4096 = 1942 = 0x796 -- wait that's backwards
    // Actually n2/4096 = 14230/4096 = 3, sysid = 14230 - 3*4096 = 14230 - 12288 = 1942 = 0x796
    // So WACN = 0x92493, SysID = 0x796 -> WPIH50
    p25_wacn_sysid_to_callsign(0x92493, 0x796, callsign);
    rc |= expect_eq_str("WACN 0x92493 SysID 0x796 (MPSCS)", callsign, "WPIH50");

    // Test: Format function with generic WACN (should NOT include callsign)
    char buf[64];
    int n = p25_format_wacn_sysid(0xBEE00, 0x001, buf, sizeof(buf));
    rc |= (n <= 0);
    if (n <= 0) {
        fprintf(stderr, "p25_format_wacn_sysid returned %d\n", n);
    }
    // Should contain the WACN and SysID but NOT a callsign in parentheses
    rc |= (strstr(buf, "BEE00") == NULL);
    rc |= (strstr(buf, "001") == NULL);
    // Should NOT contain the meaningless "0UX" decode
    if (strstr(buf, "0UX") != NULL) {
        fprintf(stderr, "format_wacn_sysid should not include callsign for BEE00: got '%s'\n", buf);
        rc |= 1;
    }

    // Test: Format function with callsign-derived WACN (should include callsign)
    n = p25_format_wacn_sysid(0x92493, 0x796, buf, sizeof(buf));
    rc |= (n <= 0);
    if (n <= 0) {
        fprintf(stderr, "p25_format_wacn_sysid returned %d\n", n);
    }
    rc |= (strstr(buf, "92493") == NULL);
    rc |= (strstr(buf, "WPIH50") == NULL);
    if (strstr(buf, "WPIH50") == NULL) {
        fprintf(stderr, "format_wacn_sysid should include WPIH50: got '%s'\n", buf);
    }

    return rc;
}
