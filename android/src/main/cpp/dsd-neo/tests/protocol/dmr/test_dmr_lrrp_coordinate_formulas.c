// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Test: Compare LRRP coordinate decoding formulas against known reference data.
 *
 * Reference data from RadioReference forum:
 *   https://forums.radioreference.com/threads/motorola-lrrp-protocol.370081/
 *
 * Known packet: 801313232F341F8893000F663F7EBBBB07CB07555672
 *   - Latitude hex:  0x3F7EBBBB -> 44.645°
 *   - Longitude hex: 0x07CB0755 -> 10.959°
 *
 * Formulas under test:
 *   1. Current dsd-neo/SDRTrunk: sign-magnitude lat, two's complement lon
 *   2. RadioReference/ok-dmrlib: two's complement for both
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Tolerance for floating point comparison (0.01 degrees ~ 1km)
#define TOLERANCE 0.01

/*
 * Formula 1: Current dsd-neo implementation (fixed)
 * - Both lat and lon: two's complement signed 32-bit
 * - Latitude:  raw * 90 / 2^31
 * - Longitude: raw * 180 / 2^31
 */
static void
formula_dsd_neo(uint32_t lat_raw, uint32_t lon_raw, double* lat_out, double* lon_out) {
    int32_t lat_signed = (int32_t)lat_raw;
    int32_t lon_signed = (int32_t)lon_raw;

    *lat_out = ((double)lat_signed * 90.0) / 2147483648.0;
    *lon_out = ((double)lon_signed * 180.0) / 2147483648.0;
}

/*
 * Formula 2: RadioReference / ok-dmrlib
 * - Both lat and lon: two's complement signed 32-bit
 * - Latitude:  raw * 90 / 2^31  (or raw * 0.00000004190952)
 * - Longitude: raw * 180 / 2^31 (or raw * 0.00000008381903)
 */
static void
formula_radioreference(uint32_t lat_raw, uint32_t lon_raw, double* lat_out, double* lon_out) {
    // Both treated as signed 32-bit two's complement
    int32_t lat_signed = (int32_t)lat_raw;
    int32_t lon_signed = (int32_t)lon_raw;

    // RadioReference multipliers
    *lat_out = (double)lat_signed * 0.00000004190952; // 90 / 2^31
    *lon_out = (double)lon_signed * 0.00000008381903; // 180 / 2^31
}

/*
 * Formula 3: ok-dmrlib exact (from mbxml.py)
 * - Latitude:  raw * 90 / 2^31
 * - Longitude: raw * 360 / 2^32
 */
static void
formula_okdmrlib(uint32_t lat_raw, uint32_t lon_raw, double* lat_out, double* lon_out) {
    int32_t lat_signed = (int32_t)lat_raw;
    int32_t lon_signed = (int32_t)lon_raw;

    *lat_out = ((double)lat_signed * 90.0) / 2147483648.0;  // 90 / 2^31
    *lon_out = ((double)lon_signed * 360.0) / 4294967296.0; // 360 / 2^32
}

/*
 * Formula 4: Proposed fix - two's complement for both with consistent units
 * - Latitude:  signed_int32 * 90 / 2^31
 * - Longitude: signed_int32 * 180 / 2^31
 */
static void
formula_proposed(uint32_t lat_raw, uint32_t lon_raw, double* lat_out, double* lon_out) {
    int32_t lat_signed = (int32_t)lat_raw;
    int32_t lon_signed = (int32_t)lon_raw;

    *lat_out = ((double)lat_signed * 90.0) / 2147483648.0;  // 90 / 2^31
    *lon_out = ((double)lon_signed * 180.0) / 2147483648.0; // 180 / 2^31
}

static int
check_coords(const char* name, double lat, double lon, double exp_lat, double exp_lon) {
    double dlat = fabs(lat - exp_lat);
    double dlon = fabs(lon - exp_lon);

    printf("  %-20s: lat=%.6f lon=%.6f", name, lat, lon);

    if (dlat > TOLERANCE || dlon > TOLERANCE) {
        printf(" [FAIL: expected (%.6f, %.6f), delta=(%.6f, %.6f)]\n", exp_lat, exp_lon, dlat, dlon);
        return 1;
    }
    printf(" [OK]\n");
    return 0;
}

int
main(void) {
    int rc = 0;
    double lat, lon;

    printf("=== LRRP Coordinate Formula Comparison ===\n\n");

    /*
     * Test Case 1: RadioReference known data
     * Location: Northern Italy (44.645°N, 10.959°E)
     */
    printf("Test 1: RadioReference example (Northern Italy)\n");
    printf("  Raw: lat=0x3F7EBBBB lon=0x07CB0755\n");
    printf("  Expected: 44.645°N, 10.959°E\n");
    {
        uint32_t lat_raw = 0x3F7EBBBBu;
        uint32_t lon_raw = 0x07CB0755u;
        double exp_lat = 44.645;
        double exp_lon = 10.959;

        formula_dsd_neo(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("dsd-neo (current)", lat, lon, exp_lat, exp_lon);

        formula_radioreference(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("RadioReference", lat, lon, exp_lat, exp_lon);

        formula_okdmrlib(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("ok-dmrlib", lat, lon, exp_lat, exp_lon);

        formula_proposed(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("proposed fix", lat, lon, exp_lat, exp_lon);
    }
    printf("\n");

    /*
     * Test Case 2: Romania (user report location)
     * Approximate center: 45.9°N, 25.0°E
     * Generate expected raw values using RadioReference formula (inverted)
     */
    printf("Test 2: Romania (user report region)\n");
    {
        double exp_lat = 45.9;
        double exp_lon = 25.0;

        // Calculate raw values using RadioReference formula (inverted)
        // lat_raw = lat * 2^31 / 90
        // lon_raw = lon * 2^31 / 180
        int32_t lat_signed = (int32_t)(exp_lat * 2147483648.0 / 90.0);
        int32_t lon_signed = (int32_t)(exp_lon * 2147483648.0 / 180.0);
        uint32_t lat_raw = (uint32_t)lat_signed;
        uint32_t lon_raw = (uint32_t)lon_signed;

        printf("  Expected: %.3f°N, %.3f°E\n", exp_lat, exp_lon);
        printf("  Calculated raw (RadioRef formula): lat=0x%08X lon=0x%08X\n", lat_raw, lon_raw);

        formula_dsd_neo(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("dsd-neo (current)", lat, lon, exp_lat, exp_lon);

        formula_radioreference(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("RadioReference", lat, lon, exp_lat, exp_lon);

        formula_okdmrlib(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("ok-dmrlib", lat, lon, exp_lat, exp_lon);

        formula_proposed(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("proposed fix", lat, lon, exp_lat, exp_lon);
    }
    printf("\n");

    /*
     * Test Case 3: Southern hemisphere (Australia)
     * Sydney: -33.87°S, 151.21°E
     */
    printf("Test 3: Sydney, Australia (Southern hemisphere)\n");
    {
        double exp_lat = -33.87;
        double exp_lon = 151.21;

        int32_t lat_signed = (int32_t)(exp_lat * 2147483648.0 / 90.0);
        int32_t lon_signed = (int32_t)(exp_lon * 2147483648.0 / 180.0);
        uint32_t lat_raw = (uint32_t)lat_signed;
        uint32_t lon_raw = (uint32_t)lon_signed;

        printf("  Expected: %.2f°S, %.2f°E\n", -exp_lat, exp_lon);
        printf("  Calculated raw (RadioRef formula): lat=0x%08X lon=0x%08X\n", lat_raw, lon_raw);

        formula_dsd_neo(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("dsd-neo (current)", lat, lon, exp_lat, exp_lon);

        formula_radioreference(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("RadioReference", lat, lon, exp_lat, exp_lon);

        formula_okdmrlib(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("ok-dmrlib", lat, lon, exp_lat, exp_lon);

        formula_proposed(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("proposed fix", lat, lon, exp_lat, exp_lon);
    }
    printf("\n");

    /*
     * Test Case 4: Western hemisphere (USA)
     * New York: 40.71°N, -74.01°W
     */
    printf("Test 4: New York, USA (Western hemisphere)\n");
    {
        double exp_lat = 40.71;
        double exp_lon = -74.01;

        int32_t lat_signed = (int32_t)(exp_lat * 2147483648.0 / 90.0);
        int32_t lon_signed = (int32_t)(exp_lon * 2147483648.0 / 180.0);
        uint32_t lat_raw = (uint32_t)lat_signed;
        uint32_t lon_raw = (uint32_t)lon_signed;

        printf("  Expected: %.2f°N, %.2f°W\n", exp_lat, -exp_lon);
        printf("  Calculated raw (RadioRef formula): lat=0x%08X lon=0x%08X\n", lat_raw, lon_raw);

        formula_dsd_neo(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("dsd-neo (current)", lat, lon, exp_lat, exp_lon);

        formula_radioreference(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("RadioReference", lat, lon, exp_lat, exp_lon);

        formula_okdmrlib(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("ok-dmrlib", lat, lon, exp_lat, exp_lon);

        formula_proposed(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("proposed fix", lat, lon, exp_lat, exp_lon);
    }
    printf("\n");

    /*
     * Test Case 5: Southwest quadrant (South America)
     * Buenos Aires: -34.60°S, -58.38°W
     */
    printf("Test 5: Buenos Aires, Argentina (SW quadrant)\n");
    {
        double exp_lat = -34.60;
        double exp_lon = -58.38;

        int32_t lat_signed = (int32_t)(exp_lat * 2147483648.0 / 90.0);
        int32_t lon_signed = (int32_t)(exp_lon * 2147483648.0 / 180.0);
        uint32_t lat_raw = (uint32_t)lat_signed;
        uint32_t lon_raw = (uint32_t)lon_signed;

        printf("  Expected: %.2f°S, %.2f°W\n", -exp_lat, -exp_lon);
        printf("  Calculated raw (RadioRef formula): lat=0x%08X lon=0x%08X\n", lat_raw, lon_raw);

        formula_dsd_neo(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("dsd-neo (current)", lat, lon, exp_lat, exp_lon);

        formula_radioreference(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("RadioReference", lat, lon, exp_lat, exp_lon);

        formula_okdmrlib(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("ok-dmrlib", lat, lon, exp_lat, exp_lon);

        formula_proposed(lat_raw, lon_raw, &lat, &lon);
        rc |= check_coords("proposed fix", lat, lon, exp_lat, exp_lon);
    }
    printf("\n");

    printf("=== Summary ===\n");
    if (rc == 0) {
        printf("All tests passed!\n");
    } else {
        printf("Some tests FAILED - see above for details\n");
    }

    return rc;
}
