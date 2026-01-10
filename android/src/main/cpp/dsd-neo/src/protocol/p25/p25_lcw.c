// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*-------------------------------------------------------------------------------
 * p25_lcw.c
 * P25p1 Link Control Word Decoding
 *
 * LWVMOBILE
 * 2023-05 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/embedded_alias.h>
#include <dsd-neo/core/gps.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/dmr/dmr_utils_api.h>
#include <dsd-neo/protocol/p25/p25_callsign.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>
#include <dsd-neo/protocol/p25/p25_lcw.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/colors.h>
#include <dsd-neo/runtime/unicode.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

// Bounded append helper
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

//new p25_lcw function here -- TIA-102.AABF-D LCW Format Messages (if anybody wants to fill the rest out)
void
p25_lcw(dsd_opts* opts, dsd_state* state, uint8_t LCW_bits[], uint8_t irrecoverable_errors) {
    UNUSED(irrecoverable_errors);

    uint8_t lc_format = (uint8_t)ConvertBitIntoBytes(&LCW_bits[0], 8);  //format
    uint8_t lc_opcode = (uint8_t)ConvertBitIntoBytes(&LCW_bits[2], 6);  //opcode portion of format
    uint8_t lc_mfid = (uint8_t)ConvertBitIntoBytes(&LCW_bits[8], 8);    //mfid
    uint8_t lc_svcopt = (uint8_t)ConvertBitIntoBytes(&LCW_bits[16], 8); //service options
    uint8_t lc_pf = LCW_bits[0];                                        //protect flag
    uint8_t lc_sf = LCW_bits[1]; // Implicit / Explicit MFID Format (SF bit in PB/SF/LCO)
    int mfid_is_implicit = (lc_sf == 1);
    int is_standard_mfid = mfid_is_implicit || lc_mfid == 0 || lc_mfid == 1;

    if (lc_pf == 1) //check the protect flag -- if set, its an encrypted lcw
    {
        fprintf(stderr, "%s", KRED);
        fprintf(stderr, " LCW Protected ");
        fprintf(stderr, "%s", KNRM);
    }

    if (lc_pf == 0) //not protected/encrypted lcw
    {

        if (opts->payload == 1) {
            fprintf(stderr, " LCW");
        }

        // Standard MFID: explicit MFID=0/1, or implicit MFID (SF=1)
        if (is_standard_mfid) // explicit MFID==0/1, or implicit MFID (SF=1)
        {

            //check the service options on applicable formats
            if (lc_format == 0x4A || lc_format == 0x46 || lc_format == 0x45 || lc_format == 0x44 || lc_format == 0x03
                || lc_format == 0x00) {

                if (lc_svcopt & 0x80) {
                    fprintf(stderr, " Emergency");
                }
                if (lc_svcopt & 0x40) {
                    fprintf(stderr, " Encrypted");
                }

                if (opts->payload == 1) //hide behind payload due to len
                {
                    if (lc_svcopt & 0x20) {
                        fprintf(stderr, " Duplex");
                    }
                    if (lc_svcopt & 0x10) {
                        fprintf(stderr, " Packet");
                    } else {
                        fprintf(stderr, " Circuit");
                    }
                    if (lc_svcopt & 0x8) {
                        fprintf(stderr, " R"); //reserved bit is on
                    }
                    fprintf(stderr, " Priority %d", lc_svcopt & 0x7); //call priority
                }
            }

            if (lc_format == 0x00) {
                fprintf(stderr, " Group Voice Channel User");
                uint8_t res = (uint8_t)ConvertBitIntoBytes(&LCW_bits[24], 7);
                uint8_t explicit = LCW_bits[24]; //explicit source id required == 1, full SUID on seperate LCW
                uint16_t group = (uint16_t)ConvertBitIntoBytes(&LCW_bits[32], 16);
                uint32_t source = (uint32_t)ConvertBitIntoBytes(&LCW_bits[48], 24);
                fprintf(stderr, " - Group %d Source %d", group, source);
                UNUSED2(res, explicit);
                state->gi[0] = 0;
                state->dmr_so = lc_svcopt; //test to make sure no random issues

                //don't set this when zero, annoying blink occurs in ncurses
                if (group != 0) {
                    state->lasttg = group;
                }
                // if (source != 0) //disable now with new event history, if same src next ptt, then it will capture all of them as individual event items
                state->lastsrc = source;
                // Clear alias at start/update of talker for this call (don’t reuse across calls)
                state->generic_talker_alias[0][0] = '\0';
                state->generic_talker_alias_src[0] = 0;

                // Track RID↔TG observation
                if (source != 0 && group != 0) {
                    p25_ga_add(state, (uint32_t)source, (uint16_t)group);
                }

                snprintf(state->call_string[0], sizeof state->call_string[0], "   Group ");
                if (lc_svcopt & 0x80) {
                    dsd_append(state->call_string[0], sizeof state->call_string[0], " Emergency  ");
                } else if (lc_svcopt & 0x40) {
                    dsd_append(state->call_string[0], sizeof state->call_string[0], " Encrypted  ");
                } else {
                    dsd_append(state->call_string[0], sizeof state->call_string[0], "            ");
                }
            }

            else if (lc_format == 0x03) {
                fprintf(stderr, " Unit to Unit Voice Channel User");
                uint32_t target = (uint32_t)ConvertBitIntoBytes(&LCW_bits[24], 24);
                uint32_t source = (uint32_t)ConvertBitIntoBytes(&LCW_bits[48], 24);
                fprintf(stderr, " - Target %d Source %d", target, source);

                //don't set this when zero, annoying blink occurs in ncurses
                if (target != 0) {
                    state->lasttg = target;
                }
                // if (source != 0) //disable now with new event history, if same src next ptt, then it will capture all of them as individual event items
                state->lastsrc = source;
                // Clear alias at start/update of talker for this call (don’t reuse across calls)
                state->generic_talker_alias[0][0] = '\0';
                state->generic_talker_alias_src[0] = 0;
                state->gi[0] = 1;
                state->dmr_so = lc_svcopt;

                snprintf(state->call_string[0], sizeof state->call_string[0], " Private ");
                if (lc_svcopt & 0x80) {
                    dsd_append(state->call_string[0], sizeof state->call_string[0], " Emergency  ");
                } else if (lc_svcopt & 0x40) {
                    dsd_append(state->call_string[0], sizeof state->call_string[0], " Encrypted  ");
                } else {
                    dsd_append(state->call_string[0], sizeof state->call_string[0], "            ");
                }

            }

            //TODO: Allow Tuning from Call Grants either in LDU1 or TDULC? (TDMA to p1 fallback?)
            //TODO: Allow TG Hold overrides here  either in LDU1 or TDULC?
            //NOTE: If we have an active TG hold, we really should't be here anyways
            else if (lc_format == 0x42) //is this group only, or group and private?
            {
                fprintf(stderr, " Group Voice Channel Update - ");
                uint16_t channel1 = (uint16_t)ConvertBitIntoBytes(&LCW_bits[8], 16);
                uint16_t group1 = (uint16_t)ConvertBitIntoBytes(&LCW_bits[24], 16);
                uint16_t channel2 = (uint16_t)ConvertBitIntoBytes(&LCW_bits[40], 16);
                uint16_t group2 = (uint16_t)ConvertBitIntoBytes(&LCW_bits[56], 16);

                if (channel1 && group1) {
                    fprintf(stderr, "Ch: %04X TG: %d; ", channel1, group1);
                    char suf[32];
                    p25_format_chan_suffix(state, channel1, -1, suf, sizeof suf);
                    snprintf(state->active_channel[0], sizeof state->active_channel[0], "Active Ch: %04X%s TG: %d; ",
                             channel1, suf, group1);
                    state->last_active_time = time(NULL);
                }

                if (channel2 && group2 && group1 != group2) {
                    fprintf(stderr, "Ch: %04X TG: %d; ", channel2, group2);
                    char suf[32];
                    p25_format_chan_suffix(state, channel2, -1, suf, sizeof suf);
                    snprintf(state->active_channel[1], sizeof state->active_channel[1], "Active Ch: %04X%s TG: %d; ",
                             channel2, suf, group2);
                    state->last_active_time = time(NULL);
                }

            }

            else if (lc_format == 0x44) {
                fprintf(stderr, " Group Voice Channel Update %s Explicit", dsd_unicode_or_ascii("–", "-"));
                uint16_t group1 = (uint16_t)ConvertBitIntoBytes(&LCW_bits[24], 16);
                uint16_t channelt = (uint16_t)ConvertBitIntoBytes(&LCW_bits[40], 16);
                uint16_t channelr = (uint16_t)ConvertBitIntoBytes(&LCW_bits[56], 16);
                fprintf(stderr, "Ch: %04X TG: %d; ", channelt, group1);
                UNUSED(channelr);

                // Optional, guarded retune from LCW explicit update (format 0x44)
                // Conditions:
                //  - Enabled via opts->p25_lcw_retune
                //  - Trunking mode active and CC known
                //  - Group-call tuning allowed and TG Hold honored
                //  - Encrypted calls skipped unless explicitly allowed
                if (opts->p25_lcw_retune == 1 && opts->p25_trunk == 1 && state->p25_cc_freq != 0) {
                    // Respect group-call tuning policy
                    if (opts->trunk_tune_group_calls == 1) {
                        // TG Hold gating
                        if (state->tg_hold != 0 && state->tg_hold != group1) {
                            // skip retune due to TG hold mismatch
                        } else {
                            // ENC gating from service options, with Harris GRG KEY=0000 override
                            if ((lc_svcopt & 0x40) && opts->trunk_tune_enc_calls == 0
                                && !p25_patch_tg_key_is_clear(state, group1)) {
                                // skip encrypted when not allowed
                            } else {
                                // Dispatch to SM; SM will compute frequency and decide if a tune is appropriate
                                p25_sm_on_group_grant(opts, state, channelt, lc_svcopt, group1, (int)state->lastsrc);
                            }
                        }
                    }
                }
                {
                    //add active channel to string for ncurses display (with FDMA/slot hint)
                    char suf[32];
                    p25_format_chan_suffix(state, channelt, -1, suf, sizeof suf);
                    snprintf(state->active_channel[0], sizeof state->active_channel[0], "Active Ch: %04X%s TG: %d; ",
                             channelt, suf, group1);
                    state->last_active_time = time(NULL);
                }
            }

            else if (lc_format == 0x45) {
                fprintf(stderr, " Unit to Unit Answer Request");
            }

            else if (lc_format == 0x46) {
                fprintf(stderr, " Telephone Interconnect Voice Channel User");
            }

            else if (lc_format == 0x47) {
                fprintf(stderr, " Telephone Interconnect Answer Request");
            }

            else if (lc_format == 0x49) {
                fprintf(stderr, " Source ID Extension -");
                uint32_t nid = (uint32_t)ConvertBitIntoBytes(&LCW_bits[16], 24);
                uint32_t src = (uint32_t)ConvertBitIntoBytes(&LCW_bits[40], 24);
                fprintf(stderr, " Full SUID: %08X-%08d", nid, src);

            }

            else if (lc_format == 0x4A) {
                fprintf(stderr, " Unit to Unit Voice Channel User %s Extended", dsd_unicode_or_ascii("–", "-"));
                uint32_t target = (uint32_t)ConvertBitIntoBytes(&LCW_bits[16], 24);
                uint32_t src = (uint32_t)ConvertBitIntoBytes(&LCW_bits[40], 24);
                fprintf(stderr, "TGT: %d; SRC: %d; ", target, src);
                state->gi[0] = 1;
            }

            else if (lc_format == 0x50) {
                fprintf(stderr, " Group Affiliation Query");
                // Heuristic field mapping: TG at bits[32..47], SRC at bits[48..71]
                uint16_t group = (uint16_t)ConvertBitIntoBytes(&LCW_bits[32], 16);
                uint32_t source = (uint32_t)ConvertBitIntoBytes(&LCW_bits[48], 24);
                if (group) {
                    fprintf(stderr, " - TG %u", group);
                    state->lasttg = group;
                }
                if (source) {
                    fprintf(stderr, " SRC %u", source);
                    state->lastsrc = source;
                }
                if (group && source) {
                    p25_ga_add(state, (uint32_t)source, (uint16_t)group);
                }
            }

            else if (lc_format == 0x51) {
                fprintf(stderr, " Unit Registration Command");
            }

            else if (lc_format == 0x52) //wonder if anybody uses this if its deleted/obsolete
            {
                fprintf(stderr, " Unit Authentication Command - OBSOLETE");
            }

            else if (lc_format == 0x53) {
                fprintf(stderr, " Status Query");
            }

            else if (lc_format == 0x54 || lc_format == 0x55) {
                fprintf(stderr, " Status Update");
            }

            else if (lc_format == 0x56) {
                fprintf(stderr, " Call Alert");
            }

            else if (lc_format == 0x5A) {
                fprintf(stderr, " Status Update %s Source ID Extension Required", dsd_unicode_or_ascii("–", "-"));
            }

            else if (lc_format == 0x5C) {
                fprintf(stderr, " Extended Function Command %s Source ID Extension Required",
                        dsd_unicode_or_ascii("–", "-"));
            }

            else if (lc_format == 0x60) {
                fprintf(stderr, " System Service Broadcast");
            }

            //this PDU does not have an associated MFID, often seen on kiwi and matches its TSBK counterpart
            //its possible some of the other ones here don't as well, need to re-check all of them
            else if (lc_format == 0x61) {
                fprintf(stderr, " Secondary Control Channel Broadcast");
            }

            else if (lc_format == 0x62) {
                fprintf(stderr, " Adjacent Site Status Broadcast");
            }

            else if (lc_format == 0x63) {
                fprintf(stderr, " RFSS Status Broadcast");
            }

            else if (lc_format == 0x64) {
                fprintf(stderr, " Network Status Broadcast");
            }

            else if (lc_format == 0x65) {
                fprintf(stderr, " Protection Parameter Broadcast - OBSOLETE");
            }

            else if (lc_format == 0x66) {
                fprintf(stderr, " Secondary Control Channel Broadcast %s Explicit (LCSCBX)",
                        dsd_unicode_or_ascii("–", "-"));
            }

            else if (lc_format == 0x67) //explicit
            {
                fprintf(stderr, " Adjacent Site Status (LCASBX)");
                uint8_t lra = (uint8_t)ConvertBitIntoBytes(&LCW_bits[8], 8);
                uint16_t channelt = (uint16_t)ConvertBitIntoBytes(&LCW_bits[16], 16);
                uint8_t rfssid = (uint8_t)ConvertBitIntoBytes(&LCW_bits[32], 8);
                uint8_t siteid = (uint8_t)ConvertBitIntoBytes(&LCW_bits[40], 8);
                uint16_t channelr = (uint16_t)ConvertBitIntoBytes(&LCW_bits[48], 16);
                uint8_t cfva = (uint8_t)ConvertBitIntoBytes(&LCW_bits[64], 4);
                fprintf(stderr, " - RFSS %d Site %d CH %04X", rfssid, siteid, channelt);
                UNUSED2(lra, channelr);

                //debug print only
                // fprintf (stderr, "\n  ");
                // fprintf (stderr, "  LRA [%02X] RFSS [%03d] SITE [%03d] CHAN-T [%04X] CHAN-R [%02X] CFVA [%X]\n  ", lra, rfssid, siteid, channelt, channelr, cfva);
                // if (cfva & 0x8) fprintf (stderr, " Conventional");
                // if (cfva & 0x4) fprintf (stderr, " Failure Condition");
                // if (cfva & 0x2) fprintf (stderr, " Up to Date (Correct)");
                // else fprintf (stderr, " Last Known");
                if (cfva & 0x1) {
                    fprintf(stderr, " - Connection Active");
                }
                // process_channel_to_freq (opts, state, channelt);
                //end debug, way too much for a simple link control line

            }

            else if (lc_format == 0x68) {
                fprintf(stderr, " RFSS Status Broadcast %s Explicit (LCRSBX)", dsd_unicode_or_ascii("–", "-"));
            }

            else if (lc_format == 0x69) {
                fprintf(stderr, " Network Status Broadcast %s Explicit (LCNSBX)", dsd_unicode_or_ascii("–", "-"));
            }

            else if (lc_format == 0x6A) {
                fprintf(stderr, " Conventional Fallback");
            }

            else if (lc_format == 0x6B) {
                fprintf(stderr, " Message Update %s Source ID Extension Required", dsd_unicode_or_ascii("–", "-"));
            }

            // Return to control channel (call termination)
            else if (lc_opcode == 0x0F) //# Call Termination/Cancellation
            {
                uint32_t tgt =
                    (uint32_t)ConvertBitIntoBytes(&LCW_bits[48], 24); //can be individual, or all units (0xFFFFFF)
                fprintf(stderr, " Call Termination; TGT: %d;", tgt);
                memset(state->dmr_pdu_sf[0], 0, sizeof(state->dmr_pdu_sf[0]));
                if (opts->p25_trunk == 1 && state->p25_cc_freq != 0 && opts->p25_is_tuned == 1) {
                    // Force release so the SM does not defer due to any stale
                    // per-slot audio gates when an explicit call termination is received.
                    state->p25_sm_force_release = 1;
                    p25_sm_on_release(opts, state);
                }
            }

            // This lc_format doesn't use the MFID field
            else if (lc_format == 0x57) {
                fprintf(stderr, " Extended Function Command");
            }

            // This lc_format doesn't use the MFID field
            else if (lc_format == 0x58) {
                uint8_t iden = (uint8_t)ConvertBitIntoBytes(&LCW_bits[8], 4);
                uint32_t base = (uint32_t)ConvertBitIntoBytes(&LCW_bits[40], 32);
                fprintf(stderr, " Channel Identifier Update VU; Iden: %X; Base: %d;", iden, base * 5);
                if (iden < 16 && base != 0) {
                    uint32_t old = state->p25_base_freq[iden];
                    if (old != base) {
                        state->p25_base_freq[iden] = base; // store in 5 kHz units
                        fprintf(stderr, " (updated)");
                    }
                    // Record provenance for LCW-learned IDENs so trunk SM can enforce site confirmation.
                    // Trust as confirmed only when on current CC; otherwise mark unconfirmed.
                    state->p25_iden_wacn[iden] = state->p2_wacn;
                    state->p25_iden_sysid[iden] = state->p2_sysid;
                    state->p25_iden_rfss[iden] = state->p2_rfssid;
                    state->p25_iden_site[iden] = state->p2_siteid;
                    state->p25_iden_trust[iden] = (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0) ? 2 : 1;
                }
            }

            // This lc_format doesn't use the MFID field
            else if (lc_format == 0x59) {
                uint8_t iden = (uint8_t)ConvertBitIntoBytes(&LCW_bits[8], 4);
                uint32_t base = (uint32_t)ConvertBitIntoBytes(&LCW_bits[40], 32);
                fprintf(stderr, " Channel Identifier Update VU; Iden: %X; Base: %d;", iden, base * 5);
                if (iden < 16 && base != 0) {
                    uint32_t old = state->p25_base_freq[iden];
                    if (old != base) {
                        state->p25_base_freq[iden] = base; // store in 5 kHz units
                        fprintf(stderr, " (updated)");
                    }
                    // Record provenance for LCW-learned IDENs so trunk SM can enforce site confirmation.
                    state->p25_iden_wacn[iden] = state->p2_wacn;
                    state->p25_iden_sysid[iden] = state->p2_sysid;
                    state->p25_iden_rfss[iden] = state->p2_rfssid;
                    state->p25_iden_site[iden] = state->p2_siteid;
                    state->p25_iden_trust[iden] = (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0) ? 2 : 1;
                }
            }

            else {
                fprintf(stderr, " Unknown Format %02X MFID %02X SVC %02X", lc_format, lc_mfid, lc_svcopt);
            }
        }

        //TODO: Look through all LCW format messages and move here if they don't use the MFID field
        //just going to add/fix a few values I've observed for now, one issue with doing so may
        //be where there is a reserved field that is also used as an MFID, i.e., Call Termination 0xF vs Moto Talker EOT 0xF with
        //the reserved field showing the 0x90 for moto in it, and Harris 0xA vs Unit to Unit Voice Call 0xA

        //TODO: Add Identification of Special SU Address values
        //0 - No Unit
        //1-0xFFFFFB - Assignable Units
        //0xFFFFFC - FNE 16777212
        //0xFFFFFD - System Default (FNE Calling Functions, Registration, Mobility)
        //0xFFFFFE - Registration Default (registration transactions from SU)
        //0xFFFFFF - All Units

        //MFID 90 Embedded GPS
        else if (lc_mfid == 0x90 && lc_opcode == 0x6) {
            fprintf(stderr, " MFID90 (Moto)");
            apx_embedded_gps(opts, state, LCW_bits);
        }

        else if (lc_mfid == 0x90 && lc_opcode == 0x0) {
            //needed to fill this in, since tuning this on P1 will just leave the TG/SRC as zeroes
            fprintf(stderr, " MFID90 (Moto) Group Regroup Channel User (LCGRGR)");
            uint32_t sg = (uint32_t)ConvertBitIntoBytes(&LCW_bits[32], 16);
            uint32_t src = (uint32_t)ConvertBitIntoBytes(&LCW_bits[48], 24);
            fprintf(stderr, " SG: %d; SRC: %d;", sg, src);
            if (LCW_bits[16] == 1) {
                fprintf(stderr, " Res;"); //res bit (octet 2)
            }
            if (LCW_bits[17] == 1) {
                fprintf(stderr, " ENC;"); //P-bit (octet 2)
            }
            if (LCW_bits[31] == 1) {
                fprintf(stderr, " EXT;"); //Full SUID next LC (external) (octet 3)
            }
            state->lasttg = sg;
            state->lastsrc = src;
            state->gi[0] = 0;

            // Treat observed Super Group on LCW as an active two-way patch.
            // This drives the UI patches line even when only LCWs are present.
            p25_patch_update(state, (int)sg, /*is_patch*/ 1, /*active*/ 1);
        }

        else if (lc_mfid == 0x90 && lc_opcode == 0x1) {
            fprintf(stderr, " MFID90 (Moto) Group Regroup Channel Update (LCGRGU)");
            uint32_t sg = (uint32_t)ConvertBitIntoBytes(&LCW_bits[24], 16);
            uint32_t ch = (uint32_t)ConvertBitIntoBytes(&LCW_bits[56], 16);
            fprintf(stderr, " SG: %d; CH: %04X;", sg, ch);
            if (LCW_bits[16] == 1) {
                fprintf(stderr, " Res;"); //res bit (octet 2)
            }
            if (LCW_bits[17] == 1) {
                fprintf(stderr, " ENC;"); //P-bit (octet 2)
            }
            state->gi[0] = 0;
        }

        else if (lc_mfid == 0x90 && lc_opcode == 0x3) {
            fprintf(stderr, " MFID90 (Moto) Group Regroup Add");
            // Best-effort: we don't fully parse membership here; keep SG marked active if previously seen.
        }

        else if (lc_mfid == 0x90 && lc_opcode == 0x4) {
            fprintf(stderr, " MFID90 (Moto) Group Regroup Delete");
            // Best-effort: without membership parsing, avoid clearing SG to prevent flicker.
        }

        else if (lc_mfid == 0x90 && lc_opcode == 0x5) {
            // MFID90 Motorola System Information / BSI
            // Field layout is proprietary; log raw data and show computed callsign
            fprintf(stderr, " MFID90 (Moto) System Information (BSI)");
            fprintf(stderr, " Data:");
            for (int bi = 16; bi + 8 <= 72; bi += 8) {
                uint8_t b = (uint8_t)ConvertBitIntoBytes(&LCW_bits[bi], 8);
                fprintf(stderr, " %02X", b);
            }
            // Show computed callsign from current WACN/SysID if available
            if (opts->show_p25_callsign_decode && (state->p2_wacn != 0 || state->p2_sysid != 0)) {
                char callsign[7];
                p25_wacn_sysid_to_callsign((uint32_t)state->p2_wacn, (uint16_t)state->p2_sysid, callsign);
                fprintf(stderr, " [%s]", callsign);
            }
        }

        else if (lc_mfid == 0x90 && lc_opcode == 0xF) {
            uint32_t src = (uint32_t)ConvertBitIntoBytes(&LCW_bits[48], 24);
            fprintf(stderr, " MFID90 (Moto) Talker EOT; SRC: %d;", src);
            // Motorola systems may signal end-of-call via MFID90 Talker EOT
            // rather than standard implicit-MFID Call Termination (0x4F).
            // Treat this as an explicit release when trunk-following.
            memset(state->dmr_pdu_sf[0], 0, sizeof(state->dmr_pdu_sf[0]));
            if (opts->p25_trunk == 1 && state->p25_cc_freq != 0 && opts->p25_is_tuned == 1) {
                state->p25_sm_force_release = 1;
                p25_sm_on_release(opts, state);
            }
        }

        //look for these in logs
        else if (lc_mfid == 0x90 && lc_opcode == 0x15) {
            fprintf(stderr, " MFID90 (Moto) Talker Alias Header");
            apx_embedded_alias_header_phase1(opts, state, 0, LCW_bits);
        }

        else if (lc_mfid == 0x90 && lc_opcode == 0x17) {
            fprintf(stderr, " MFID90 (Moto) Talker Alias Blocks");
            apx_embedded_alias_blocks_phase1(opts, state, 0, LCW_bits);
        }

        else if (lc_mfid == 0xA4 && lc_opcode > 0x31 && lc_opcode < 0x36) {
            fprintf(stderr, " MFIDA4 (Harris) Talker Alias Blocks");
            l3h_embedded_alias_blocks_phase1(opts, state, 0, LCW_bits);
        }

        //Harris GPS on Phase 1 tested and working
        else if (lc_mfid == 0xA4 && lc_opcode == 0x2A) {
            fprintf(stderr, " MFIDA4 (Harris) GPS Block 1");
            memset(state->dmr_pdu_sf[0], 0, sizeof(state->dmr_pdu_sf[0]));
            memcpy(state->dmr_pdu_sf[0], LCW_bits, 16 * sizeof(uint8_t)); //opcode and mfid for check
            memcpy(state->dmr_pdu_sf[0] + 40, LCW_bits + 16,
                   56 * sizeof(uint8_t)); //+40 offset to match the vPDU decoder
        }

        else if (lc_mfid == 0xA4 && lc_opcode == 0x2B) {
            fprintf(stderr, " MFIDA4 (Harris) GPS Block 2");
            memcpy(state->dmr_pdu_sf[0] + 40 + 56, LCW_bits + 16, 56 * sizeof(uint8_t)); //+40 +56 to offset first block
            uint16_t check = (uint16_t)ConvertBitIntoBytes(&state->dmr_pdu_sf[0][0], 16);
            if (check == 0x2AA4) {
                nmea_harris(opts, state, state->dmr_pdu_sf[0], (uint32_t)state->lastsrc, 0);
            } else {
                fprintf(stderr, " Missing GPS Block 1");
            }
            memset(state->dmr_pdu_sf[0], 0, sizeof(state->dmr_pdu_sf[0]));
        }

        //observed format value on Harris SNDCP data channel (Phase 2 CC to Phase 1 MPDU channel)
        else if (lc_mfid == 0xA4 && lc_format == 0x0A) {
            //Could also just be a Unit to Unit Voice Channel User using the reserved field as the MFID?
            //if it were similar to Unit to Unit though, the TGT and SRC values seem to be reversed
            //this appears to be a data channel indicator, has a matching target and the FNE address in it
            uint32_t src = (uint32_t)ConvertBitIntoBytes(&LCW_bits[24], 24);
            uint32_t tgt = (uint32_t)ConvertBitIntoBytes(&LCW_bits[48], 24);
            fprintf(stderr, " MFIDA4 (Harris) Data Channel; SRC: %d; TGT: %d;", src, tgt);
        }

        //observed on Tait conventional / uplink (TODO: Move all this to dsd_alias.c)
        else if (lc_mfid == 0xD8 && lc_format == 0x00) {
            fprintf(stderr, " MFIDD8 (Tait) Talker Alias: ");
            tait_iso7_embedded_alias_decode(opts, state, 0, 8, LCW_bits);
        }

        //not a duplicate, this one will print if not MFID 0 or 1
        else {
            fprintf(stderr, " Unknown Format %02X MFID %02X ", lc_format, lc_mfid);
            if (lc_mfid == 0x90) {
                fprintf(stderr, "(Moto)");
            } else if (lc_mfid == 0xA4) {
                fprintf(stderr, "(Harris)");
            } else if (lc_mfid == 0xD8) {
                fprintf(stderr, "(Tait)");
            }
        }
    }

    //ending line break
    fprintf(stderr, "\n");
}
