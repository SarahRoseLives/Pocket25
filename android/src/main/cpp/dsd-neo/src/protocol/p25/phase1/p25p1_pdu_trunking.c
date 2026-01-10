// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*-------------------------------------------------------------------------------
 * p25p1_pdu_trunking.c
 * P25p1 PDU Alt Format Trunking
 *
 * LWVMOBILE
 * 2025-03 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/protocol/p25/p25_vpdu.h>
#include <dsd-neo/protocol/p25/p25p1_pdu_trunking.h>
#include <dsd-neo/runtime/colors.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

//trunking data delivered via PDU format
void
p25_decode_pdu_trunking(dsd_opts* opts, dsd_state* state, uint8_t* mpdu_byte) {
    //group list mode so we can look and see if we need to block tuning any groups, etc
    char mode[8]; //allow, block, digital, enc, etc
    sprintf(mode, "%s", "");

    //if we are using allow/whitelist mode, then write 'B' to mode for block
    //comparison below will look for an 'A' to write to mode if it is allowed
    if (opts->trunk_use_allow_list == 1) {
        sprintf(mode, "%s", "B");
    }

    uint8_t fmt = mpdu_byte[0] & 0x1F;
    uint8_t MFID = mpdu_byte[2];
    int blks = mpdu_byte[6] & 0x7F;
    uint8_t opcode = 0;

    if (fmt == 0x15) {
        fprintf(stderr, " UNC");
    } else {
        fprintf(stderr, " ALT");
    }
    fprintf(stderr, " MBT");
    if (fmt == 0x17) {
        opcode = mpdu_byte[7] & 0x3F; //alt MBT
    } else {
        opcode = mpdu_byte[12] & 0x3F; //unconf MBT
    }
    fprintf(stderr, " - OP: %02X", opcode);

    // Bridge Identifier Updates (MBT -> MAC layout) so iden tables are populated on P1, too
    // Use the existing MAC decoder to normalize parsing and state updates.
    // Note: Standard Identifier Update MAC formats do not carry an MFID octet; payload starts
    // immediately after the opcode. Populate MAC[] accordingly so downstream parsers align.
    if ((opcode == 0x74 || opcode == 0x7D || opcode == 0x73 || opcode == 0xF3 || opcode == 0x34 || opcode == 0x3D
         || opcode == 0x33) // accept both MAC-coded and MBT/TSBK-coded values
        && MFID < 2) {
        // Build a minimal MAC buffer from MBT payload immediately after the opcode byte
        // ALT format places opcode at index 7; UNCONF at index 12
        int op_idx = (fmt == 0x17) ? 7 : 12;
        int payload_off = op_idx + 1;
        int total_len = (12 * (blks + 1));
        unsigned long long int MAC[24] = {0};
        // Convert MBT/TSBK-coded opcodes (0x3x) to MAC-coded (set 0x40) when needed
        uint8_t mac_opcode = opcode;
        if ((mac_opcode & 0xC0) == 0x00) {
            mac_opcode |= 0x40;
        }
        MAC[1] = mac_opcode; // opcode

        // Copy as many bytes as we have room for and are available in this MBT
        int mac_i = 2; // payload begins at MAC[2] for standard formats
        for (int i = payload_off; i < total_len && mac_i < 24; i++, mac_i++) {
            MAC[mac_i] = mpdu_byte[i];
        }

        fprintf(stderr, "%s", KYEL);
        fprintf(stderr, "\n Identifier Update (MBT bridged) OP:%02X -> MAC decode", opcode);
        process_MAC_VPDU(opts, state, 0, MAC);
        fprintf(stderr, "%s", KNRM);
        // Fall through to allow any additional prints for known MBT messages below
    }

    //NET_STS_BCST -- TIA-102.AABC-D 6.2.11.2
    if (opcode == 0x3B) {
        int lra = mpdu_byte[3];
        int sysid = ((mpdu_byte[4] & 0xF) << 8) | mpdu_byte[5];
        int res_a = mpdu_byte[8];
        int res_b = mpdu_byte[9];
        long int wacn = (mpdu_byte[12] << 12) | (mpdu_byte[13] << 4) | (mpdu_byte[14] >> 4);
        int channelt = (mpdu_byte[15] << 8) | mpdu_byte[16];
        int channelr = (mpdu_byte[17] << 8) | mpdu_byte[18];
        int ssc = mpdu_byte[19];
        UNUSED3(res_a, res_b, ssc);
        fprintf(stderr, "%s", KYEL);
        fprintf(stderr, "\n Network Status Broadcast MBT - Extended \n");
        fprintf(stderr, "  LRA [%02X] WACN [%05lX] SYSID [%03X] NAC [%03llX]\n", lra, wacn, sysid, state->p2_cc);
        fprintf(stderr, "  CHAN-T [%04X] CHAN-R [%04X]", channelt, channelr);
        long int ct_freq = process_channel_to_freq(opts, state, channelt);
        long int cr_freq = process_channel_to_freq(opts, state, channelr);
        UNUSED(cr_freq);

        if (ct_freq > 0) {
            state->p25_cc_freq = ct_freq;
            state->p25_cc_is_tdma = 0; //flag off for CC tuning purposes when system is qpsk

            //place the cc freq into the list at index 0 if 0 is empty, or not the same,
            //so we can hunt for rotating CCs without user LCN list
            if (state->trunk_lcn_freq[0] == 0 || state->trunk_lcn_freq[0] != state->p25_cc_freq) {
                state->trunk_lcn_freq[0] = state->p25_cc_freq;
            }

            //only set IF these values aren't already hard set by the user
            if (state->p2_hardset == 0) {
                if ((state->p2_wacn != 0 || state->p2_sysid != 0)
                    && (state->p2_wacn != (unsigned long long)wacn || state->p2_sysid != (unsigned long long)sysid)) {
                    p25_reset_iden_tables(state);
                }
                if (wacn != 0 || sysid != 0) {
                    state->p2_wacn = wacn;
                    state->p2_sysid = sysid;
                }
            }

            long neigh[2] = {ct_freq, cr_freq};
            p25_sm_on_neighbor_update(opts, state, neigh, 2);
            // Confirm any IDENs now that CC identity is known
            p25_confirm_idens_for_current_site(state);
        } else {
            fprintf(stderr, "\n  P25 MBT NET_STS: ignoring invalid channel->freq (CHAN-T=%04X)", channelt);
        }
    }
    //RFSS Status Broadcast - Extended 6.2.15.2
    else if (opcode == 0x3A) {
        int lra = mpdu_byte[3];
        int lsysid = ((mpdu_byte[4] & 0xF) << 8) | mpdu_byte[5];
        int rfssid = mpdu_byte[12];
        int siteid = mpdu_byte[13];
        int channelt = (mpdu_byte[14] << 8) | mpdu_byte[15];
        int channelr = (mpdu_byte[16] << 8) | mpdu_byte[17];
        int sysclass = mpdu_byte[18];
        fprintf(stderr, "%s", KYEL);
        fprintf(stderr, "\n RFSS Status Broadcast MBF - Extended \n");
        fprintf(stderr,
                "  LRA [%02X] SYSID [%03X] RFSS ID [%03d] SITE ID [%03d]\n  CHAN-T [%04X] CHAN-R [%02X] SSC [%02X] ",
                lra, lsysid, rfssid, siteid, channelt, channelr, sysclass);
        long int f1 = process_channel_to_freq(opts, state, channelt);
        long int f2 = process_channel_to_freq(opts, state, channelr);

        long neigh2[2] = {f1, f2};
        p25_sm_on_neighbor_update(opts, state, neigh2, 2);

        state->p2_siteid = siteid;
        state->p2_rfssid = rfssid;
        p25_confirm_idens_for_current_site(state);
    }

    //Adjacent Status Broadcast (ADJ_STS_BCST) Extended 6.2.2.2
    else if (opcode == 0x3C) {
        int lra = mpdu_byte[3];
        int cfva = mpdu_byte[4] >> 4;
        int lsysid = ((mpdu_byte[4] & 0xF) << 8) | mpdu_byte[5];
        int rfssid = mpdu_byte[8];
        int siteid = mpdu_byte[9];
        int channelt = (mpdu_byte[12] << 8) | mpdu_byte[13];
        int channelr = (mpdu_byte[14] << 8) | mpdu_byte[15];
        int sysclass = mpdu_byte[16];
        long int wacn = (mpdu_byte[17] << 12) | (mpdu_byte[18] << 4) | (mpdu_byte[19] >> 4);
        fprintf(stderr, "%s", KYEL);
        fprintf(stderr, "\n Adjacent Status Broadcast - Extended\n");
        fprintf(stderr,
                "  LRA [%02X] CFVA [%X] RFSS[%03d] SITE [%03d] SYSID [%03X]\n  CHAN-T [%04X] CHAN-R [%04X] SSC [%02X] "
                "WACN [%05lX]\n  ",
                lra, cfva, rfssid, siteid, lsysid, channelt, channelr, sysclass, wacn);
        if (cfva & 0x8) {
            fprintf(stderr, " Conventional");
        }
        if (cfva & 0x4) {
            fprintf(stderr, " Failure Condition");
        }
        if (cfva & 0x2) {
            fprintf(stderr, " Up to Date (Correct)");
        } else {
            fprintf(stderr, " Last Known");
        }
        if (cfva & 0x1) {
            fprintf(stderr, " Valid RFSS Connection Active");
        }
        long int f3 = process_channel_to_freq(opts, state, channelt);
        long int f4 = process_channel_to_freq(opts, state, channelr);
        long neigh3[2] = {f3, f4};
        p25_sm_on_neighbor_update(opts, state, neigh3, 2);

    }

    //Group Voice Channel Grant - Extended
    else if (opcode == 0x0) {
        int svc = mpdu_byte[8];
        int channelt = (mpdu_byte[14] << 8) | mpdu_byte[15];
        int channelr = (mpdu_byte[16] << 8) | mpdu_byte[17];
        long int source = (mpdu_byte[3] << 16) | (mpdu_byte[4] << 8) | mpdu_byte[5];
        int group = (mpdu_byte[18] << 8) | mpdu_byte[19];
        long int freq1 = 0;
        long int freq2 = 0;
        UNUSED2(source, freq2);
        fprintf(stderr, "%s\n ", KYEL);
        if (svc & 0x80) {
            fprintf(stderr, " Emergency");
            state->p25_call_emergency[0] = 1;
        } else {
            state->p25_call_emergency[0] = 0;
        }
        if (svc & 0x40) {
            fprintf(stderr, " Encrypted");
        }

        if (opts->payload == 1) //hide behind payload due to len
        {
            if (svc & 0x20) {
                fprintf(stderr, " Duplex");
            }
            if (svc & 0x10) {
                fprintf(stderr, " Packet");
            } else {
                fprintf(stderr, " Circuit");
            }
            if (svc & 0x8) {
                fprintf(stderr, " R"); //reserved bit is on
            }
            fprintf(stderr, " Priority %d", svc & 0x7); //call priority
            state->p25_call_priority[0] = (uint8_t)(svc & 0x7);
        } else {
            state->p25_call_priority[0] = 0;
        }
        fprintf(stderr, " Group Voice Channel Grant Update - Extended");
        fprintf(stderr, "\n  SVC [%02X] CHAN-T [%04X] CHAN-R [%04X] Group [%d][%04X]", svc, channelt, channelr, group,
                group);
        freq1 = process_channel_to_freq(opts, state, channelt);
        (void)process_channel_to_freq(opts, state, channelr);

        //add active channel to string for ncurses display (FDMA/slot hint only shown for TDMA systems)
        char suf1[32];
        p25_format_chan_suffix(state, channelt, -1, suf1, sizeof suf1);
        sprintf(state->active_channel[0], "Active Ch: %04X%s TG: %d; ", channelt, suf1, group);
        state->last_active_time = time(NULL);

        for (unsigned int i = 0; i < state->group_tally; i++) {
            if (state->group_array[i].groupNumber == (unsigned long)group) {
                fprintf(stderr, " [%s]", state->group_array[i].groupName);
                strncpy(mode, state->group_array[i].groupMode, sizeof(mode) - 1);
                mode[sizeof(mode) - 1] = '\0';
                break;
            }
        }

        //TG hold on P25p1 Ext -- block non-matching target, allow matching group
        if (state->tg_hold != 0 && state->tg_hold != (uint32_t)group) {
            sprintf(mode, "%s", "B");
        }
        if (state->tg_hold != 0 && state->tg_hold == (uint32_t)group) {
            sprintf(mode, "%s", "A");
        }

        //Skip tuning group calls if group calls are disabled
        if (opts->trunk_tune_group_calls == 0) {
            goto SKIPCALL;
        }

        //Skip tuning encrypted calls if enc calls are disabled – emit event immediately (once)
        //Override when Harris GRG policy indicates KEY=0000 for this TG/SG
        if ((svc & 0x40) && opts->trunk_tune_enc_calls == 0 && !p25_patch_tg_key_is_clear(state, group)
            && !p25_patch_sg_key_is_clear(state, group)) {
            p25_emit_enc_lockout_once(opts, state, 0, group, svc);
            goto SKIPCALL;
        }

        //tune if tuning available
        if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
            if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq1 != 0) {
                p25_sm_on_group_grant(opts, state, channelt, svc, group, (int)source);
            }
        }
    }

    //Unit to Unit Voice Channel Grant - Extended
    else if (opcode == 0x6) {
        //I'm not doing EVERY element of this, just enough for tuning!
        int svc = mpdu_byte[8];
        int channelt = (mpdu_byte[22] << 8) | mpdu_byte[23];
        int channelr = (mpdu_byte[24] << 8) | mpdu_byte[25]; //optional!
        //using source and target address, not source and target id (is this correct?)
        long int source = (mpdu_byte[3] << 16) | (mpdu_byte[4] << 8) | mpdu_byte[5];
        long int target = (mpdu_byte[19] << 16) | (mpdu_byte[20] << 8) | mpdu_byte[21];
        //TODO: Test Full Values added here for SUID, particular tgt_nid and tgt_sid
        long int src_nid = (mpdu_byte[12] << 24) | (mpdu_byte[13] << 16) | (mpdu_byte[14] << 8) | mpdu_byte[15];
        long int src_sid = (mpdu_byte[16] << 16) | (mpdu_byte[17] << 8) | mpdu_byte[18];
        long int tgt_nid =
            (mpdu_byte[26] << 16) | (mpdu_byte[27] << 8) | mpdu_byte[28]; //only has 3 octets on tgt nid, partial only?
        long int tgt_sid = (mpdu_byte[29] << 16) | (mpdu_byte[30] << 8) | mpdu_byte[31];
        long int freq1 = 0;
        long int freq2 = 0;
        UNUSED(freq2);
        fprintf(stderr, "%s\n ", KYEL);
        if (svc & 0x80) {
            fprintf(stderr, " Emergency");
            state->p25_call_emergency[0] = 1;
        } else {
            state->p25_call_emergency[0] = 0;
        }
        if (svc & 0x40) {
            fprintf(stderr, " Encrypted");
        }

        if (opts->payload == 1) //hide behind payload due to len
        {
            if (svc & 0x20) {
                fprintf(stderr, " Duplex");
            }
            if (svc & 0x10) {
                fprintf(stderr, " Packet");
            } else {
                fprintf(stderr, " Circuit");
            }
            if (svc & 0x8) {
                fprintf(stderr, " R"); //reserved bit is on
            }
            fprintf(stderr, " Priority %d", svc & 0x7); //call priority
            state->p25_call_priority[0] = (uint8_t)(svc & 0x7);
        } else {
            state->p25_call_priority[0] = 0;
        }
        fprintf(stderr, " Unit to Unit Voice Channel Grant Update - Extended");
        fprintf(stderr,
                "\n  SVC: %02X; CHAN-T: %04X; CHAN-R: %04X; SRC: %ld; TGT: %ld; FULL SRC: %08lX-%08ld; FULL TGT: "
                "%08lX-%08ld;",
                svc, channelt, channelr, source, target, src_nid, src_sid, tgt_nid, tgt_sid);
        freq1 = process_channel_to_freq(opts, state, channelt);
        (void)process_channel_to_freq(opts, state, channelr); //optional!

        //add active channel to string for ncurses display
        char suf2[32];
        p25_format_chan_suffix(state, channelt, -1, suf2, sizeof suf2);
        sprintf(state->active_channel[0], "Active Ch: %04X%s TGT: %u; ", channelt, suf2, (uint32_t)target);

        for (unsigned int i = 0; i < state->group_tally; i++) {
            if (state->group_array[i].groupNumber == (unsigned long)target) {
                fprintf(stderr, " [%s]", state->group_array[i].groupName);
                strncpy(mode, state->group_array[i].groupMode, sizeof(mode) - 1);
                mode[sizeof(mode) - 1] = '\0';
                break;
            }
        }

        //TG hold on P25p1 Ext UU -- will want to disable UU_V grants while TG Hold enabled
        if (state->tg_hold != 0 && state->tg_hold != (uint32_t)target) {
            sprintf(mode, "%s", "B");
        }
        // if (state->tg_hold != 0 && state->tg_hold == (uint32_t)target) sprintf (mode, "%s", "A");

        //Skip tuning private calls if group calls are disabled
        if (opts->trunk_tune_private_calls == 0) {
            goto SKIPCALL;
        }

        //Skip tuning encrypted calls if enc calls are disabled
        if ((svc & 0x40) && opts->trunk_tune_enc_calls == 0) {
            goto SKIPCALL;
        }

        //tune if tuning available
        if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
            if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq1 != 0) {
                p25_sm_on_indiv_grant(opts, state, channelt, svc, (int)target, (int)source);
            }
        }
    }

    //Telephone Interconnect Voice Channel Grant (or Update) -- Explicit Channel Form
    else if ((opcode == 0x8 || opcode == 0x9)
             && MFID < 2) //This form does allow for other MFID's but Moto has a seperate function on 9
    {
        //TELE_INT_CH_GRANT or TELE_INT_CH_GRANT_UPDT
        int svc = mpdu_byte[8];
        int channel = (mpdu_byte[12] << 8) | mpdu_byte[13];
        int timer = (mpdu_byte[16] << 8) | mpdu_byte[17];
        uint32_t target = (uint32_t)((mpdu_byte[3] << 16) | (mpdu_byte[4] << 8) | mpdu_byte[5]);
        long int freq = 0;
        fprintf(stderr, "\n");
        if (svc & 0x80) {
            fprintf(stderr, " Emergency");
            state->p25_call_emergency[0] = 1;
        } else {
            state->p25_call_emergency[0] = 0;
        }
        if (svc & 0x40) {
            fprintf(stderr, " Encrypted");
        }

        if (opts->payload == 1) //hide behind payload due to len
        {
            if (svc & 0x20) {
                fprintf(stderr, " Duplex");
            }
            if (svc & 0x10) {
                fprintf(stderr, " Packet");
            } else {
                fprintf(stderr, " Circuit");
            }
            if (svc & 0x8) {
                fprintf(stderr, " R"); //reserved bit is on
            }
            fprintf(stderr, " Priority %d", svc & 0x7); //call priority
            state->p25_call_priority[0] = (uint8_t)(svc & 0x7);
        } else {
            state->p25_call_priority[0] = 0;
        }

        fprintf(stderr, " Telephone Interconnect Voice Channel Grant");
        if (opcode & 1) {
            fprintf(stderr, " Update");
        }
        fprintf(stderr, " Extended");
        fprintf(stderr, "\n  CHAN: %04X; Timer: %f Seconds; Target: %u;", channel, (float)timer * 0.1f,
                target); //timer unit is 100 ms, or 0.1 seconds
        freq = process_channel_to_freq(opts, state, channel);

        //add active channel to string for ncurses display
        if (channel != 0 && channel != 0xFFFF) {
            sprintf(state->active_channel[0], "Active Tele Ch: %04X TGT: %u; ", channel, target);
        }
        state->last_active_time = time(NULL);

        //Skip tuning private calls if private calls is disabled (are telephone int calls private, or talkgroup?)
        if (opts->trunk_tune_private_calls == 0) {
            goto SKIPCALL;
        }

        //Skip tuning encrypted calls if enc calls are disabled
        if ((svc & 0x40) && opts->trunk_tune_enc_calls == 0) {
            goto SKIPCALL;
        }

        //telephone only has a target address (manual shows combined source/target of 24-bits)
        for (unsigned int i = 0; i < state->group_tally; i++) {
            if (state->group_array[i].groupNumber == (unsigned long)target) {
                fprintf(stderr, " [%s]", state->group_array[i].groupName);
                strncpy(mode, state->group_array[i].groupMode, sizeof(mode) - 1);
                mode[sizeof(mode) - 1] = '\0';
                break;
            }
        }

        //TG hold on UU_V -- will want to disable UU_V grants while TG Hold enabled
        if (state->tg_hold != 0 && state->tg_hold != target) {
            sprintf(mode, "%s", "B");
        }

        //tune if tuning available (centralized)
        if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
            if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq != 0) {
                p25_sm_on_indiv_grant(opts, state, channel, svc, (int)target, /*src*/ 0);
            }
        }
        if (opts->p25_trunk == 0) {
            if ((int)target == state->lasttg || (int)target == state->lasttgR) {
                //P1 FDMA
                if (DSD_SYNC_IS_P25P1(state->synctype)) {
                    state->p25_vc_freq[0] = freq;
                }
                //P2 TDMA
                else {
                    state->p25_vc_freq[0] = state->p25_vc_freq[1] = freq;
                }
            }
        }

    }

    //look at Harris Opcodes and payload portion of MPDU
    else if (MFID == 0xA4) {
        //TODO: Add Known Opcodes from Manual (all one of them)
        fprintf(stderr, "%s", KCYN);
        fprintf(stderr, "\n MFID A4 (Harris); Opcode: %02X; ", opcode);
        for (int i = 0; i < (12 * (blks + 1) % 37); i++) {
            fprintf(stderr, "%02X", mpdu_byte[i]);
        }
        fprintf(stderr, " %s", KNRM);
    }

    //look at Motorola Opcodes and payload portion of MPDU
    else if (MFID == 0x90) {
        //TIA-102.AABH
        if (opcode == 0x02) {
            int svc = mpdu_byte[8]; //Just the Res, P-bit, and more res bits
            int channelt = (mpdu_byte[12] << 8) | mpdu_byte[13];
            int channelr = (mpdu_byte[14] << 8) | mpdu_byte[15];
            long int source = (mpdu_byte[3] << 16) | (mpdu_byte[4] << 8) | mpdu_byte[5];
            int group = (mpdu_byte[16] << 8) | mpdu_byte[17];
            long int freq1 = 0;
            long int freq2 = 0;
            UNUSED2(source, freq2);
            fprintf(stderr, "%s\n ", KYEL);

            if (svc & 0x40) {
                fprintf(stderr, " Encrypted"); //P-bit
            }

            fprintf(stderr, " MFID90 Group Regroup Channel Grant - Explicit");
            fprintf(stderr, "\n  RES/P [%02X] CHAN-T [%04X] CHAN-R [%04X] SG [%d][%04X]", svc, channelt, channelr,
                    group, group);
            freq1 = process_channel_to_freq(opts, state, channelt);
            (void)process_channel_to_freq(opts, state, channelr);

            //add active channel to string for ncurses display
            {
                char suf3[32];
                p25_format_chan_suffix(state, channelt, -1, suf3, sizeof suf3);
                sprintf(state->active_channel[0], "MFID90 Ch: %04X%s SG: %d ", channelt, suf3, group);
            }
            state->last_active_time = time(NULL);

            for (unsigned int i = 0; i < state->group_tally; i++) {
                if (state->group_array[i].groupNumber == (unsigned long)group) {
                    fprintf(stderr, " [%s]", state->group_array[i].groupName);
                    strncpy(mode, state->group_array[i].groupMode, sizeof(mode) - 1);
                    mode[sizeof(mode) - 1] = '\0';
                    break;
                }
            }

            //TG hold on MFID90 GRG -- block non-matching target, allow matching group
            if (state->tg_hold != 0 && state->tg_hold != (uint32_t)group) {
                sprintf(mode, "%s", "B");
            }
            if (state->tg_hold != 0 && state->tg_hold == (uint32_t)group) {
                sprintf(mode, "%s", "A");
            }

            //Skip tuning group calls if group calls are disabled
            if (opts->trunk_tune_group_calls == 0) {
                goto SKIPCALL;
            }

            //Skip tuning encrypted calls if enc calls are disabled – emit event immediately (once)
            //Harris GRG KEY=0000 policy override: if SG policy is clear, allow tuning
            if ((svc & 0x40) && opts->trunk_tune_enc_calls == 0 && !p25_patch_sg_key_is_clear(state, group)) {
                p25_emit_enc_lockout_once(opts, state, 0, group, svc);
                goto SKIPCALL;
            }

            //tune if tuning available
            if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq1 != 0) {
                    p25_sm_on_group_grant(opts, state, channelt, svc, group, (int)source);
                }
            }
        }

        else {
            fprintf(stderr, "%s", KCYN);
            fprintf(stderr, "\n MFID 90 (Moto); Opcode: %02X; ", mpdu_byte[0] & 0x3F);
            for (int i = 0; i < (12 * (blks + 1) % 37); i++) {
                fprintf(stderr, "%02X", mpdu_byte[i]);
            }
            fprintf(stderr, " %s", KNRM);
        }
    }

    else {
        fprintf(stderr, "%s", KCYN);
        fprintf(stderr, "\n MFID %02X (Unknown); Opcode: %02X; ", MFID, mpdu_byte[0] & 0x3F);
        for (int i = 0; i < (12 * (blks + 1) % 37); i++) {
            fprintf(stderr, "%02X", mpdu_byte[i]);
        }
        fprintf(stderr, " %s", KNRM);
    }

SKIPCALL:; //do nothing
}
