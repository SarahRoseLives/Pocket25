// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
 * dmr_bs.c
 * DMR Data (1/2, 3/4, 1) PDU Decoding
 *
 * LWVMOBILE
 * 2022-12 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/bit_packing.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/gps.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/protocol/dmr/dmr.h>
#include <dsd-neo/protocol/pdu.h>
#include <dsd-neo/runtime/colors.h>
#include <dsd-neo/runtime/unicode.h>

#include <stdio.h>
#include <string.h>

static inline void dsd_append(char* dst, size_t dstsz, const char* src);

//convert a value that is stored as a string decimal into a decimal uint16_t
uint16_t
convert_hex_to_dec(uint16_t input) {
    char num_str[10];
    memset(num_str, 0, sizeof(num_str));
    sprintf(num_str, "%X", input);
    input = 0;
    sscanf(num_str, "%hu", &input);
    return input;
}

void
utf16_to_text(dsd_state* state, uint8_t wr, uint16_t len, uint8_t* input) {
    uint8_t slot = state->currentslot;
    if (wr == 1) {
        sprintf(state->event_history_s[slot].Event_History_Items[0].text_message, "%s", ""); //full text string
    }
    // fprintf (stderr, "\n UTF16 Text: ");
    uint16_t ch16 = 0;
    for (uint16_t i = 0; i < len; i += 2) {
        ch16 = (uint16_t)input[i + 0];
        ch16 <<= 8;
        ch16 |= (uint16_t)input[i + 1];
        // fprintf (stderr, " %04X; ", ch16); //debug for raw values to check grouping for offset

        if (ch16 >= 0x20 && ch16 != 0x040D) { //if not a linebreak or terminal commmands
            if (dsd_unicode_supported()) {
                fprintf(stderr, "%lc", ch16);
            } else {
                /* best-effort ASCII: print low byte if printable */
                unsigned char lo = (unsigned char)(ch16 & 0xFF);
                if (lo >= 0x20 && lo < 0x7F) {
                    fputc((int)lo, stderr);
                } else {
                    fputc('?', stderr);
                }
            }
        } else if (ch16 == 0) { //if padding (0 could also indicate end of text terminator?)
            fprintf(stderr, "_");
        } else if (ch16 == 0x040D) { //Ѝ or 0x040D may be ETLF
            fprintf(stderr, " / ");
        } else {
            fprintf(stderr, "-");
        }

        //convert to ascii range (will break eastern langauge, but can't do much about that right now)
        char c[2];
        c[0] = (char)input[i + 1];
        c[1] = 0;

        //short version (disabled)
        // if (wr == 1 && i < 76 && input[i] == 0 && input[i+1] < 0x7F && input[i+1] >= 0x20)
        //   strcat (state->dmr_lrrp_gps[slot], c);

        //this is the long version, complete message for logging purposes
        if (wr == 1 && input[i] == 0 && input[i + 1] < 0x7F && input[i + 1] >= 0x20) {
            dsd_append(state->event_history_s[slot].Event_History_Items[0].text_message,
                       sizeof state->event_history_s[slot].Event_History_Items[0].text_message, c);
        }
    }

    //add elipses to indicate this is possibly truncated
    // if (wr == 1)
    //   strcat (state->dmr_lrrp_gps[slot], "...");

    //debug
    // if (wr == 1)
    //   fprintf (stderr, "%s", state->dmr_lrrp_gps[slot]);
}

void
utf8_to_text(dsd_state* state, uint8_t wr, uint16_t len, uint8_t* input) {
    uint8_t slot = state->currentslot;
    fprintf(stderr, "\n UTF8 Text: ");

    if (wr == 1) {
        sprintf(state->event_history_s[slot].Event_History_Items[0].text_message, "%s", ""); //full text string
    }

    for (uint16_t i = 0; i < len; i++) {
        if (input[i] >= 0x20 && input[i] < 0x7F) { //if not a linebreak or terminal commmands
            fprintf(stderr, "%c", input[i]);
        } else if (input[i] == 0) { //if padding (0 could also indicate end of text terminator?)
            fprintf(stderr, "_");
        }
        // else if (input[i] == 0x03) //ASCII end of text (observed on the NMEA LOCN ones anyways)
        //   break;
        else {
            fprintf(stderr, "-");
        }

        char c = input[i];

        //for now, just rip the first 40 or so chars lower byte value
        //in the ASCII Range (should be alright for a quick visual)
        // if (wr == 1 && i < 38 && c < 0x7F && c >= 0x20)
        //   strcat (state->dmr_lrrp_gps[slot], &c);

        //this is the long version, complete message for logging purposes
        if (wr == 1 && c < 0x7F && c >= 0x20) {
            dsd_append(state->event_history_s[slot].Event_History_Items[0].text_message,
                       sizeof state->event_history_s[slot].Event_History_Items[0].text_message, &c);
        }
    }

    //add elipses to indicate this is possibly truncated
    // if (wr == 1)
    //   strcat (state->dmr_lrrp_gps[slot], "...");
}

void
dmr_sd_pdu(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* DMR_PDU) {

    uint8_t slot = state->currentslot;
    uint16_t offset = 0; //sanity check of sorts, prevent extra long line print outs in the console
    if (len > 23) {
        offset = 23;
    }

    // if (DMR_PDU[0] == 0x01) //found some on another system that is 00 here, and not a Loction
    if (state->data_header_format[state->currentslot] == 13) //only short data: defined format (testing)
    {
        utf8_to_text(state, 0, len - offset, DMR_PDU + offset);
        dmr_locn(opts, state, len, DMR_PDU);
        sprintf(state->event_history_s[slot].Event_History_Items[0].gps_s, "%s", state->dmr_lrrp_gps[slot]);
        state->event_history_s[slot].Event_History_Items[0].color_pair =
            4; //Remus, add this line to a decode to change its line color
    } else {
        if (len >= (127 * 18)) {
            len = 127 * 18; //sanity check of sorts, prevent extra long line print outs in the console
        }
        utf8_to_text(state, 0, len, DMR_PDU); //generic catch-all to see if anything relevant is there
        // utf16_to_text(state, 0, len, DMR_PDU); //generic catch-all to see if anything relevant is there
    }

    //dump to event history
    uint32_t source = state->dmr_lrrp_source[slot];
    uint32_t target = state->dmr_lrrp_target[slot];
    char comp_string[500];
    memset(comp_string, 0, sizeof(comp_string));
    sprintf(comp_string, "Short Data SRC: %d; TGT: %d; ", source, target);
    watchdog_event_datacall(opts, state, source, target, comp_string, slot);
}

//reading ETSI, seems like these aren't compressed, just that they are preset indexed values on the radio
void
dmr_udp_comp_pdu(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* DMR_PDU) {
    UNUSED(opts);
    UNUSED(state);
    uint16_t ipid = (DMR_PDU[0] << 8) | DMR_PDU[1];
    uint16_t said = (DMR_PDU[2] >> 4) & 0xF;
    uint16_t daid = (DMR_PDU[2] >> 0) & 0xF;

    //the manual shows this is the lsb and msb of the header compression 'opcode', but only zero is defined
    uint8_t op1 = (DMR_PDU[3] >> 7) & 1;
    uint8_t op2 = (DMR_PDU[4] >> 7) & 1;
    uint8_t opcode = (op1 << 1) | op2;

    uint8_t spid = (DMR_PDU[3] >> 0) & 0x7F;
    uint8_t dpid = (DMR_PDU[4] >> 0) & 0x7F;

    //configure SAID / DAID string
    char addrstring[2][35];
    memset(addrstring, 0, sizeof(addrstring));

    //said
    if (said == 0) {
        sprintf(addrstring[0], "%s", "Radio Network");
    } else if (said == 1) {
        sprintf(addrstring[0], "%s", "Ethernet");
    } else if (said > 1 && said < 11) {
        sprintf(addrstring[0], "%s", "Reserved");
    } else {
        sprintf(addrstring[0], "%s", "Manufacturer Specific");
    }

    //daid
    if (daid == 0) {
        sprintf(addrstring[1], "%s", "Radio Network");
    } else if (daid == 1) {
        sprintf(addrstring[1], "%s", "Ethernet");
    } else if (daid == 1) {
        sprintf(addrstring[1], "%s", "Group Network");
    } else if (daid > 2 && daid < 11) {
        sprintf(addrstring[1], "%s", "Reserved");
    } else {
        sprintf(addrstring[1], "%s", "Manufacturer Specific");
    }

    //according to ETSI, if spid and/or dpid is zero, their respective port is defined in this header, otherwise, they are preset indexed values

    //look for and set optional port values and the ptr value to start of data
    uint16_t ptr = 5;

    if (spid == 0 && dpid == 0) {
        spid = (DMR_PDU[5] << 8) | DMR_PDU[6];
        dpid = (DMR_PDU[7] << 8) | DMR_PDU[8];
        ptr = 9;
    } else if (spid == 0) {
        spid = (DMR_PDU[5] << 8) | DMR_PDU[6];
        ptr = 7;
    } else if (dpid == 0) {
        dpid = (DMR_PDU[5] << 8) | DMR_PDU[6];
        ptr = 7;
    } else {
        ptr = 5;
    }

    char portstring[2][35];
    memset(portstring, 0, sizeof(portstring));

    //spid
    if (spid == 1) {
        // spid = 5016;
        sprintf(portstring[0], "%s", "UTF-16BE Text Message");
    } else if (spid == 2) {
        // spid = 5017;
        sprintf(portstring[0], "%s", "Location Interface Protocol");
    } else if (spid > 2 && spid < 191) {
        // spid = spid;
        sprintf(portstring[0], "%s", "Reserved");
    } else {
        // spid = spid;
        sprintf(portstring[0], "%s", "Manufacturer Specific");
    }

    //dpid
    if (dpid == 1) {
        // dpid = 5016;
        sprintf(portstring[1], "%s", "UTF-16BE Text Message");
    } else if (dpid == 2) {
        // dpid = 5017;
        sprintf(portstring[1], "%s", "Location Interface Protocol");
    } else if (dpid > 2 && dpid < 191) {
        // dpid = dpid;
        sprintf(portstring[1], "%s", "Reserved");
    } else {
        // dpid = dpid;
        sprintf(portstring[1], "%s", "Manufacturer Specific");
    }

    fprintf(stderr, "\n Compressed IP Idx: %d; Opcode: %d; Src Idx: %d (%s); Dst Idx: %d (%s); ", ipid, opcode, said,
            addrstring[0], daid, addrstring[1]);
    fprintf(stderr, "\n Src Port Idx: %d (%s); Dst Port Idx: %d (%s); ", spid, portstring[0], dpid, portstring[1]);

    //sanity check
    if (len > ptr) {
        len -= ptr;
    }

    //decode known types
    if (spid == 1 || dpid == 1) {
        utf16_to_text(state, 1, len, DMR_PDU + ptr); //assumming text starts right at the ptr value
    } else if (spid == 2 || dpid == 2) {
        //untested
        uint8_t bits[127 * 8];
        memset(bits, 0, sizeof(bits));
        unpack_byte_array_into_bit_array(DMR_PDU + ptr, bits, len * sizeof(uint8_t));
        lip_protocol_decoder(opts, state, bits);
    } else {
        fprintf(stderr, "Unknown Decode Format;");
    }

    uint8_t slot = 0;
    if (state->currentslot == 1) {
        slot = 1;
    }

    char comp_string[500];
    memset(comp_string, 0, sizeof(comp_string));
    sprintf(comp_string, "IPC: %d; OP: %d; SRC: %d:%d (%s):(%s); DST: %d:%d (%s):(%s); ", ipid, opcode, said, spid,
            addrstring[0], portstring[0], daid, dpid, addrstring[1], portstring[1]);
    watchdog_event_datacall(opts, state, said, daid, comp_string, slot);
}

//IP PDU header decode and port forward to appropriate decoder
void
decode_ip_pdu(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* input) {

    if (!opts || !state || !input || len < 20) {
        return;
    }

    uint8_t slot = state->currentslot;

    //the IPv4 Header
    uint8_t version = input[0] >> 4; //may need to read ahead and get this value before coming here
    uint8_t ihl = input[0] & 0xF;    //decompressed value is 0x05 (may need to check this before preceeding)
    uint8_t tos = input[1];          //0
    uint16_t tlen =
        (input[2] << 8) | input[3]; //IPv4 header length (20 bytes) + UDP header length (5 bytes) + Packet Data
    uint16_t iden = (input[4] << 8) | input[5];
    uint8_t ipf = input[6] >> 5;                           //0
    uint16_t offset = ((input[6] & 0x1F) << 8) | input[7]; //0
    uint8_t ttl = input[8];                                //should be 0x40 (64)
    uint8_t prot = input[9];
    uint16_t hsum = (input[10] << 8) | input[11];

    // Header length in bytes (IHL is in 32-bit words).
    size_t ip_header_len = (size_t)ihl * 4u;
    if (version != 4 || ihl < 5 || ip_header_len > (size_t)len) {
        return;
    }

    // Clamp to IP total length to ignore padding beyond the IP packet.
    size_t effective_len = (size_t)len;
    if ((size_t)tlen >= ip_header_len && (size_t)tlen <= effective_len) {
        effective_len = (size_t)tlen;
    }

    if (opts->payload == 1) {
        fprintf(stderr,
                "\n IPv%d; IHL: %d; Type of Service: %d; Total Len: %d; IP ID: %04X; Flags: %X;\n Fragment Offset: %d; "
                "TTL: %d; Protocol: 0x%02X; Checksum: %04X; PDU Len: %d;",
                version, ihl, tos, tlen, iden, ipf, offset, ttl, prot, hsum, len);
    }

    //take a look at the src, dst, and port indicated (assuming both ports will match)
    uint32_t src24 = (input[13] << 16) | (input[14] << 8) | input[15];
    uint32_t dst24 = (input[17] << 16) | (input[18] << 8) | input[19];
    uint16_t port1 = 0;
    uint16_t port2 = 0;
    if (prot == 0x11 && effective_len >= ip_header_len + 8u) {
        port1 = (uint16_t)((input[ip_header_len + 0] << 8) | input[ip_header_len + 1]);
        port2 = (uint16_t)((input[ip_header_len + 2] << 8) | input[ip_header_len + 3]);
    }
    fprintf(stderr, "\n SRC(24): %08d; IP: %03d.%03d.%03d.%03d; ", src24, input[12], input[13], input[14], input[15]);
    if (prot == 0x11) {
        fprintf(stderr, "Port: %04d; ", port1);
    }
    fprintf(stderr, "\n DST(24): %08d; IP: %03d.%03d.%03d.%03d; ", dst24, input[16], input[17], input[18], input[19]);
    if (prot == 0x11) {
        fprintf(stderr, "Port: %04d; ", port2);
    }

    //IP Protocol List: https://en.wikipedia.org/wiki/List_of_IP_protocol_numbers
    if (prot == 0x01) //ICMP
    {
        if (effective_len < ip_header_len + 4u) {
            return;
        }

        uint8_t icmp_type = input[ip_header_len + 0];
        uint8_t icmp_code = input[ip_header_len + 1];
        uint16_t icmp_chk = (uint16_t)((input[ip_header_len + 2] << 8) | input[ip_header_len + 3]);
        fprintf(stderr, "\n ICMP Protocol; Type: %02X; Code: %02X; Checksum: %02X;", icmp_type, icmp_code, icmp_chk);
        if (icmp_type == 3) {
            fprintf(stderr, " Destination");
            if (icmp_code == 0) {
                fprintf(stderr, " Network");
            } else if (icmp_code == 1) {
                fprintf(stderr, " Host");
            } else if (icmp_code == 2) {
                fprintf(stderr, " Protocol");
            } else if (icmp_code == 3) {
                fprintf(stderr, " Port");
            }
            fprintf(stderr, " Unreachable;");
        }
        //see: https://en.wikipedia.org/wiki/Internet_Control_Message_Protocol
        //look at attached message, if present
        size_t attached_off = ip_header_len + 8u;
        if (effective_len > attached_off && input[attached_off] == 0x45) //if another chained IPv4 header and/or message
        {
            fprintf(stderr, "\n ------------Attached Message-------------");
            size_t rem = effective_len - attached_off;
            if (rem > UINT16_MAX) {
                rem = UINT16_MAX;
            }
            decode_ip_pdu(opts, state, (uint16_t)rem, input + attached_off);
        }

    }

    else if (prot == 0x11) //UDP
    {
        if (effective_len < ip_header_len + 8u) {
            sprintf(state->dmr_lrrp_gps[slot], "Truncated UDP;");
            watchdog_event_datacall(opts, state, src24, dst24, state->dmr_lrrp_gps[slot], slot);
            return;
        }

        port1 = (uint16_t)((input[ip_header_len + 0] << 8) | input[ip_header_len + 1]);
        port2 = (uint16_t)((input[ip_header_len + 2] << 8) | input[ip_header_len + 3]);
        uint16_t udp_len =
            (uint16_t)((input[ip_header_len + 4] << 8)
                       | input
                           [ip_header_len
                            + 5]); //This UDP Length information element is the length in bytes of this user datagram including this header and the application data (no IP header)
        uint16_t udp_chk = (uint16_t)((input[ip_header_len + 6] << 8) | input[ip_header_len + 7]);
        fprintf(stderr, "\n UDP Protocol; Datagram Len: %d; UDP Checksum: %04X; ", udp_len, udp_chk);

        //if dst port and src prt don't match, then make it so
        if (port2 != port1) {
            port1 = port2;
        }

        size_t udp_payload_off = ip_header_len + 8u;
        size_t udp_payload_len = 0;
        if (udp_len >= 8u) {
            udp_payload_len = (size_t)udp_len - 8u;
        }
        size_t max_payload_len = (effective_len > udp_payload_off) ? (effective_len - udp_payload_off) : 0u;
        if (udp_payload_len > max_payload_len) {
            udp_payload_len = max_payload_len;
        }
        uint16_t payload_len = (udp_payload_len > UINT16_MAX) ? UINT16_MAX : (uint16_t)udp_payload_len;
        uint8_t* payload = input + udp_payload_off;

        if (port1 == 231 && port2 == 231) {
            fprintf(stderr, "Cellocator;");
            sprintf(state->dmr_lrrp_gps[slot], "Cellocator SRC: %d; DST: %d;", src24, dst24);
            if (payload_len > 0) {
                decode_cellocator(opts, state, payload, (int)payload_len);
            }
        } else if (port1 == 4001 && port2 == 4001) {
            fprintf(stderr, "LRRP;");
            dmr_lrrp(opts, state, payload_len, src24, dst24, payload, 1);
            state->event_history_s[slot].Event_History_Items[0].color_pair =
                4; //Remus, add this line to a decode to change its line color
        } else if (port1 == 4004 && port2 == 4004) {
            fprintf(stderr, "XCMP;");
            sprintf(state->dmr_lrrp_gps[slot], "XCMP SRC: %d; DST: %d;", src24, dst24);
            state->event_history_s[slot].Event_History_Items[0].color_pair =
                4; //Remus, add this line to a decode to change its line color
        } else if (port1 == 4005 && port2 == 4005) {
            fprintf(stderr, "ARS;");
            //TODO: ARS Decoder
            sprintf(state->dmr_lrrp_gps[slot], "ARS SRC: %d; DST: %d; ", src24, dst24);
            uint16_t ars_len = (payload_len < 10) ? payload_len : 10;
            utf8_to_text(state, 0, ars_len, payload); //seen some ARS radio IDs in ASCII/ISO7/UTF8 format here
        } else if (port1 == 4007 && port2 == 4007) {
            int tms_len = 0;
            if (payload_len >= 2) {
                tms_len = (payload[0] << 8) | payload[1];
            }
            fprintf(stderr, " TMS ");
            fprintf(stderr, "Len: %d; ", tms_len);

            //loosely based on information found here: https://github.com/OK-DMR/ok-dmrlib/blob/master/okdmr/dmrlib/motorola/text_messaging_service.py
            //look at header and any optional values (simplified version)
            int tms_ptr = 2;
            uint8_t tms_hdr = payload[tms_ptr++]; //first header
            uint8_t tms_ack = (tms_hdr >> 0) & 0xF;
            if (opts->payload == 1) {
                fprintf(stderr, "HDR: %02X; ", tms_hdr);
            }

            //optional address len and address value
            uint8_t tms_adl = payload[tms_ptr++];
            if (tms_adl != 0) {
                //the encoding seems to start at the adl (len) byte, but does not include it
                //so, to get the decoder to work, we well go back one byte and zero it out
                tms_ptr--; //back up one position
                payload[tms_ptr] = 0;
                fprintf(stderr, "Address Len: %d; Address: ", tms_adl);
                utf16_to_text(state, 1, tms_adl - 4,
                              payload + tms_ptr); //addresses seem to have an extra .4 value on end (4 octets)
                payload[tms_ptr] = tms_adl;       //restore this byte
                tms_ptr += tms_adl;               //the len value seems to include the len byte
                tms_ptr += 1;                     //advance the ptr back to negate the negation
                fprintf(stderr, "; ");
            }

            //any additional headers (don't care)
            uint8_t tms_more = payload[tms_ptr] >> 7;
            while (tms_more) {
                uint8_t tms_b1 = payload[tms_ptr++];
                uint8_t tms_b2 = payload[tms_ptr];
                if (opts->payload == 1) {
                    fprintf(stderr, "B1: %02X; B2: %02X; ", tms_b1, tms_b2);
                }
                tms_more = tms_b1 >> 7;
                if (tms_more) {
                    tms_ptr++;
                }
            }

            sprintf(state->dmr_lrrp_gps[slot], "TMS SRC: %d; DST: %d; ", src24, dst24);
            if (tms_ack == 0) {
                //sanity check, see if the ptr is odd (start always seems to be an odd value)
                if (!tms_ptr % 1) {
                    tms_ptr++;
                }

                //sanity check on tms_len at this point (fix offset for below)
                if (tms_len > 3) {
                    tms_len -= (tms_ptr - 3);
                }

                //the first utf16 char seems to be encoded as XXYY where XX is not part of the
                //encoding for the character, so zero that byte out and restore it later
                tms_ptr -= 2; //back up two positions
                uint8_t temp = payload[tms_ptr];
                payload[tms_ptr] = 0;

                //debug
                if (opts->payload == 1) {
                    fprintf(stderr, "Ptr: %d; Len: %d;", tms_ptr, tms_len);
                }
                fprintf(stderr, "\n Text: ");
                utf16_to_text(state, 1, tms_len, payload + tms_ptr);

                payload[tms_ptr] = temp; //restore byte
            } else {
                dsd_append(state->dmr_lrrp_gps[slot], sizeof state->dmr_lrrp_gps[slot], "Acknowledgment;");
                fprintf(stderr, "Acknowledgment;");
            }
        } else if (port1 == 4008 && port2 == 4008) {
            fprintf(stderr, "Telemetry;");
            sprintf(state->dmr_lrrp_gps[slot], "Telemetry SRC: %d; DST: %d;", src24, dst24);
        } else if (port1 == 4009 && port2 == 4009) {
            fprintf(stderr, "OTAP;");
            sprintf(state->dmr_lrrp_gps[slot], "OTAP SRC: %d; DST: %d;", src24, dst24);
        } else if (port1 == 4012 && port2 == 4012) {
            fprintf(stderr, "Battery Management;");
            sprintf(state->dmr_lrrp_gps[slot], "Batt. Man. SRC: %d; DST: %d;", src24, dst24);
        } else if (port1 == 4013 && port2 == 4013) {
            fprintf(stderr, "Job Ticket Server;");
            sprintf(state->dmr_lrrp_gps[slot], "JTS SRC: %d; DST: %d;", src24, dst24);
        } else if (port1 == 4069 && port2 == 4069) {
            //https://trbonet.com/kb/how-to-configure-dt500-and-mobile-radio-to-work-with-scada-sensors/
            fprintf(stderr, "TRBOnet SCADA;");
            sprintf(state->dmr_lrrp_gps[slot], "SCADA SRC: %d; DST: %d;", src24, dst24);
        }
        //ETSI specific -- unknown entry value, assuming +28
        else if (port1 == 5016 && port2 == 5016) {
            fprintf(stderr, "ETSI TMS;");
            sprintf(state->dmr_lrrp_gps[slot], "ETSI TMS SRC: %d; DST: %d; ", src24, dst24);
            utf16_to_text(state, 1, payload_len, payload);
        } else if (port1 == 5017 && port2 == 5017) {
            uint8_t bits[127 * 12 * 8];
            memset(bits, 0, sizeof(bits));
            unpack_byte_array_into_bit_array(payload, bits, payload_len * sizeof(uint8_t));
            lip_protocol_decoder(opts, state, bits);
        }
        //known P25 Ports
        else if (port1 == 49198 && port2 == 49198) {
            sprintf(state->dmr_lrrp_gps[slot], "P25 Tier 2 LOCN SRC(IP): %d.%d.%d.%d; DST(IP): %d.%d.%d.%d; ",
                    input[12], input[13], input[14], input[15], input[16], input[17], input[18], input[19]);
            fprintf(stderr, "P25 Tier 2 Location Service;"); //LRRP
            dmr_lrrp(opts, state, payload_len, src24, dst24, payload, 1);
        } else {
            sprintf(state->dmr_lrrp_gps[slot], "IP SRC: %d.%d.%d.%d:%d; DST: %d.%d.%d.%d:%d; Unknown UDP Port;",
                    input[12], input[13], input[14], input[15], port1, input[16], input[17], input[18], input[19],
                    port2);
            fprintf(stderr, "Unknown UDP Port;");
            // if (len > 28) //default catch all (debug only)
            //   utf8_to_text(state, 0, len-28, input+28);
            // else utf8_to_text(state, 0, len, input+28);
        }

    }

    else {
        sprintf(state->dmr_lrrp_gps[slot], "IP SRC: %d.%d.%d.%d; DST: %d.%d.%d.%d; Unknown IP Protocol: %d; ",
                input[12], input[13], input[14], input[15], input[16], input[17], input[18], input[19], prot);
        fprintf(stderr, "Unknown IP Protocol: %02X;", prot);
        // if (len > 28) //default catch all (debug only)
        //   utf8_to_text(state, 0, len-28, input+28);
        // else utf8_to_text(state, 0, len, input+28);
    }

    watchdog_event_datacall(opts, state, src24, dst24, state->dmr_lrrp_gps[slot], slot);
}

typedef struct {
    // decoded time (token 0x34)
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;

    // decoded position (point/circle 2D/3D)
    uint32_t lat_raw;
    uint32_t lon_raw;
    uint16_t rad_raw;
    uint32_t alt_raw;
    uint16_t alt_acc_raw;
    uint8_t have_pos;
    uint8_t have_rad;
    uint8_t have_alt;
    uint8_t have_alt_acc;
    // TokenType order for position tokens: CIRCLE_2D, CIRCLE_3D, POINT_2D, POINT_3D.
    uint8_t pos_best_rank;

    // speed/heading
    double velocity_mph; // units are 1/100 mph per SDRTrunk Speed.java
    uint8_t vel_set;
    uint16_t heading_deg; // degrees, 2-degree increments per SDRTrunk Heading.java
    uint8_t heading_set;

    // parser quality metrics
    int known_tokens;
    int unknown_tokens;
    int truncated_tokens;
} dmr_lrrp_parse_result;

static void
dmr_lrrp_parse_result_init(dmr_lrrp_parse_result* r) {
    memset(r, 0, sizeof(*r));
    r->pos_best_rank = 0xFFu;
}

static void
dmr_lrrp_parse_response_tokens(const uint8_t* pdu, size_t avail, size_t idx_start, size_t remaining,
                               dmr_lrrp_parse_result* r) {
    size_t idx = idx_start;
    while (remaining > 0 && idx < avail) {
        uint8_t token = pdu[idx];
        size_t need = 1;
        int known = 1;

        switch (token) {
            case 0x22: { // IDENTITY (variable-length; includes payload length byte)
                if (remaining < 2) {
                    need = remaining;
                    r->truncated_tokens++;
                    break;
                }
                size_t payload = (size_t)pdu[idx + 1];
                need = 2u + payload;
                if (need > remaining) {
                    need = remaining;
                    r->truncated_tokens++;
                }
            } break;

            case 0x23: // UNKNOWN_23 (len 1)
            case 0x31: // TRIGGER_PERIODIC (len 1)
            case 0x4A: // TRIGGER_DISTANCE (len 1)
            case 0x78: // TRIGGER_ON_MOVE (len 1)
            case 0x61: // REQUEST_61 (len 1)
            case 0x73: // REQUEST_73 (len 1)
                need = (remaining >= 2) ? 2u : remaining;
                if (need != 2u) {
                    r->truncated_tokens++;
                }
                break;

            case 0x42: // TRIGGER_GPIO (len 0)
            case 0x3A: // REQUEST_3A (len 0)
            case 0x50: // ALTITUDE_ACCURACY (len 0, unimplemented in SDRTrunk)
            case 0x52: // TIME (len 0, unimplemented in SDRTrunk)
            case 0x54: // ALTITUDE (len 0, unimplemented in SDRTrunk)
            case 0x57: // HORIZONTAL_DIRECTION (len 0, unimplemented in SDRTrunk)
            case 0x62: // REQUEST_62 (len 0)
            case 0x64: // REQUEST_64 (len 0)
            case 0x38: // SUCCESS (len 0)
                need = 1;
                break;

            case 0x34: // TIMESTAMP (len 5)
                need = (remaining >= 6) ? 6u : remaining;
                if (need != 6u) {
                    r->truncated_tokens++;
                }
                if (need == 6u && r->year == 0) {
                    r->year = (uint16_t)((pdu[idx + 1] << 6) + (pdu[idx + 2] >> 2));
                    r->month = (uint16_t)(((pdu[idx + 2] & 0x3) << 2) + ((pdu[idx + 3] & 0xC0) >> 6));
                    r->day = (uint16_t)(((pdu[idx + 3] & 0x3E) >> 1));
                    r->hour = (uint16_t)(((pdu[idx + 3] & 0x01) << 4) + ((pdu[idx + 4] & 0xF0) >> 4));
                    r->minute = (uint16_t)(((pdu[idx + 4] & 0x0F) << 2) + ((pdu[idx + 5] & 0xC0) >> 6));
                    r->second = (uint16_t)((pdu[idx + 5] & 0x3F));

                    // Validate decoded timestamp; ignore if out-of-range to avoid bogus decodes.
                    int valid = 1;
                    if (!(r->month >= 1 && r->month <= 12)) {
                        valid = 0;
                    }
                    if (!(r->day >= 1 && r->day <= 31)) {
                        valid = 0;
                    }
                    if (!(r->hour <= 23 && r->minute <= 59 && r->second <= 59)) {
                        valid = 0;
                    }
                    // Accept only years in [2000, 2037] to avoid epoch edge cases and bogus decodes.
                    if (!(r->year >= 2000 && r->year <= 2037)) {
                        valid = 0;
                    }
                    if (!valid) {
                        r->year = r->month = r->day = r->hour = r->minute = r->second = 0;
                    }
                }
                break;

            case 0x36: // VERSION (len 1)
                need = (remaining >= 2) ? 2u : remaining;
                if (need != 2u) {
                    r->truncated_tokens++;
                }
                break;

            case 0x37: { // RESPONSE (2 or 3 bytes)
                if (remaining < 2) {
                    need = remaining;
                    r->truncated_tokens++;
                    break;
                }
                int extended = (pdu[idx + 1] & 0x80) ? 1 : 0;
                size_t full = extended ? 3u : 2u;
                need = (remaining >= full) ? full : remaining;
                if (need != full) {
                    r->truncated_tokens++;
                }
            } break;

            case 0x51: { // CIRCLE_2D (len 10)
                size_t full = 11u;
                need = (remaining >= full) ? full : remaining;
                if (need != full) {
                    r->truncated_tokens++;
                    break;
                }
                if (r->pos_best_rank > 0u) {
                    r->pos_best_rank = 0u;
                    r->lat_raw = ((uint32_t)pdu[idx + 1] << 24) | ((uint32_t)pdu[idx + 2] << 16)
                                 | ((uint32_t)pdu[idx + 3] << 8) | (uint32_t)pdu[idx + 4];
                    r->lon_raw = ((uint32_t)pdu[idx + 5] << 24) | ((uint32_t)pdu[idx + 6] << 16)
                                 | ((uint32_t)pdu[idx + 7] << 8) | (uint32_t)pdu[idx + 8];
                    r->rad_raw = ((uint16_t)pdu[idx + 9] << 8) | (uint16_t)pdu[idx + 10];
                    r->alt_raw = 0;
                    r->alt_acc_raw = 0;
                    r->have_pos = 1;
                    r->have_rad = 1;
                    r->have_alt = 0;
                    r->have_alt_acc = 0;
                }
            } break;

            case 0x55: { // CIRCLE_3D (len 15; trailing byte ignored)
                size_t full = 16u;
                need = (remaining >= full) ? full : remaining;
                if (need != full) {
                    r->truncated_tokens++;
                    break;
                }
                if (r->pos_best_rank > 1u) {
                    r->pos_best_rank = 1u;
                    r->lat_raw = ((uint32_t)pdu[idx + 1] << 24) | ((uint32_t)pdu[idx + 2] << 16)
                                 | ((uint32_t)pdu[idx + 3] << 8) | (uint32_t)pdu[idx + 4];
                    r->lon_raw = ((uint32_t)pdu[idx + 5] << 24) | ((uint32_t)pdu[idx + 6] << 16)
                                 | ((uint32_t)pdu[idx + 7] << 8) | (uint32_t)pdu[idx + 8];
                    r->rad_raw = ((uint16_t)pdu[idx + 9] << 8) | (uint16_t)pdu[idx + 10];
                    r->alt_raw = ((uint16_t)pdu[idx + 11] << 8) | (uint16_t)pdu[idx + 12];
                    r->alt_acc_raw = ((uint16_t)pdu[idx + 13] << 8) | (uint16_t)pdu[idx + 14];
                    r->have_pos = 1;
                    r->have_rad = 1;
                    r->have_alt = 1;
                    r->have_alt_acc = 1;
                }
            } break;

            case 0x66: { // POINT_2D (len 8)
                size_t full = 9u;
                need = (remaining >= full) ? full : remaining;
                if (need != full) {
                    r->truncated_tokens++;
                    break;
                }
                if (r->pos_best_rank > 2u) {
                    r->pos_best_rank = 2u;
                    r->lat_raw = ((uint32_t)pdu[idx + 1] << 24) | ((uint32_t)pdu[idx + 2] << 16)
                                 | ((uint32_t)pdu[idx + 3] << 8) | (uint32_t)pdu[idx + 4];
                    r->lon_raw = ((uint32_t)pdu[idx + 5] << 24) | ((uint32_t)pdu[idx + 6] << 16)
                                 | ((uint32_t)pdu[idx + 7] << 8) | (uint32_t)pdu[idx + 8];
                    r->rad_raw = 0;
                    r->alt_raw = 0;
                    r->alt_acc_raw = 0;
                    r->have_pos = 1;
                    r->have_rad = 0;
                    r->have_alt = 0;
                    r->have_alt_acc = 0;
                }
            } break;

            case 0x69: { // POINT_3D (len 11)
                size_t full = 12u;
                need = (remaining >= full) ? full : remaining;
                if (need != full) {
                    r->truncated_tokens++;
                    break;
                }
                if (r->pos_best_rank > 3u) {
                    r->pos_best_rank = 3u;
                    r->lat_raw = ((uint32_t)pdu[idx + 1] << 24) | ((uint32_t)pdu[idx + 2] << 16)
                                 | ((uint32_t)pdu[idx + 3] << 8) | (uint32_t)pdu[idx + 4];
                    r->lon_raw = ((uint32_t)pdu[idx + 5] << 24) | ((uint32_t)pdu[idx + 6] << 16)
                                 | ((uint32_t)pdu[idx + 7] << 8) | (uint32_t)pdu[idx + 8];
                    // altitude is 24-bit per SDRTrunk Point3d.java (bits 72-95)
                    r->alt_raw =
                        ((uint32_t)pdu[idx + 9] << 16) | ((uint32_t)pdu[idx + 10] << 8) | (uint32_t)pdu[idx + 11];
                    r->rad_raw = 0;
                    r->alt_acc_raw = 0;
                    r->have_pos = 1;
                    r->have_rad = 0;
                    r->have_alt = 1;
                    r->have_alt_acc = 0;
                }
            } break;

            case 0x6C: { // SPEED (len 2)
                size_t full = 3u;
                need = (remaining >= full) ? full : remaining;
                if (need != full) {
                    r->truncated_tokens++;
                    break;
                }
                if (!r->vel_set) {
                    r->velocity_mph = ((double)(((uint16_t)pdu[idx + 1] << 8) | (uint16_t)pdu[idx + 2])) * 0.01;
                    r->vel_set = 1;
                }
            } break;

            case 0x56: { // HEADING (len 1), 2-degree increments
                size_t full = 2u;
                need = (remaining >= full) ? full : remaining;
                if (need != full) {
                    r->truncated_tokens++;
                    break;
                }
                if (!r->heading_set) {
                    r->heading_deg = (uint16_t)pdu[idx + 1] * 2u;
                    r->heading_set = 1;
                }
            } break;

            default:
                known = 0;
                need = 1;
                break;
        }

        if (known) {
            r->known_tokens++;
        } else {
            r->unknown_tokens++;
        }

        idx += need;
        remaining -= need;
    }
}

static int
dmr_lrrp_parse_score(const dmr_lrrp_parse_result* r, size_t prefix_skip) {
    int score = 0;
    score -= (int)prefix_skip * 5;
    score += r->known_tokens * 10;
    score -= r->unknown_tokens;
    score -= r->truncated_tokens * 50;

    if (r->have_pos) {
        score += 1000 - ((int)r->pos_best_rank * 10);
        // Penalize (0,0) which often shows up as a desync / bogus decode.
        if (r->lat_raw == 0u && r->lon_raw == 0u) {
            score -= 200;
        }
    }
    if (r->year) {
        score += 100;
    }
    if (r->vel_set) {
        score += 50;
    }
    if (r->heading_set) {
        score += 50;
    }
    if (r->have_rad) {
        score += 20;
    }
    if (r->have_alt) {
        score += 20;
    }
    if (r->have_alt_acc) {
        score += 20;
    }

    return score;
}

//The contents of this function are mostly trial and error
void
dmr_lrrp(dsd_opts* opts, dsd_state* state, uint16_t len, uint32_t source, uint32_t dest, uint8_t* DMR_PDU,
         uint8_t pdu_crc_ok) {

    uint8_t slot = state->currentslot;
    if (!DMR_PDU || len < 2) {
        return;
    }

    // Match SDRTrunk: header is 2 bytes (type + payload length), tokens start at offset +2.
    uint8_t lrrp_type = DMR_PDU[0];
    uint8_t payload_len = DMR_PDU[1];

    uint8_t is_request = 0;
    uint8_t is_response = 0;
    switch (lrrp_type) {
        case 0x0F: // Triggered Location Stop Request
        case 0x05: // Immediate Location Request
        case 0x09: // Triggered Location Start Request
        case 0x14: // Protocol Version Request
            is_request = 1;
            break;
        case 0x07: // Immediate Location Response
        case 0x0B: // Triggered Location Start Response
        case 0x0D: // Triggered Location
        case 0x11: // Triggered Location Stop Response
        case 0x15: // Protocol Version Response
            is_response = 1;
            break;
        default: break;
    }

    dmr_lrrp_parse_result best;
    dmr_lrrp_parse_result_init(&best);

    // Token parsing bounds: clamp to available bytes so malformed/truncated packets can't overrun.
    size_t avail = (size_t)len;
    size_t token_avail = (avail > 2u) ? (avail - 2u) : 0u;
    size_t token_len = (size_t)payload_len;
    if (token_len > token_avail) {
        token_len = token_avail;
    }

    int want_response_parse = is_response;
    if (!want_response_parse && !is_request && token_len > 0) {
        // Some real-world packets have nonstandard type values; treat as a response only if a position token is present.
        for (size_t i = 0; i < token_len && (2u + i) < avail; i++) {
            uint8_t b = DMR_PDU[2u + i];
            if (b == 0x51 || b == 0x55 || b == 0x66 || b == 0x69) {
                want_response_parse = 1;
                break;
            }
        }
    }

    if (want_response_parse) {
        // Resync: some packets include prefix bytes that can masquerade as token ids and desync parsing.
        const size_t max_skip = 6u;
        int best_score = -1000000;
        for (size_t skip = 0; skip <= max_skip && skip <= token_len; skip++) {
            dmr_lrrp_parse_result cur;
            dmr_lrrp_parse_result_init(&cur);
            dmr_lrrp_parse_response_tokens(DMR_PDU, avail, 2u + skip, token_len - skip, &cur);
            int score = dmr_lrrp_parse_score(&cur, skip);
            if (score > best_score) {
                best_score = score;
                best = cur;
            }
        }
    }

    // Establish SRC if not provided in the LRRP wrapper.
    if (!source) {
        source = (uint32_t)state->dmr_lrrp_source[state->currentslot];
    }

    // Compute scaled values (matches SDRTrunk Point2d/Speed/Heading conversions).
    double lat_fin = 0.0;
    double lon_fin = 0.0;
    double rad_fin = 0.0;
    double alt_fin = 0.0;
    double alt_acc_fin = 0.0;
    if (best.have_pos) {
        // Two's complement signed 32-bit for both coordinates.
        // Formula: lat = raw * 90 / 2^31, lon = raw * 180 / 2^31
        // Reference: RadioReference forum, ok-dmrlib
        lat_fin = ((double)((int32_t)best.lat_raw) * 90.0) / 2147483648.0;
        lon_fin = ((double)((int32_t)best.lon_raw) * 180.0) / 2147483648.0;
    }
    if (best.have_rad) {
        rad_fin = (double)best.rad_raw * 0.01;
    }
    if (best.have_alt) {
        alt_fin = (double)best.alt_raw * 0.01;
    }
    if (best.have_alt_acc) {
        alt_acc_fin = (double)best.alt_acc_raw * 0.01;
    }

    // Emit details (stderr) and write to LRRP mapping/logging file.
    if (payload_len > 0) {
        fprintf(stderr, "%s", KYEL);

        if (best.year) {
            fprintf(stderr, "\n");
            fprintf(stderr, " Time:");
            fprintf(stderr, " %04d.%02d.%02d %02d:%02d:%02d", best.year, best.month, best.day, best.hour, best.minute,
                    best.second);
        }

        if (best.have_pos && pdu_crc_ok) {
            fprintf(stderr, "\n Lat: %.5lf Lon: %.5lf (%.5lf, %.5lf)", lat_fin, lon_fin, lat_fin, lon_fin);
        } else if (best.have_pos && !pdu_crc_ok) {
            fprintf(stderr, "\n Position: (suppressed; CRC ERR)");
        }
        if (best.have_rad) {
            fprintf(stderr, "\n Radius: %.2lfm", rad_fin);
        }
        if (best.have_alt) {
            fprintf(stderr, "\n Altitude: %.2lfm", alt_fin);
        }
        if (best.have_alt_acc) {
            fprintf(stderr, "\n Alt Accuracy: %.2lfm", alt_acc_fin);
        }
        if (best.vel_set) {
            fprintf(stderr, "\n Speed: %.2lf mph %.2lf km/h %.2lf m/s", best.velocity_mph,
                    (best.velocity_mph * 1.60934), (best.velocity_mph * 0.44704));
        }
        if (best.heading_set) {
            const char* deg_glyph = dsd_degrees_glyph();
            fprintf(stderr, "\n Track: %d%s", (int)best.heading_deg, deg_glyph);
        }

        // Write to LRRP file if a lat/lon is present; timestamps always use system time for consistency.
        if (opts->lrrp_file_output == 1 && pdu_crc_ok && lat_fin != 0.0 && lon_fin != 0.0) {
            char timestr[9];
            char datestr[11];
            getTimeC_buf(timestr);
            getDateS_buf(datestr);

            FILE* pFile = fopen(opts->lrrp_out_file, "a");
            if (pFile != NULL) {
                fprintf(pFile, "%s\t%s\t", datestr, timestr);
                fprintf(pFile, "%08d\t", source);
                fprintf(pFile, "%.5lf\t", lat_fin);
                fprintf(pFile, "%.5lf\t", lon_fin);
                fprintf(pFile, "%.3lf\t ", (best.velocity_mph * 1.60934)); // mph -> km/h
                fprintf(pFile, "%d\t", (int)best.heading_deg);
                fprintf(pFile, "\n");
                fclose(pFile);
            }
        }

        // Save to string for ncurses
        char velstr[20];
        char degstr[20];
        char lrrpstr[100];
        sprintf(lrrpstr, "%s", "");
        sprintf(velstr, "%s", "");
        sprintf(degstr, "%s", "");

        if (best.have_pos && pdu_crc_ok) {
            sprintf(lrrpstr, "LRRP SRC: %0d; (%lf, %lf)", source, lat_fin, lon_fin);
        } else if (best.have_pos && !pdu_crc_ok) {
            sprintf(lrrpstr, "LRRP SRC: %0d; Position suppressed (CRC ERR);", source);
        } else if (is_request) {
            sprintf(lrrpstr, "LRRP SRC: %0d; Request from TGT: %d;", source, dest);
        } else if (is_response) {
            sprintf(lrrpstr, "LRRP SRC: %0d; Response to TGT: %d;", source, dest);
        } else {
            sprintf(lrrpstr, "LRRP SRC: %0d; Unknown Format %02X; TGT: %d;", source, lrrp_type, dest);
        }

        if (best.vel_set) {
            sprintf(velstr, " %.2lf km/h", best.velocity_mph * 1.60934);
        }
        if (best.heading_set) {
            const char* deg_glyph = dsd_degrees_glyph();
            sprintf(degstr, " %d%s  ", (int)best.heading_deg, deg_glyph);
        }

        sprintf(state->dmr_lrrp_gps[slot], "%s%s%s", lrrpstr, velstr, degstr);

        if (!best.have_pos || !pdu_crc_ok) {
            fprintf(stderr, "\n %s", state->dmr_lrrp_gps[slot]);
        }

    } else {
        char lrrpstr[100];
        sprintf(lrrpstr, "LRRP SRC: %0d; Unknown Format %02X; TGT: %d;", source, lrrp_type, dest);
        sprintf(state->dmr_lrrp_gps[slot], "%s", lrrpstr);
        fprintf(stderr, "\n %s", state->dmr_lrrp_gps[slot]);
    }

    fprintf(stderr, "%s", KNRM);
}

void
dmr_locn(dsd_opts* opts, dsd_state* state, uint16_t len, uint8_t* DMR_PDU) {

    uint8_t slot = state->currentslot;
    uint8_t source = state->dmr_lrrp_source[slot];

    //flags if certain data type is present
    uint8_t time = 0;
    uint8_t lat = 0;
    uint8_t lon = 0;

    //date-time variables
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;

    //lat and lon variables
    uint16_t lat_deg = 0;
    uint16_t lat_min = 0;
    uint16_t lat_sec = 0;

    uint16_t lon_deg = 0;
    uint16_t lon_min = 0;
    uint16_t lon_sec = 0;

    const char* deg_glyph = dsd_degrees_glyph();

    //more strings...
    char locnstr[50];
    char latstr[75];
    char lonstr[75];
    sprintf(locnstr, "%s", "");
    sprintf(latstr, "%s", "");
    sprintf(lonstr, "%s", "");

    int lat_sign = 1; //positive 1 or negative 1
    int lon_sign = 1; //positive 1 or negative 1

    //start looking for specific bytes corresponding to 'letters' A (time), NSEW (ordinal directions), etc
    for (uint16_t i = 0; i < len; i++) {
        switch (DMR_PDU[i]) {
            case 0x41: //A -- time and date
                time = 1;
                hour = ((DMR_PDU[i + 1] - 0x30) << 4) | (DMR_PDU[i + 2] - 0x30);
                minute = ((DMR_PDU[i + 3] - 0x30) << 4) | (DMR_PDU[i + 4] - 0x30);
                second = ((DMR_PDU[i + 5] - 0x30) << 4) | (DMR_PDU[i + 6] - 0x30);
                // think this is in day, mon, year format (packed BCD nibbles)
                day = ((DMR_PDU[i + 7] - 0x30) << 4) | (DMR_PDU[i + 8] - 0x30);
                month = ((DMR_PDU[i + 9] - 0x30) << 4) | (DMR_PDU[i + 10] - 0x30);
                year = ((DMR_PDU[i + 11] - 0x30) << 4) | (DMR_PDU[i + 12] - 0x30);
                i += 12;
                // Validate BCD fields; if out-of-range, drop to system time fallback later.
                {
                    int yy = ((year >> 4) & 0xF) * 10 + (year & 0xF);
                    int mm = ((month >> 4) & 0xF) * 10 + (month & 0xF);
                    int dd = ((day >> 4) & 0xF) * 10 + (day & 0xF);
                    int hh = ((hour >> 4) & 0xF) * 10 + (hour & 0xF);
                    int mi = ((minute >> 4) & 0xF) * 10 + (minute & 0xF);
                    int ss = ((second >> 4) & 0xF) * 10 + (second & 0xF);
                    int full_year = 2000 + yy;
                    int valid = 1;
                    if (!(mm >= 1 && mm <= 12)) {
                        valid = 0;
                    }
                    if (!(dd >= 1 && dd <= 31)) {
                        valid = 0;
                    }
                    if (!(hh >= 0 && hh <= 23 && mi >= 0 && mi <= 59 && ss >= 0 && ss <= 59)) {
                        valid = 0;
                    }
                    if (!(full_year >= 2000 && full_year <= 2037)) {
                        valid = 0;
                    }
                    if (!valid) {
                        time = 0; // force writer to use system time
                    }
                }
                break;

            case 0x53: //S -- South
                lat_sign = -1;
                /* fall through */
            case 0x4E: //N -- North
                lat = 1;
                lat_deg = ((DMR_PDU[i + 1] - 0x30) << 4) | (DMR_PDU[i + 2] - 0x30);
                lat_min = ((DMR_PDU[i + 3] - 0x30) << 4) | (DMR_PDU[i + 4] - 0x30);
                lat_sec = ((DMR_PDU[i + 6] - 0x30) << 12) | ((DMR_PDU[i + 7] - 0x30) << 8)
                          | ((DMR_PDU[i + 8] - 0x30) << 4) | ((DMR_PDU[i + 9] - 0x30) << 0);
                i += 8;
                break;

            case 0x57: //W -- West
                lon_sign = -1;
                /* fall through */
            case 0x45: //E -- East
                lon = 1;
                lon_deg =
                    ((DMR_PDU[i + 1] - 0x30) << 8) | ((DMR_PDU[i + 2] - 0x30) << 4) | ((DMR_PDU[i + 3] - 0x30) << 0);
                lon_min = ((DMR_PDU[i + 4] - 0x30) << 4) | (DMR_PDU[i + 5] - 0x30);
                lon_sec = ((DMR_PDU[i + 7] - 0x30) << 12) | ((DMR_PDU[i + 8] - 0x30) << 8)
                          | ((DMR_PDU[i + 9] - 0x30) << 4) | ((DMR_PDU[i + 10] - 0x30) << 0);
                i += 8;
                break;

            default:
                //do nothing
                break;

        } //end switch

    } //for i

    if (lat && lon) {
        //convert dd.MMmmmm to decimal format
        double latitude = 0.0f;
        double longitude = 0.0f;
        double velocity = 0.0f;
        uint16_t degrees = 0;

        //convert hex string chars representing decimal values to integers
        lat_deg = convert_hex_to_dec(lat_deg);
        lat_min = convert_hex_to_dec(lat_min);
        lat_sec = convert_hex_to_dec(lat_sec);

        lon_deg = convert_hex_to_dec(lon_deg);
        lon_min = convert_hex_to_dec(lon_min);
        lon_sec = convert_hex_to_dec(lon_sec);

        //dd.MMmmmm to decimal formatting
        latitude = (double)lat_sign
                   * ((double)lat_deg + ((double)lat_min / (double)60.0f) + ((double)lat_sec / (double)600000.0f));
        longitude = (double)lon_sign
                    * ((double)lon_deg + ((double)lon_min / (double)60.0f) + ((double)lon_sec / (double)600000.0f));

        fprintf(stderr, "%s", KYEL);
        fprintf(stderr, "\n NMEA / LOCN; Source: %d;", source);
        if (time) {
            fprintf(stderr, " 20%02X/%02X/%02X %02X:%02X:%02X", year, month, day, hour, minute, second);
        }
        fprintf(stderr, " (%.5lf%s, %.5lf%s);", latitude, deg_glyph, longitude, deg_glyph);

        //string manip for ncurses terminal display
        sprintf(locnstr, "NMEA / LOCN; Source: %d ", source);
        sprintf(latstr, "(%.5lf%s, ", latitude, deg_glyph);
        sprintf(lonstr, "%.5lf%s)", longitude, deg_glyph);
        sprintf(state->dmr_lrrp_gps[slot], "%s%s%s", locnstr, latstr, lonstr);

        //write to LRRP file
        if (opts->lrrp_file_output == 1) {
            char timestr[9];
            char datestr[11];
            getTimeC_buf(timestr);
            getDateS_buf(datestr);

            //open file by name that is supplied in the ncurses terminal, or cli
            FILE* pFile; //file pointer
            pFile = fopen(opts->lrrp_out_file, "a");
            if (pFile != NULL) {
                //LOCN LRRP timestamps always use system time for consistency
                fprintf(pFile, "%s\t%s\t", datestr, timestr);

                //write data header source from data header
                fprintf(pFile, "%08lld\t", state->dmr_lrrp_source[state->currentslot]);

                /* stack buffers; no free */

                fprintf(pFile, "%.5lf\t", latitude);
                fprintf(pFile, "%.5lf\t", longitude);
                fprintf(pFile, "%.3lf\t ", (velocity * 3.6));
                fprintf(pFile, "%d\t", degrees);

                fprintf(pFile, "\n");
                fclose(pFile);
            }
        }
    }
}

static inline void
dsd_append(char* dst, size_t dstsz, const char* src) {
    if (!dst || !src || dstsz == 0) {
        return;
    }
    size_t len = strlen(dst);
    if (len >= dstsz) {
        return;
    }
    snprintf(dst + len, dstsz - len, "%s", src);
}
