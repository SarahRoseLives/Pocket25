// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*-------------------------------------------------------------------------------
 * dsd_gps.c
 * GPS Handling Functions for Various Protocols
 *
 * LWVMOBILE
 * 2023-12 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/protocol/dmr/dmr_utf8_text.h>
#include <dsd-neo/protocol/dmr/dmr_utils_api.h>
#include <dsd-neo/runtime/colors.h>
#include <dsd-neo/runtime/unicode.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
lip_protocol_decoder(dsd_opts* opts, dsd_state* state, uint8_t* input) {
    //NOTE: This is defined in ETSI TS 102 361-4 V1.12.1 (2023-07) p208
    //6.6.11.3.2 USBD Polling Service Poll Response PDU for LIP (That's a mouthful)

    int slot = state->currentslot;

    //May need to set this in the UDT header, or pass it into this function to be sure
    // uint32_t src = 0;
    // if (slot == 0)
    //   src = state->lastsrc;
    // else src = state->lastsrcR;

    //NOTE: This format is pretty much the same as DMR EMB GPS, but has a few extra elements,
    //so I got lazy and just lifted most of the code from there, also assuming same lat/lon calcs
    //since those have been tested to work in DMR EMB GPS and units are same in LIP

    fprintf(stderr, "Location Information Protocol; ");

    // uint8_t service_type = (uint8_t)ConvertBitIntoBytes(&input[0], 4); //checked before arrival here
    uint8_t time_elapsed = (uint8_t)ConvertBitIntoBytes(&input[6], 2);
    uint8_t lon_sign = input[8];
    uint32_t lon = (uint32_t)ConvertBitIntoBytes(&input[9], 24); //8, 25
    uint8_t lat_sign = input[33];
    uint32_t lat = (uint32_t)ConvertBitIntoBytes(&input[34], 23); //33, 24
    uint8_t pos_err = (uint8_t)ConvertBitIntoBytes(&input[57], 2);
    uint8_t hor_vel = (uint8_t)ConvertBitIntoBytes(&input[59], 7);
    uint8_t dir_tra = (uint8_t)ConvertBitIntoBytes(&input[66], 4);
    uint8_t reason = (uint8_t)ConvertBitIntoBytes(&input[70], 3);
    uint8_t add_hash = (uint8_t)ConvertBitIntoBytes(&input[73], 8); //MS Source Address Hash

    //NOTE: May need to use double instead of float to avoid rounding errors
    double latitude, longitude = 0.0f;
    /* Avoid pow(2, n): use exact constants for 2^24 and 2^25 */
    double lat_unit = 180.0 / 16777216.0; // 180 / (2^24)
    double lon_unit = 360.0 / 33554432.0; // 360 / (2^25)
    double lon_sf = 1.0f;                 //float value we can multiple longitude with
    double lat_sf = 1.0f;                 //float value we can multiple latitude with

    char latstr[3];
    char lonstr[3];
    snprintf(latstr, sizeof latstr, "%s", "N");
    snprintf(lonstr, sizeof lonstr, "%s", "E");

    const char* deg_glyph = dsd_degrees_glyph();

    //lat and lon calculations (two's complement conversion)
    if (lat_sign) {
        lat = 0x800000 - lat;
        snprintf(latstr, sizeof latstr, "%s", "S");
        lat_sf = -1.0f;
    }
    latitude = ((double)lat * lat_unit);

    if (lon_sign) {
        lon = 0x1000000 - lon;
        snprintf(lonstr, sizeof lonstr, "%s", "W");
        lon_sf = -1.0f;
    }
    longitude = ((double)lon * lon_unit);

    //6.3.63 Position Error
    //6.3.17 Horizontal velocity
    /*
    Horizontal velocity shall be encoded for speeds 0 km/h to 28 km/h in 1 km/h steps and
    from 28 km/h onwards using equation: v = C × (1 + x)^(K-A) + B where:
  */
    float v = 0.0f;
    if (hor_vel > 28) {
        /* Precomputed LUT for v = 16 * (1.038)^(K-13), K in [0,127] */
        static int lut_init = 0;
        static float v_lut[128];
        if (!lut_init) {
            /* Fill formula values from K=13 upward using iterative multiply to avoid powf */
            const float f = 1.038f;
            for (int k = 0; k < 128; k++) {
                v_lut[k] = 0.0f;
            }
            v_lut[13] = 16.0f; /* (K-13)==0 */
            for (int k = 14; k < 128; k++) {
                v_lut[k] = v_lut[k - 1] * f;
            }
            lut_init = 1;
        }
        v = v_lut[hor_vel];
    } else {
        v = (float)hor_vel;
    }

    float dir = (((float)dir_tra + 11.25f) / 22.5f); //page 68, Table 6.45

    //truncated and rounded forms
    int vt = (int)v;
    int dt = (int)dir;

    //sanity check
    if (fabs(latitude) <= 90.0 && fabs(longitude) <= 180.0) {
        fprintf(stderr, "Src(Hash); %03d;  Lat: %.5lf%s%s Lon: %.5lf%s%s (%.5lf, %.5lf); Spd: %d km/h; Dir: %d%s",
                add_hash, latitude, deg_glyph, latstr, longitude, deg_glyph, lonstr, lat_sf * latitude,
                lon_sf * longitude, vt, dt, deg_glyph);

        //6.3.63 Position Error (2 * 10^pos_err) via tiny LUT
        static const uint32_t pow10_lut[8] = {1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u};
        unsigned int position_error = (unsigned int)(2u * pow10_lut[pos_err & 7]);
        if (pos_err == 0x7) {
            fprintf(stderr, "\n  Position Error: Unknown or Invalid;");
        } else {
            fprintf(stderr, "\n  Position Error: Less than %dm;", position_error);
        }

        //Reason For Sending 6.6.11.3.3 Table 6.80
        if (reason) {
            fprintf(stderr, " Reserved: %d;", reason);
        } else {
            fprintf(stderr, " Request Response; ");
        }

        //6.3.78 Time elapsed
        if (time_elapsed == 0) {
            fprintf(stderr, " TE: < 5s;");
        }
        if (time_elapsed == 1) {
            fprintf(stderr, " TE: < 5m;");
        }
        if (time_elapsed == 2) {
            fprintf(stderr, " TE: < 30m;");
        }
        if (time_elapsed == 3) {
            fprintf(stderr, " TE: NA or UNK;"); //not applicable or unknown
        }

        //save to array for ncurses
        if (pos_err != 0x7) {
            snprintf(state->dmr_embedded_gps[slot], sizeof state->dmr_embedded_gps[slot],
                     "%03d; LIP: %.5lf%s%s %.5lf%s%s; Err: %dm; Spd: %d km/h; Dir: %d%s", add_hash, latitude, deg_glyph,
                     latstr, longitude, deg_glyph, lonstr, position_error, vt, dt, deg_glyph);
        } else {
            snprintf(state->dmr_embedded_gps[slot], sizeof state->dmr_embedded_gps[slot],
                     "%03d; LIP: %.5lf%s%s %.5lf%s%s Unknown Pos Err; Spd: %d km/h; Dir %d%s", add_hash, latitude,
                     deg_glyph, latstr, longitude, deg_glyph, lonstr, vt, dt, deg_glyph);
        }

        //save to event history string
        snprintf(state->event_history_s[slot].Event_History_Items[0].gps_s,
                 sizeof state->event_history_s[slot].Event_History_Items[0].gps_s, "%s", state->dmr_embedded_gps[slot]);

        //save to LRRP report for mapping/logging
        FILE* pFile; //file pointer
        if (opts->lrrp_file_output == 1) {

            char datestr[9];
            char timestr[7];
            getDate_buf(datestr);
            getTime_buf(timestr);

            //open file by name that is supplied in the ncurses terminal, or cli
            pFile = fopen(opts->lrrp_out_file, "a");
            if (pFile != NULL) {
                fprintf(pFile, "%s\t", datestr);
                fprintf(pFile, "%s\t", timestr);
                fprintf(pFile, "%08d\t", add_hash);
                // LRRP/QGIS mapping expects signed decimal degrees
                fprintf(pFile, "%.5lf\t", lat_sf * latitude);
                fprintf(pFile, "%.5lf\t", lon_sf * longitude);
                fprintf(pFile, "%d\t", vt); //speed in km/h
                fprintf(pFile, "%d\t", dt); //direction of travel
                fprintf(pFile, "\n");
                fclose(pFile);
            }

            /* stack buffers; no free */
        }

    } else {
        fprintf(stderr, " Position Calculation Error;");
    }
}

void
nmea_iec_61162_1(dsd_opts* opts, dsd_state* state, uint8_t* input, uint32_t src, int type) {
    int slot = state->currentslot;

    //NOTE: The Only difference between Short (type == 1) and Long Format (type == 2)
    //is the utc_ss3 on short vs utc_ss6 and inclusion of COG value on long

    //NOTE: MFID Specific Formats are not handled here, unknown, could be added if worked out
    //they could share most of the same elements and use the large spare bits in block 2 for extra
    // uint8_t mfid = (uint8_t)ConvertBitIntoBytes(&input[88], 8); //on type 3, last octet of first block carries MFID

    // uint8_t nmea_c = input[0];  //encrypted -- checked before we get here
    uint8_t nmea_ns = input[1];                                      //north/south (lat sign)
    uint8_t nmea_ew = input[2];                                      //east/west (lon sign)
    uint8_t nmea_q = input[3];                                       //Quality Indicator (no fix or fix valid)
    uint8_t nmea_speed = (uint8_t)ConvertBitIntoBytes(&input[4], 7); //speed in knots (127 = greater than 126 knots)

    //Latitude Bits
    uint8_t nmea_ndeg = (uint8_t)ConvertBitIntoBytes(&input[11], 7);     //Latitude Degrees
    uint8_t nmea_nmin = (uint8_t)ConvertBitIntoBytes(&input[18], 6);     //Latitude Minutes
    uint16_t nmea_nminf = (uint16_t)ConvertBitIntoBytes(&input[24], 14); //Latitude Fractions of Minutes

    //Longitude Bits
    uint8_t nmea_edeg = (uint8_t)ConvertBitIntoBytes(&input[38], 8);     //Longitude Degrees
    uint8_t nmea_emin = (uint8_t)ConvertBitIntoBytes(&input[46], 6);     //Longitude Minutes
    uint16_t nmea_eminf = (uint16_t)ConvertBitIntoBytes(&input[52], 14); //Longitude Fractions of Minutes

    //UTC Time and COG
    uint8_t nmea_utc_hh = (uint8_t)ConvertBitIntoBytes(&input[66], 5);
    uint8_t nmea_utc_mm = (uint8_t)ConvertBitIntoBytes(&input[71], 6);
    //seconds and the addition of COG is the difference between short and long formats
    uint8_t nmea_utc_ss3 = (uint8_t)ConvertBitIntoBytes(&input[77], 3) * 10; //seconds in 10s
    uint8_t nmea_utc_ss6 = (uint8_t)ConvertBitIntoBytes(&input[77], 6);      //seconds in 1s
    uint16_t nmea_cog = (uint16_t)ConvertBitIntoBytes(&input[103], 9);       //course over ground in degrees

    //lat and lon conversion
    const char* deg_glyph = dsd_degrees_glyph();
    float latitude = 0.0f;
    float longitude = 0.0f;
    // float m_unit = 1.0f / 60.0f;     //unit to convert min into decimal value - (1/60)*60 minutes = 1 degree
    // float mm_unit = 1.0f / 10000.0f;  //unit to convert minf into decimal value - (0000 - 9999) 0.0001×9999 = .9999 minutes, so its sub 1 minute decimal

    //testing w/ Harris NMEA like values (ran tests over there with this code, this seems to work on those values)
    float m_unit = 1.0f / 60.0f; //unit to convert min into decimal value - (1/60)*60 minutes = 1 degree
    float mm_unit =
        1.0f
        / 600000.0f; //unit to convert minf into decimal value - (0000 - 9999) 0.0001×9999 = .9999 minutes, so its sub 1 minute decimal

    //speed conversion
    float fmps, fmph, fkph = 0.0f; //conversion of knots to mps, kph, and mph values
    fmps = (float)nmea_speed * 0.514444;
    UNUSED(fmps);
    fmph = (float)nmea_speed * 1.15078f;
    UNUSED(fmph);
    fkph = (float)nmea_speed * 1.852f;

    //calculate decimal representation of latidude and longitude (need some samples to test)
    latitude = ((float)nmea_ndeg + ((float)nmea_nmin * m_unit) + ((float)nmea_nminf * mm_unit));
    longitude = ((float)nmea_edeg + ((float)nmea_emin * m_unit) + ((float)nmea_eminf * mm_unit));

    if (!nmea_ns) {
        latitude *= -1.0f; //0 is South, 1 is North
    }
    if (!nmea_ew) {
        longitude *= -1.0f; //0 is West, 1 is East
    }

    fprintf(stderr, " GPS: %f%s, %f%s;", latitude, deg_glyph, longitude, deg_glyph);

    //Speed in Knots
    if (nmea_speed > 126) {
        fprintf(stderr, " SPD > 126 knots or %f kph;", fkph);
    } else {
        fprintf(stderr, " SPD: %d knots; %f kph;", nmea_speed, fkph);
    }

    //Print UTC Time and COG according to Format Type
    if (type == 1) {
        fprintf(stderr, " FIX: %d; %02d:%02d:%02d UTC; Short Format;", nmea_q, nmea_utc_hh, nmea_utc_mm, nmea_utc_ss3);
    }
    if (type == 2) {
        fprintf(stderr, " FIX: %d; %02d:%02d:%02d UTC; COG: %d%s; Long Format;", nmea_q, nmea_utc_hh, nmea_utc_mm,
                nmea_utc_ss6, nmea_cog, deg_glyph);
    }

    //save to ncurses string
    snprintf(state->dmr_embedded_gps[slot], sizeof state->dmr_embedded_gps[slot], "GPS: (%f%s, %f%s)", latitude,
             deg_glyph, longitude, deg_glyph);

    //save to event history string
    snprintf(state->event_history_s[slot].Event_History_Items[0].gps_s,
             sizeof state->event_history_s[slot].Event_History_Items[0].gps_s, "(%f%s, %f%s)", latitude, deg_glyph,
             longitude, deg_glyph);

    //save to LRRP report for mapping/logging
    FILE* pFile; //file pointer
    if (opts->lrrp_file_output == 1) {

        char datestr[9];
        char timestr[7];
        getDate_buf(datestr);
        getTime_buf(timestr);

        int s = (int)fkph; //rounded interger format for the log report
        int a = 0;
        if (type == 2) {
            a = nmea_cog; //long format only
        }
        //open file by name that is supplied in the ncurses terminal, or cli
        pFile = fopen(opts->lrrp_out_file, "a");
        if (pFile != NULL) {
            fprintf(pFile, "%s\t", datestr);
            fprintf(pFile, "%s\t", timestr); //could switch to UTC time if desired, but would require local user offset
            fprintf(pFile, "%08d\t", src);
            fprintf(pFile, "%.6lf\t", latitude);
            fprintf(pFile, "%.6lf\t", longitude);
            fprintf(pFile, "%d\t ", s);
            fprintf(pFile, "%d\t ", a);
            fprintf(pFile, "\n");
            fclose(pFile);
        }

        /* stack buffers; no free */
    }
}

//restructured the Harris GPS to flow more like the DMR UDT NMEA Format when possible
void
nmea_harris(dsd_opts* opts, dsd_state* state, uint8_t* input, uint32_t src, int slot) {
    if (!opts || !state || !input) {
        return;
    }

    /*
     * L3Harris Talker GPS (P25 Phase 1/2)
     *
     * Aligns with SDRTrunk's L3HarrisGPS bitfield definitions.
     *
     * Note: In our codepaths, the formatted GPS data starts at bit offset 40:
     * - P25p1: we concatenate the two 56-bit LC blocks and place them at +40
     * - P25p2: vendor MAC structure places the GPS at offset 16+24 == 40
     */
    const uint32_t gps_off = 40;

    const char* deg_glyph = dsd_degrees_glyph();

    // Detect LCW vs MAC only for display context.
    uint16_t header = (uint16_t)ConvertBitIntoBytes(&input[0], 16);

    // Latitude: degrees/minutes/fractional minutes (1/10000) with hemisphere flag
    uint32_t lat_frac = (uint32_t)ConvertBitIntoBytes(&input[gps_off + 0], 16);
    uint8_t lat_hemi = input[gps_off + 16] & 1; //1 == negative
    uint32_t lat_min = (uint32_t)ConvertBitIntoBytes(&input[gps_off + 17], 7);
    uint32_t lat_deg = (uint32_t)ConvertBitIntoBytes(&input[gps_off + 24], 8);

    // Longitude: degrees/minutes/fractional minutes (1/10000) with hemisphere flag
    uint32_t lon_frac = (uint32_t)ConvertBitIntoBytes(&input[gps_off + 32], 16);
    uint8_t lon_hemi = input[gps_off + 48] & 1; //1 == negative
    uint32_t lon_min = (uint32_t)ConvertBitIntoBytes(&input[gps_off + 49], 7);
    uint32_t lon_deg = (uint32_t)ConvertBitIntoBytes(&input[gps_off + 56], 8);

    double latitude = (double)lat_deg + (((double)lat_min + ((double)lat_frac / 10000.0)) / 60.0);
    if (lat_hemi) {
        latitude *= -1.0;
    }

    double longitude = (double)lon_deg + (((double)lon_min + ((double)lon_frac / 10000.0)) / 60.0);
    if (lon_hemi) {
        longitude *= -1.0;
    }

    // Timestamp: seconds since midnight UTC (16-bit + separate MSB flag)
    uint32_t seconds = (uint32_t)ConvertBitIntoBytes(&input[gps_off + 64], 16);
    if (input[gps_off + 80]) {
        seconds += 65536U;
    }
    seconds %= 86400U; //match HH:mm:ss formatting behavior

    uint32_t thour = seconds / 3600U;
    uint32_t tmin = (seconds % 3600U) / 60U;
    uint32_t tsec = seconds % 60U;

    // Heading: 9-bit field (0-359 degrees per SDRTrunk; keep raw)
    uint16_t heading = (uint16_t)ConvertBitIntoBytes(&input[gps_off + 95], 9);

    // Sanity check
    if (fabs(latitude) > 90.0 || fabs(longitude) > 180.0) {
        fprintf(stderr, "\n");
        if (header == 0x2AA4) {
            fprintf(stderr, " SRC: %08d;", src);
        } else {
            fprintf(stderr, " VCH: %d - SRC: %08d;", slot, src);
        }
        fprintf(stderr, " Harris GPS: Invalid Position;");
        return;
    }

    uint8_t slot_idx = (slot >= 2) ? 1 : (uint8_t)slot;

    fprintf(stderr, "\n");
    if (header == 0x2AA4) {
        fprintf(stderr, " SRC: %08d;", src);
    } else {
        fprintf(stderr, " VCH: %d - SRC: %08d;", slot, src);
    }
    fprintf(stderr, " Harris GPS: %.6lf%s, %.6lf%s;", latitude, deg_glyph, longitude, deg_glyph);
    fprintf(stderr, " HEADING: %03u%s;", (unsigned int)heading, deg_glyph);
    fprintf(stderr, " TIME: %02u:%02u:%02u UTC;", (unsigned int)thour, (unsigned int)tmin, (unsigned int)tsec);

    // save to ncurses string
    snprintf(state->dmr_embedded_gps[slot_idx], sizeof state->dmr_embedded_gps[slot_idx], "(%.6lf%s, %.6lf%s) %03u%s",
             latitude, deg_glyph, longitude, deg_glyph, (unsigned int)heading, deg_glyph);

    // save to event history string
    if (state->event_history_s[slot_idx].Event_History_Items[0].source_id == src && src != 0) {
        snprintf(state->event_history_s[slot_idx].Event_History_Items[0].gps_s,
                 sizeof state->event_history_s[slot_idx].Event_History_Items[0].gps_s, "%s",
                 state->dmr_embedded_gps[slot_idx]);
    }

    // save to LRRP report for mapping/logging
    if (opts->lrrp_file_output == 1) {

        char datestr[9];
        char timestr[7];
        getDate_buf(datestr);
        getTime_buf(timestr);

        // open file by name that is supplied in the ncurses terminal, or cli
        FILE* pFile = fopen(opts->lrrp_out_file, "a");
        if (pFile != NULL) {
            fprintf(pFile, "%s\t", datestr);
            fprintf(pFile, "%s\t", timestr); //system time for consistency
            fprintf(pFile, "%08d\t", src);
            fprintf(pFile, "%.6lf\t", latitude);
            fprintf(pFile, "%.6lf\t", longitude);
            fprintf(pFile, "0\t "); //speed not provided in the Harris talker GPS payload
            fprintf(pFile, "%d\t ", (int)heading);
            fprintf(pFile, "\n");
            fclose(pFile);
        }
        // pFile is closed above when non-NULL
    }
}

//fallback version (if desired/required)
void
harris_gps(dsd_opts* opts, dsd_state* state, int slot, uint8_t* input) {

    uint8_t lat_sign, lat_deg, lat_min = 0;
    uint8_t lon_sign, lon_deg, lon_min = 0;
    uint16_t lat_mmin = 0;
    uint16_t lon_mmin = 0;

    //potentially in this PDU, but unverifiable without documentation or reasonable
    //mathematical proof vs map points and direction of travel / distance over time (assuming the radio is facing that direction)
    uint16_t rspeed = (uint16_t)ConvertBitIntoBytes(&input[136], 8);        //MSB //136
    rspeed = (rspeed << 8) + (uint16_t)ConvertBitIntoBytes(&input[128], 8); //LSB //128

    //this works okay for example of driver driving due west at the speed limit of the road (fluke?)
    uint16_t rangle = (uint16_t)ConvertBitIntoBytes(&input[120], 8); //120,8
    float fspeed = (float)rspeed;
    float fangle = (float)rangle;

    fspeed /= 255.0f; //unit value of 1 bit
    fspeed *=
        1.56f; //this is based on an observation of a driver moving approx 210 meters in about 8 seconds and this makes it just under the speed limit on the road there
    // fangle *= 360.0f/255.0f; //unit value of 1 bit
    fangle *= 2.0f; //may exceed 360 degrees as is (use %)

    int s, a = 0;
    s = (int)fspeed * 3.6;
    a = (int)fangle % 360;

    //fix and quality indicators?
    uint8_t fix = input[147];
    UNUSED(fix);
    uint8_t quality = (uint16_t)ConvertBitIntoBytes(&input[148], 3);
    UNUSED(quality);

    //timestamp (16-bit value w/ appended 17th bit to MSB)
    uint32_t rtime = (uint32_t)ConvertBitIntoBytes(
        &input[104], 16); //seconds since midnight since last GPS fix (whatever the radio has as internal time)
    rtime |=
        input[144] << 16; //test appending this as bit 17 (observed this bit set after the afternoon 0xFFFF rollover)
    uint32_t thour = rtime / 3600;
    uint32_t tmin = (rtime % 3600) / 60;
    uint32_t tsec = (rtime % 3600) % 60;

    float lat_dec = 0.0f;
    float lon_dec = 0.0f;

    const char* deg_glyph = dsd_degrees_glyph();

    //This appears to be similar to the NMEA GPGGA format (DDmm.mm) but
    //octets are ordered in least significant to most significant value
    lat_mmin = (uint16_t)ConvertBitIntoBytes(&input[40], 16); //? bits required, but grabbing two octets
    lat_min = (uint8_t)ConvertBitIntoBytes(&input[58], 6);    //6 bits required to get 60
    lat_deg = (uint8_t)ConvertBitIntoBytes(&input[65], 7);    //7 bits required to get 90
    lat_sign = input[72];                                     //64

    lon_mmin = (uint16_t)ConvertBitIntoBytes(&input[72], 16); //? bits required, but grabbing two octets
    lon_min = (uint8_t)ConvertBitIntoBytes(&input[90], 6);    //6 bits required to get 60
    lon_deg = (uint8_t)ConvertBitIntoBytes(&input[96], 8);    //8 bits required to get 180
    lon_sign =
        input[88]; //88, unsure of a correct location, but on the sample with 0 minutes, this lonely bit was flagged on

    int src = 0;
    if (slot == 0) {
        src = state->lastsrc;
    }
    if (slot == 1) {
        src = state->lastsrcR;
    }

    //calculate decimal representation (was a pain to figure out the sub minute values)
    lat_dec = ((float)lat_deg + ((float)lat_min / 60.0f) + ((float)lat_mmin / 600000.0f));
    lon_dec = ((float)lon_deg + ((float)lon_min / 60.0f) + ((float)lon_mmin / 600000.0f));

    if (lat_sign) {
        lat_dec *= -1.0f;
    }

    if (lon_sign) {
        lon_dec *= -1.0f;
    }

    //line break
    fprintf(stderr, "\n");
    fprintf(stderr, " GPS: %f%s, %f%s;", lat_dec, deg_glyph, lon_dec, deg_glyph);

    //gps fix time
    // fprintf (stderr, " RT: %05X;", rtime); //debug
    fprintf(stderr, " LTS: %02d:%02d:%02d UTC;", thour, tmin, tsec); //last time synced to GPS

    //speed and direction
    // fprintf (stderr, " RSPD: %04X;", rspeed); //debug
    // fprintf (stderr, " RANG: %02X;", rangle); //debug
    // fprintf (stderr, " MPS: %06.02f;", fspeed);
    // fprintf (stderr, " KPH: %06.02f;", fspeed * 3.6f);
    // fprintf (stderr, " MPH: %06.02f;", fspeed * 2.2369f);
    fprintf(stderr, " DIR: %03d%s;", a, deg_glyph);

    // fprintf (stderr, " FIX: %d", fix);
    // fprintf (stderr, " QLT: %02d", quality);

    // fprintf (stderr, " SRC: %08d;", src);

    //save to array for ncurses (guard slot index)
    {
        uint8_t slot_idx = (slot >= 2) ? 1 : slot;
        snprintf(state->dmr_embedded_gps[slot_idx], sizeof state->dmr_embedded_gps[slot_idx], "GPS: (%f%s, %f%s)",
                 lat_dec, deg_glyph, lon_dec, deg_glyph);
    }

    //save to LRRP report for mapping/logging
    FILE* pFile; //file pointer
    if (opts->lrrp_file_output == 1) {

        char* datestr = getDate();
        char* timestr = getTime();

        //open file by name that is supplied in the ncurses terminal, or cli
        pFile = fopen(opts->lrrp_out_file, "a");
        if (pFile != NULL) {
            fprintf(pFile, "%s\t", datestr);
            fprintf(pFile, "%s\t", timestr);
            fprintf(pFile, "%08d\t", src);
            fprintf(pFile, "%.6lf\t", lat_dec);
            fprintf(pFile, "%.6lf\t", lon_dec);
            fprintf(pFile, "%d\t ", s);
            fprintf(pFile, "%d\t ", a);
            fprintf(pFile, "\n");
            fclose(pFile);
        }

        if (timestr != NULL) {
            free(timestr);
            timestr = NULL;
        }
        if (datestr != NULL) {
            free(datestr);
            datestr = NULL;
        }
    }

    //NOTE: Thanks to DSheirer (SDRTrunk) for helping me work out a few of the things in here
    //not entirely convinced on some of these calcs (speed, angle, and TS) but sure these bits
    //are actual speed/direction/timestamps judging from what the coordinates show on a map
}

//externalize embedded GPS - Confirmed working now on NE, NW, SE, and SW coordinates
void
dmr_embedded_gps(dsd_opts* opts, dsd_state* state, uint8_t lc_bits[]) {
    UNUSED(opts);

    fprintf(stderr, "%s", KYEL);
    fprintf(stderr, " Embedded GPS:");
    uint8_t slot = state->currentslot;
    uint8_t pf = lc_bits[0];
    uint8_t res_a = lc_bits[1];
    uint8_t res_b = (uint8_t)ConvertBitIntoBytes(&lc_bits[16], 4);
    uint8_t pos_err = (uint8_t)ConvertBitIntoBytes(&lc_bits[20], 3);
    UNUSED2(res_a, res_b);

    const char* deg_glyph = dsd_degrees_glyph();

    uint32_t lon_sign = lc_bits[23];
    uint32_t lon = (uint32_t)ConvertBitIntoBytes(&lc_bits[24], 24);
    uint32_t lat_sign = lc_bits[48];
    uint32_t lat = (uint32_t)ConvertBitIntoBytes(&lc_bits[49], 23);
    double lon_sf = 1.0f; //float value we can multiple longitude with
    double lat_sf = 1.0f; //float value we can multiple latitude with

    double lat_unit = 180.0 / 16777216.0; // 180 / (2^24)
    double lon_unit = 360.0 / 33554432.0; // 360 / (2^25)

    char latstr[3];
    char lonstr[3];
    snprintf(latstr, sizeof latstr, "%s", "N");
    snprintf(lonstr, sizeof lonstr, "%s", "E");

    //run calculations and print
    //7.2.16 and 7.2.17 (two's compliment)

    double latitude = 0;
    double longitude = 0;

    if (pf) {
        fprintf(stderr, " Protected");
    } else {
        //two's complement conversion (ETSI TS 102 361-2 7.2.16/7.2.17)
        if (lat_sign) {
            lat = 0x800000 - lat;
            snprintf(latstr, sizeof latstr, "%s", "S");
            lat_sf = -1.0f;
        }
        latitude = ((double)lat * lat_unit);

        if (lon_sign) {
            lon = 0x1000000 - lon;
            snprintf(lonstr, sizeof lonstr, "%s", "W");
            lon_sf = -1.0f;
        }
        longitude = ((double)lon * lon_unit);

        //sanity check
        if (fabs(latitude) <= 90.0 && fabs(longitude) <= 180.0) {
            fprintf(stderr, " Lat: %.5lf%s%s Lon: %.5lf%s%s (%.5lf, %.5lf)", latitude, deg_glyph, latstr, longitude,
                    deg_glyph, lonstr, lat_sf * latitude, lon_sf * longitude);

            //7.2.15 Position Error: 2 * 10^pos_err via LUT
            static const uint32_t pow10_lut[8] = {1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u};
            unsigned int position_error = 0;
            if (pos_err <= 0x5) {
                position_error = (unsigned int)(2u * pow10_lut[pos_err]);
            }
            if (pos_err == 0x7) {
                fprintf(stderr, "\n  Position Error: Unknown or Invalid");
            } else if (pos_err == 0x6) {
                fprintf(stderr, "\n  Position Error: More than 200km");
            } else {
                fprintf(stderr, "\n  Position Error: Less than %dm", position_error);
            }

            //save to array for ncurses
            if (pos_err <= 0x5) {
                snprintf(state->dmr_embedded_gps[slot], sizeof state->dmr_embedded_gps[slot],
                         "GPS: %.5lf%s%s %.5lf%s%s Err: %dm", latitude, deg_glyph, latstr, longitude, deg_glyph, lonstr,
                         position_error);
            } else if (pos_err == 0x6) {
                snprintf(state->dmr_embedded_gps[slot], sizeof state->dmr_embedded_gps[slot],
                         "GPS: %.5lf%s%s %.5lf%s%s Err: >200km", latitude, deg_glyph, latstr, longitude, deg_glyph,
                         lonstr);
            } else {
                snprintf(state->dmr_embedded_gps[slot], sizeof state->dmr_embedded_gps[slot],
                         "GPS: %.5lf%s%s %.5lf%s%s Unknown Pos Err", latitude, deg_glyph, latstr, longitude, deg_glyph,
                         lonstr);
            }

            uint32_t src = 0;
            if (slot == 0) {
                src = state->lasttg;
            }
            if (slot == 1) {
                src = state->lasttgR;
            }

            //save to event history string
            if (state->event_history_s[slot].Event_History_Items[0].source_id == src) {
                snprintf(state->event_history_s[slot].Event_History_Items[0].gps_s,
                         sizeof state->event_history_s[slot].Event_History_Items[0].gps_s, "%s",
                         state->dmr_embedded_gps[slot]);
            }

            //save to LRRP report for mapping/logging
            FILE* pFile; //file pointer
            if (opts->lrrp_file_output == 1) {

                char datestr[9];
                char timestr[7];
                getDate_buf(datestr);
                getTime_buf(timestr);

                //open file by name that is supplied in the ncurses terminal, or cli
                pFile = fopen(opts->lrrp_out_file, "a");
                if (pFile != NULL) {
                    fprintf(pFile, "%s\t", datestr);
                    fprintf(pFile, "%s\t", timestr);
                    fprintf(pFile, "%08d\t", src);
                    // LRRP/QGIS mapping expects signed decimal degrees
                    fprintf(pFile, "%.5lf\t", lat_sf * latitude);
                    fprintf(pFile, "%.5lf\t", lon_sf * longitude);
                    fprintf(pFile, "0\t "); //zero for velocity
                    fprintf(pFile, "0\t "); //zero for azimuth
                    fprintf(pFile, "\n");
                    fclose(pFile);
                }
                // pFile is closed above when non-NULL
            }
        }
    }

    fprintf(stderr, "%s", KNRM);
}

//P25 Motorola APX GPS format (sign + magnitude encoding)
void
apx_embedded_gps(dsd_opts* opts, dsd_state* state, uint8_t lc_bits[]) {

    fprintf(stderr, "%s", KYEL);
    fprintf(stderr, " GPS:");
    uint8_t slot = state->currentslot;
    uint8_t pf = lc_bits[0];
    uint8_t res_a = lc_bits[1];
    uint8_t res_b = (uint8_t)ConvertBitIntoBytes(&lc_bits[16], 7);
    uint8_t expired = lc_bits[23]; //this bit seems to indicate that the GPS coordinates are out of date or fresh

    const char* deg_glyph = dsd_degrees_glyph();

    uint32_t lon_sign = lc_bits[48];
    uint32_t lon = (uint32_t)ConvertBitIntoBytes(&lc_bits[49], 23);
    uint32_t lat_sign = lc_bits[24];
    uint32_t lat = (uint32_t)ConvertBitIntoBytes(&lc_bits[25], 23);

    double lat_unit = 90.0 / 0x7FFFFF;
    double lon_unit = 180.0 / 0x7FFFFF;

    char latstr[3];
    char lonstr[3];
    char valid[12];
    snprintf(latstr, sizeof latstr, "%s", "N");
    snprintf(lonstr, sizeof lonstr, "%s", "E");
    snprintf(valid, sizeof valid, "%s", "Current Fix");

    double latitude = 0;
    double longitude = 0;

    if (pf) {
        fprintf(stderr, " Protected");
    } else {
        //Latitude: value encodes 0-90 degrees, sign indicates hemisphere
        latitude = ((double)lat * lat_unit);
        if (lat_sign) {
            latitude = -latitude;
            snprintf(latstr, sizeof latstr, "%s", "S");
        }

        //Longitude: value encodes 0-180 degrees, sign indicates hemisphere via offset
        longitude = ((double)lon * lon_unit);
        if (lon_sign) {
            longitude -= 180.0;
            snprintf(lonstr, sizeof lonstr, "%s", "W");
        }

        //sanity check
        if (fabs((float)latitude) <= 90.0f && fabs((float)longitude) <= 180.0f) {
            fprintf(stderr, " Lat: %.5lf%s%s Lon: %.5lf%s%s (%.5lf, %.5lf) ", latitude, deg_glyph, latstr, longitude,
                    deg_glyph, lonstr, latitude, longitude);

            if (expired) {
                fprintf(stderr, "Last Fix; ");
                snprintf(valid, sizeof valid, "%s", "Last Fix");
            } else if (!expired) {
                fprintf(stderr, "Current Fix; ");
            }

            if (res_a) {
                fprintf(stderr, "RES_A: %d; ", res_a);
            }

            if (res_b) {
                fprintf(stderr, "RES_B: %02X; ", res_b);
            }

            uint32_t src = 0;
            if (slot == 0) {
                src = state->lastsrc;
            }
            if (slot == 1) {
                src = state->lastsrcR;
            }

            //save to array for ncurses (guard slot index)
            uint8_t slot_idx = (slot >= 2) ? 1 : slot;
            snprintf(state->dmr_embedded_gps[slot_idx], sizeof state->dmr_embedded_gps[slot_idx],
                     "GPS: %lf%s%s %lf%s%s (%lf, %lf) %s", latitude, deg_glyph, latstr, longitude, deg_glyph, lonstr,
                     latitude, longitude, valid);

            //save to event history string
            if (state->event_history_s[slot_idx].Event_History_Items[0].source_id == src) {
                snprintf(state->event_history_s[slot_idx].Event_History_Items[0].gps_s,
                         sizeof state->event_history_s[slot_idx].Event_History_Items[0].gps_s, "%s",
                         state->dmr_embedded_gps[slot_idx]);
            }

            //save to LRRP report for mapping/logging
            FILE* pFile; //file pointer
            if (opts->lrrp_file_output == 1) {
                char* datestr = getDate();
                char* timestr = getTime();

                //open file by name that is supplied in the ncurses terminal, or cli
                pFile = fopen(opts->lrrp_out_file, "a");
                if (pFile != NULL) {
                    fprintf(pFile, "%s\t", datestr);
                    fprintf(pFile, "%s\t", timestr);
                    fprintf(pFile, "%08d\t", src);
                    fprintf(pFile, "%.5lf\t", latitude);
                    fprintf(pFile, "%.5lf\t", longitude);
                    fprintf(pFile, "0\t "); //zero for velocity
                    fprintf(pFile, "0\t "); //zero for azimuth
                    fprintf(pFile, "\n");
                    fclose(pFile);
                }
                // pFile is closed above when non-NULL

                if (timestr != NULL) {
                    free(timestr);
                    timestr = NULL;
                }
                if (datestr != NULL) {
                    free(datestr);
                    datestr = NULL;
                }
            }
        }
    }

    fprintf(stderr, "%s", KNRM);
}

void
decode_cellocator(dsd_opts* opts, dsd_state* state, uint8_t* input, int len) {
    //TODO: This
    UNUSED(opts);
    UNUSED(state);
    UNUSED(input);
    UNUSED(len);

    //UTF8 Text: MCGP (0x4D434750) followed by data values
    //(0417D1050000F45FD1DD00010000000097BDD56C81000009AAAABF12864C)
    utf8_to_text(state, 0, 4, input);

    fprintf(stderr, " Cellocator:");

    uint8_t type = input[4];
    if (type == 1) {
        fprintf(stderr, " Platform Manifest Data;");
    } else if (type == 2) {
        fprintf(stderr, " CAN Data;");
    } else if (type == 3) {
        fprintf(stderr, " CAN Trigger Data;");
    } else if (type == 4) {
        fprintf(stderr, " Time and Location Data;");
    } else if (type == 5) {
        fprintf(stderr, " Accelerometer Data;");
    } else if (type == 6) {
        fprintf(stderr, " PSP Alarm System Data;");
    } else if (type == 7) {
        fprintf(stderr, " Usage Counter Data;");
    } else if (type == 8) {
        fprintf(stderr, " Command Authentication Table Data;");
    } else if (type == 9) {
        fprintf(stderr, " GSM Neighbor List Data;");
    } else if (type == 10) {
        fprintf(stderr, " Maintenance Server Platform Manifest Data;");
    } else {
        fprintf(stderr, " Unknown Data;");
    }

    //Data afterwards appears to be an arbitrary len so has variable reporting data
    //will need to establish a len value for data and contents
}

void
decode_ars(dsd_opts* opts, dsd_state* state, uint8_t* input, int len) {
    //TODO: This
    UNUSED(opts);
    UNUSED(state);
    UNUSED(input);
    UNUSED(len);
}
