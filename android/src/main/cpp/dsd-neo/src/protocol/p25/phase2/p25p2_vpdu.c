// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*-------------------------------------------------------------------------------
 * p25p2_vpdu.c
 * Phase 2 Variable PDU (and TSBK PDU) Handling
 *
 * LWVMOBILE
 * 2022-10 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/bit_packing.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/embedded_alias.h>
#include <dsd-neo/core/gps.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/p25/p25_callsign.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/protocol/p25/p25_vpdu.h>
#include <dsd-neo/runtime/colors.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/p25_p2_audio_ring.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Forward prototype for bounded append helper (definition later)
static inline void dsd_append(char* dst, size_t dstsz, const char* src);

// Expose MAC helpers for tests and diagnostics.
#include <dsd-neo/protocol/p25/p25p2_mac_parse.h>
#include <dsd-neo/protocol/p25/p25p2_mac_tables.h>

/* Emit a compact JSON line for a P25 Phase 2 MAC PDU when enabled. */
static void
p25p2_emit_mac_json_if_enabled(dsd_state* state, int xch_type, uint8_t mfid, uint8_t opcode, int slot, int len_b,
                               int len_c, const char* summary) {
    const dsdneoRuntimeConfig* rc = dsd_neo_get_config();
    if (!rc || !rc->pdu_json_enable) {
        return;
    }

    /* xch_type: 0 FACCH, 1 SACCH; prefer LCCH label when flagged */
    const char* xch = (state && state->p2_is_lcch) ? "LCCH" : (xch_type == 1 ? "SACCH" : "FACCH");

    /* Minimal summary sanitization (drop quotes) to keep JSON valid */
    char sum[80];
    sum[0] = '\0';
    if (summary && summary[0] != '\0') {
        int j = 0;
        for (int i = 0; summary[i] != '\0' && j < (int)sizeof(sum) - 1; i++) {
            char ch = summary[i];
            if (ch == '"') {
                continue;
            }
            sum[j++] = ch;
        }
        sum[j] = '\0';
    }

    time_t ts = time(NULL);
    fprintf(stderr,
            "{\"ts\":%ld,\"proto\":\"p25\",\"mac\":1,\"xch\":\"%s\",\"mfid\":%u,\"op\":%u,\"slot\":%d,\"slot1\":%d,"
            "\"lenB\":%d,\"lenC\":%d,\"summary\":\"%s\"}\n",
            (long)ts, xch, (unsigned)mfid, (unsigned)opcode, slot, slot + 1, len_b, len_c, sum);
}

/* Centralized helper for MAC-based group grants; currently a thin wrapper
 * over the Tier II/III trunking state machine so future refactors can
 * route per-opcode behavior through a single surface. */
static void
p25p2_mac_handle(const struct p25p2_mac_result* res, dsd_opts* opts, dsd_state* state, int channel, int svc_bits,
                 int group, int source) {
    (void)res;
    if (!opts || !state) {
        return;
    }
    p25_sm_on_group_grant(opts, state, channel, svc_bits, group, source);
}

//MAC PDU 3-bit Opcodes BBAC (8.4.1) p 123:
//0 - reserved //1 - Mac PTT //2 - Mac End PTT //3 - Mac Idle //4 - Mac Active
//5 - reserved //6 - Mac Hangtime //7 - reserved //Mac PTT BBAC p80

//TODO: Check for Non standard MFIDs first MAC[1], then set len on the MAC[2] if
//the result from the len table is 0 (had to manually enter a few observed values from Harris)
void
process_MAC_VPDU(dsd_opts* opts, dsd_state* state, int type, unsigned long long int MAC[24]) {
    //handle variable content MAC PDUs (Active, Idle, Hangtime, or Signal)
    //use type to specify SACCH or FACCH, so we know if we should invert the currentslot when assigning ids etc

    //b values - 0 = Unique TDMA Message,  1 Phase 1 OSP/ISP abbreviated
    // 2 = Manufacturer Message, 3 Phase 1 OSP/ISP extended/explicit

    struct p25p2_mac_result mac_res;
    if (p25p2_mac_parse(type, MAC, &mac_res) != 0) {
        return;
    }

    int len_a = mac_res.len_a;
    int len_b = mac_res.len_b;
    int len_c = mac_res.len_c;

    int slot = 9;
    if (type == 1) //0 for F, 1 for S
    {
        slot = (state->currentslot ^ 1) & 1; //flip slot internally for SACCH
    } else {
        slot = state->currentslot;
    }

    /* Emit one JSON record for this MAC PDU (when enabled) */
    {
        uint8_t mfid = (uint8_t)MAC[2];
        uint8_t opcode = (uint8_t)MAC[1];
        const char* tag = NULL;
        switch (opcode) {
            case 0x0: tag = "SIGNAL"; break;
            case 0x1: tag = "PTT"; break;
            case 0x2: tag = "END"; break;
            case 0x3: tag = "IDLE"; break;
            case 0x4: tag = "ACTIVE"; break;
            case 0x6: tag = "HANGTIME"; break;
            default:
                tag = "MAC"; /* generic */
                break;
        }
        p25p2_emit_mac_json_if_enabled(state, type, mfid, opcode, slot, len_b, len_c, tag);
    }

    //assigning here if OECI MAC SIGNAL, after passing RS and CRC
    if (state->p2_is_lcch == 1) {
        //fix for blinking SIGNAL on Slot 2 during inverted slot in Ncurses
        //TEMP: assume LCH 0 is the SIGNAL slot, fix for blinking SIGNAL on Slot 2 during inverted slot
        if (type == 0 && slot == 0) {
            state->dmrburstL = 30;
        }
        if (type == 1 && slot == 0) {
            state->dmrburstL = 30;
        }
        // if (type == 0 && slot == 1) state->dmrburstR = 30;
        // if (type == 1 && slot == 1) state->dmrburstR = 30;

        // Do not change per-slot audio gating on MAC_SIGNAL. Gating is
        // controlled by MAC_PTT/END and ESS fallback so clear opposite-slot
        // calls don’t get muted by periodic LCCH signaling.

        //TODO: Iron out issues with audio playing in every non SACCH slot when no voice present
        //without it stuttering during actual voice
    }

    // One-time diagnostics for unknown vendor/opcode MAC lengths to aid future coverage.
    // Only emit if we failed both table/override and MCO-based fallback.
    if (len_b == 0) {
        static struct {
            uint8_t mfid;
            uint8_t opcode;
        } seen[32];

        static int seen_count = 0;
        uint8_t mfid = (uint8_t)MAC[2];
        uint8_t opcode = (uint8_t)MAC[1];

        int already = 0;
        for (int i = 0; i < seen_count; i++) {
            if (seen[i].mfid == mfid && seen[i].opcode == opcode) {
                already = 1;
                break;
            }
        }
        if (!already && seen_count < (int)(sizeof(seen) / sizeof(seen[0]))) {
            seen[seen_count].mfid = mfid;
            seen[seen_count].opcode = opcode;
            seen_count++;
            fprintf(stderr, "%s", KYEL);
            int mco_dbg = (int)(MAC[1] & 0x3F);
            fprintf(stderr,
                    "\n P25p2 MAC length unknown/unsupported: MFID=%02X OPCODE=%02X (len=0, MCO=%d). Please report.\n",
                    mfid, opcode, mco_dbg);
            fprintf(stderr, "%s", KNRM);
        }
        goto END_PDU;
    }

    //group list mode so we can look and see if we need to block tuning any groups, etc
    char mode[8]; //allow, block, digital, enc, etc
    sprintf(mode, "%s", "");

    //if we are using allow/whitelist mode, then write 'B' to mode for block
    //comparison below will look for an 'A' to write to mode if it is allowed
    if (opts->trunk_use_allow_list == 1) {
        sprintf(mode, "%s", "B");
    }

    for (int i = 0; i < 2; i++) {

        //MFID90 Voice Grants, A3, A4, and A5 <--I bet A4 here was triggering a phantom call when TSBK sent PDUs here
        //MFID90 Group Regroup Channel Grant - Implicit
        if (MAC[1 + len_a] == 0xA3 && MAC[2 + len_a] == 0x90) {
            int mfid = MAC[2 + len_a];
            UNUSED(mfid);
            int channel = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
            int sgroup = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            long int freq = 0;
            fprintf(stderr, "\n MFID90 Group Regroup Channel Grant - Implicit");
            fprintf(stderr, "\n  CHAN [%04X] Group [%d][%04X]", channel, sgroup, sgroup);
            freq = process_channel_to_freq(opts, state, channel);

            //add active channel to string for ncurses display
            char suf_m90a[32];
            p25_format_chan_suffix(state, (uint16_t)channel, -1, suf_m90a, sizeof suf_m90a);
            sprintf(state->active_channel[0], "MFID90 Active Ch: %04X%s SG: %d; ", channel, suf_m90a, sgroup);
            state->last_active_time = time(NULL);

            for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                if (state->group_array[gi].groupNumber == (unsigned long)sgroup) {
                    fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                    strncpy(mode, state->group_array[gi].groupMode, sizeof(mode) - 1);
                    mode[sizeof(mode) - 1] = '\0';
                    break;
                }
            }

            //TG hold on MFID90 GRG -- block non-matching super group, allow matching group
            if (state->tg_hold != 0 && state->tg_hold != (uint32_t)sgroup) {
                sprintf(mode, "%s", "B");
            }
            if (state->tg_hold != 0 && state->tg_hold == (uint32_t)sgroup) {
                sprintf(mode, "%s", "A");
            }

            //Skip tuning group calls if group calls are disabled
            if (opts->trunk_tune_group_calls == 0) {
                goto SKIPCALL;
            }

            //tune if tuning available (centralized)
            if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq != 0) {
                    // ENC lockout: these MFID90 GRG grants do not carry SVC bits. When
                    // ENC lockout is enabled, be conservative and avoid tuning unless a
                    // Harris regroup/patch policy explicitly indicates KEY=0000 (clear)
                    // for this TG/SG.
                    if (opts->trunk_tune_enc_calls == 0 && !p25_patch_tg_key_is_clear(state, sgroup)
                        && !p25_patch_sg_key_is_clear(state, sgroup)) {
                        goto SKIPCALL;
                    }
                    p25p2_mac_handle(&mac_res, opts, state, channel, /*svc_bits*/ 0, sgroup, /*src*/ 0);
                }
            }
            //if playing back files, and we still want to see what freqs are in use in the ncurses terminal
            //might only want to do these on a grant update, and not a grant by itself?
            if (opts->p25_trunk == 0) {
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

        //MFID90 Group Regroup Channel Grant - Explicit
        if (MAC[1 + len_a] == 0xA4 && MAC[2 + len_a] == 0x90) {
            int mfid = MAC[2 + len_a];
            int channel = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
            int channelr = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int sgroup = (MAC[9 + len_a] << 8) | MAC[10 + len_a];
            long int freq = 0;
            UNUSED2(mfid, channelr);
            fprintf(stderr, "\n MFID90 Group Regroup Channel Grant - Explicit");
            fprintf(stderr, "\n  CHAN [%04X] Group [%d][%04X]", channel, sgroup, sgroup);
            freq = process_channel_to_freq(opts, state, channel);

            //add active channel to string for ncurses display
            char suf_m90b[32];
            p25_format_chan_suffix(state, (uint16_t)channel, -1, suf_m90b, sizeof suf_m90b);
            sprintf(state->active_channel[0], "MFID90 Active Ch: %04X%s SG: %d; ", channel, suf_m90b, sgroup);
            state->last_active_time = time(NULL);

            for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                if (state->group_array[gi].groupNumber == (unsigned long)sgroup) {
                    fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                    strncpy(mode, state->group_array[gi].groupMode, sizeof(mode) - 1);
                    mode[sizeof(mode) - 1] = '\0';
                    break;
                }
            }

            //TG hold on MFID90 GRG -- block non-matching super group, allow matching group
            if (state->tg_hold != 0 && state->tg_hold != (uint32_t)sgroup) {
                sprintf(mode, "%s", "B");
            }
            if (state->tg_hold != 0 && state->tg_hold == (uint32_t)sgroup) {
                sprintf(mode, "%s", "A");
            }

            //Skip tuning group calls if group calls are disabled
            if (opts->trunk_tune_group_calls == 0) {
                goto SKIPCALL;
            }

            //tune if tuning available (centralized)
            if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq != 0) {
                    // ENC lockout conservative gating for MFID90 GRG without SVC bits
                    if (opts->trunk_tune_enc_calls == 0 && !p25_patch_tg_key_is_clear(state, sgroup)
                        && !p25_patch_sg_key_is_clear(state, sgroup)) {
                        goto SKIPCALL;
                    }
                    p25p2_mac_handle(&mac_res, opts, state, channel, /*svc_bits*/ 0, sgroup, /*src*/ 0);
                }
            }
            //if playing back files, and we still want to see what freqs are in use in the ncurses terminal
            //might only want to do these on a grant update, and not a grant by itself?
            if (opts->p25_trunk == 0) {
                if (sgroup == state->lasttg || sgroup == state->lasttgR) {
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

        //MFID90 Group Regroup Channel Grant Update
        if (MAC[1 + len_a] == 0xA5 && MAC[2 + len_a] == 0x90) {
            int channel1 = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int group1 = (MAC[6 + len_a] << 8) | MAC[7 + len_a];
            int channel2 = (MAC[8 + len_a] << 8) | MAC[9 + len_a];
            int group2 = (MAC[10 + len_a] << 8) | MAC[11 + len_a];
            long int freq1 = 0;
            long int freq2 = 0;

            fprintf(stderr, "\n MFID90 Group Regroup Channel Grant Update");
            fprintf(stderr, "\n  Channel 1 [%04X] Group 1 [%d][%04X]", channel1, group1, group1);
            freq1 = process_channel_to_freq(opts, state, channel1);
            if (channel2 != channel1 && channel2 != 0 && channel2 != 0xFFFF) {
                fprintf(stderr, "\n  Channel 2 [%04X] Group 2 [%d][%04X]", channel2, group2, group2);
                freq2 = process_channel_to_freq(opts, state, channel2);
            }

            //add active channel to string for ncurses display
            if (channel2 != channel1 && channel2 != 0 && channel2 != 0xFFFF) {
                char suf_m90c1[32];
                char suf_m90c2[32];
                p25_format_chan_suffix(state, (uint16_t)channel1, -1, suf_m90c1, sizeof suf_m90c1);
                p25_format_chan_suffix(state, (uint16_t)channel2, -1, suf_m90c2, sizeof suf_m90c2);
                sprintf(state->active_channel[0], "MFID90 Active Ch: %04X%s SG: %d; Ch: %04X%s SG: %d; ", channel1,
                        suf_m90c1, group1, channel2, suf_m90c2, group2);
            } else {
                char suf_m90d[32];
                p25_format_chan_suffix(state, (uint16_t)channel1, -1, suf_m90d, sizeof suf_m90d);
                sprintf(state->active_channel[0], "MFID90 Active Ch: %04X%s SG: %d; ", channel1, suf_m90d, group1);
            }
            state->last_active_time = time(NULL);

            //Skip tuning group calls if group calls are disabled
            if (opts->trunk_tune_group_calls == 0) {
                goto SKIPCALL;
            }

            //monstrocity below should get us evaluating and tuning groups...hopefully, will be first come first served though, no priority
            //see how many loops we need to run on when to tune if first group is blocked
            int loop = 1;
            if (channel1 == channel2) {
                loop = 1;
            } else {
                loop = 2;
            }
            //assigned inside loop
            long int tunable_freq = 0;
            int tunable_chan = 0;
            int tunable_group = 0;

            for (int j = 0; j < loop; j++) {
                //assign our internal variables for check down on if to tune one freq/group or not
                if (j == 0) {
                    tunable_freq = freq1;
                    tunable_chan = channel1;
                    tunable_group = group1;
                } else {
                    tunable_freq = freq2;
                    tunable_chan = channel2;
                    tunable_group = group2;
                }

                for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                    if (state->group_array[gi].groupNumber == (unsigned long)tunable_group) {
                        fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                        strncpy(mode, state->group_array[gi].groupMode, sizeof(mode) - 1);
                        mode[sizeof(mode) - 1] = '\0';
                        break;
                    }
                }

                //TG hold on MFID90 GRG -- block non-matching super group, allow matching group
                if (state->tg_hold != 0 && state->tg_hold != (uint32_t)tunable_group) {
                    sprintf(mode, "%s", "B");
                }
                if (state->tg_hold != 0 && state->tg_hold == (uint32_t)tunable_group) {
                    sprintf(mode, "%s", "A");
                }

                //check to see if the group candidate is blocked first
                if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                    if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && tunable_freq != 0) {
                        // ENC lockout conservative gating for MFID90 GRG Update without SVC bits
                        if (opts->trunk_tune_enc_calls == 0 && !p25_patch_tg_key_is_clear(state, tunable_group)
                            && !p25_patch_sg_key_is_clear(state, tunable_group)) {
                            goto SKIPCALL;
                        }
                        p25p2_mac_handle(&mac_res, opts, state, tunable_chan, /*svc_bits*/ 0, tunable_group, /*src*/ 0);
                        j = 8; //break loop
                    }
                }
                //if playing back files, and we still want to see what freqs are in use in the ncurses terminal
                //might only want to do these on a grant update, and not a grant by itself?
                if (opts->p25_trunk == 0) {
                    if (tunable_group == state->lasttg || tunable_group == state->lasttgR) {
                        //P1 FDMA
                        if (DSD_SYNC_IS_P25P1(state->synctype)) {
                            state->p25_vc_freq[0] = tunable_freq;
                        }
                        //P2 TDMA
                        else {
                            state->p25_vc_freq[0] = state->p25_vc_freq[1] = tunable_freq;
                        }
                    }
                }
            }
        }

        //Standard P25 Tunable Commands
        //Group Voice Channel Grant (GRP_V_CH_GRANT)
        if (MAC[1 + len_a] == 0x40) {
            int svc = MAC[2 + len_a];
            int channel = (MAC[3 + len_a] << 8) | MAC[4 + len_a];
            int group = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
            int source = (MAC[7 + len_a] << 16) | (MAC[8 + len_a] << 8) | MAC[9 + len_a];
            long int freq = 0;

            fprintf(stderr, "\n");
            if (svc & 0x80) {
                fprintf(stderr, " Emergency");
                state->p25_call_emergency[state->currentslot & 1] = 1;
            } else {
                state->p25_call_emergency[state->currentslot & 1] = 0;
            }
            if (svc & 0x40) {
                fprintf(stderr, " Encrypted");
            }
            // Track Packet/Data bit for this slot so XCCH can avoid audio gating
            state->p25_call_is_packet[state->currentslot & 1] = (uint8_t)((svc & 0x10) ? 1 : 0);

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
                state->p25_call_priority[state->currentslot & 1] = (uint8_t)(svc & 0x7);
            } else {
                state->p25_call_priority[state->currentslot & 1] = 0;
            }

            fprintf(stderr, " Group Voice Channel Grant");
            fprintf(stderr, "\n  SVC [%02X] CHAN [%04X] Group [%d] Source [%d]", svc, channel, group, source);
            freq = process_channel_to_freq(opts, state, channel);

            // Persist SVC bits for event history (per-slot)
            if ((state->currentslot & 1) == 0) {
                state->dmr_so = (uint16_t)svc;
            } else {
                state->dmr_soR = (uint16_t)svc;
            }

            //add active channel to string for ncurses display
            char suf_gvg[32];
            p25_format_chan_suffix(state, (uint16_t)channel, -1, suf_gvg, sizeof suf_gvg);
            sprintf(state->active_channel[0], "Active Ch: %04X%s TG: %d; ", channel, suf_gvg, group);
            state->last_active_time = time(NULL);

            for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                if (state->group_array[gi].groupNumber == (unsigned long)group) {
                    fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                    strncpy(mode, state->group_array[gi].groupMode, sizeof(mode) - 1);
                    mode[sizeof(mode) - 1] = '\0';
                    break;
                }
            }

            //TG hold on GRP_V -- block non-matching group, allow matching group
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

            //tune if tuning available (centralized)
            if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq != 0) {
                    p25p2_mac_handle(&mac_res, opts, state, channel, svc, group, source);
                }
            }
            //if playing back files, and we still want to see what freqs are in use in the ncurses terminal
            //might only want to do these on a grant update, and not a grant by itself?
            if (opts->p25_trunk == 0) {
                if (group == state->lasttg || group == state->lasttgR) {
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

        //Telephone Interconnect Voice Channel Grant (or Update) -- Implicit and Explicit (MFID != Moto and opcode 8 and opcode 9)
        if (MAC[1 + len_a] == 0x48 || MAC[1 + len_a] == 0x49 || MAC[1 + len_a] == 0xC8 || MAC[1 + len_a] == 0xC9) {
            //TELE_INT_CH_GRANT or TELE_INT_CH_GRANT_UPDT
            int k = 1; //vPDU
            if (MAC[len_a] == 0x07) {
                k = 0; //TSBK
            }
            int svc = MAC[2 + len_a + k];
            int channel = (MAC[3 + len_a + k] << 8) | MAC[4 + len_a + k];
            int timer = (MAC[5 + len_a + k] << 8) | MAC[6 + len_a + k];
            uint32_t target = (uint32_t)((MAC[7 + len_a + k] << 16) | (MAC[8 + len_a + k] << 8) | MAC[9 + len_a + k]);
            long int freq = 0;
            if (MAC[1 + len_a] & 0x80) //vPDU only
            {
                timer = (MAC[8 + len_a] << 8) | MAC[9 + len_a];
                target = (uint32_t)((MAC[10 + len_a] << 16) | (MAC[11 + len_a] << 8) | MAC[12 + len_a]);
            }

            fprintf(stderr, "\n");
            if (svc & 0x80) {
                fprintf(stderr, " Emergency");
                state->p25_call_emergency[state->currentslot & 1] = 1;
            } else {
                state->p25_call_emergency[state->currentslot & 1] = 0;
            }
            if (svc & 0x40) {
                fprintf(stderr, " Encrypted");
            }
            // Track Packet/Data bit for this slot so XCCH can avoid audio gating
            state->p25_call_is_packet[state->currentslot & 1] = (uint8_t)((svc & 0x10) ? 1 : 0);

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
                state->p25_call_priority[state->currentslot & 1] = (uint8_t)(svc & 0x7);
            } else {
                state->p25_call_priority[state->currentslot & 1] = 0;
            }

            fprintf(stderr, " Telephone Interconnect Voice Channel Grant");
            if (MAC[1 + len_a] & 0x01) {
                fprintf(stderr, " Update");
            }
            if (MAC[1 + len_a] & 0x80) {
                fprintf(stderr, " Explicit");
            } else {
                fprintf(stderr, " Implicit");
            }
            fprintf(stderr, "\n  CHAN: %04X; Timer: %f Seconds; Target: %d;", channel, (float)timer * 0.1f,
                    target); //timer unit is 100 ms, or 0.1 seconds
            freq = process_channel_to_freq(opts, state, channel);

            // Persist SVC bits for event history (per-slot)
            if ((state->currentslot & 1) == 0) {
                state->dmr_so = (uint16_t)svc;
            } else {
                state->dmr_soR = (uint16_t)svc;
            }

            //add active channel to string for ncurses display
            if (channel != 0 && channel != 0xFFFF) {
                char suf_tel[32];
                p25_format_chan_suffix(state, (uint16_t)channel, -1, suf_tel, sizeof suf_tel);
                sprintf(state->active_channel[0], "Active Tele Ch: %04X%s TGT: %u; ", channel, suf_tel, target);
            }
            state->last_active_time = time(NULL);

            //Skip tuning private calls if private calls is disabled (are telephone int calls private, or talkgroup?)
            if (opts->trunk_tune_private_calls == 0) {
                goto SKIPCALL;
            }

            //Skip tuning encrypted calls if enc calls are disabled (no event for telephone)
            if ((svc & 0x40) && opts->trunk_tune_enc_calls == 0) {
                goto SKIPCALL;
            }

            //telephone only has a target address (manual shows combined source/target of 24-bits)
            for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                if (state->group_array[gi].groupNumber == (unsigned long)target) {
                    fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                    strncpy(mode, state->group_array[gi].groupMode, sizeof(mode) - 1);
                    mode[sizeof(mode) - 1] = '\0';
                    break;
                }
            }

            //TG hold on UU_V -- will want to disable UU_V grants while TG Hold enabled -- same for Telephone?
            if (state->tg_hold != 0 && state->tg_hold != (uint32_t)target) {
                sprintf(mode, "%s", "B");
            }
            // if (state->tg_hold != 0 && state->tg_hold == target)
            // {
            // 	sprintf (mode, "%s", "A");
            // 	opts->p25_is_tuned = 0; //unlock tuner
            // }

            //tune if tuning available (centralized)
            if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq != 0) {
                    p25_sm_on_indiv_grant(opts, state, channel, svc, (int)target, /*src*/ 0);
                }
            }
            if (opts->p25_trunk == 0) {
                if ((uint32_t)target == (uint32_t)state->lasttg || (uint32_t)target == (uint32_t)state->lasttgR) {
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

        //Unit-to-Unit Voice Service Channel Grant (UU_V_CH_GRANT), or Grant Update (same format)
        if (MAC[1 + len_a] == 0x44 || MAC[1 + len_a] == 0x46 || MAC[1 + len_a] == 0xC4) //double check these opcodes
        {
            int channel = (MAC[2 + len_a] << 8) | MAC[3 + len_a];
            int target = (MAC[4 + len_a] << 16) | (MAC[5 + len_a] << 8) | MAC[6 + len_a];
            int source = (MAC[7 + len_a] << 16) | (MAC[8 + len_a] << 8) | MAC[9 + len_a];
            unsigned long long int src_suid = 0; // TODO: parse SUID for extended (0xC4) format
            long int freq = 0;

            fprintf(stderr, "\n Unit to Unit Channel Grant");
            if (MAC[1 + len_a] == 0x46) {
                fprintf(stderr, " Update");
            }
            if (MAC[1 + len_a] == 0xC4) {
                fprintf(stderr, " Extended");
            }
            fprintf(stderr, "\n  CHAN: %04X; SRC: %d; TGT: %d; ", channel, source, target);
            if (MAC[1 + len_a] == 0xC4) {
                fprintf(stderr, "SUID: %08llX-%08d; ", src_suid >> 24, source);
            }
            freq = process_channel_to_freq(opts, state, channel);

            //add active channel to string for ncurses display
            char suf_uug[32];
            p25_format_chan_suffix(state, (uint16_t)channel, -1, suf_uug, sizeof suf_uug);
            sprintf(state->active_channel[0], "Active Ch: %04X%s TGT: %d; ", channel, suf_uug, target);
            state->last_active_time = time(NULL);

            //Skip tuning private calls if private calls is disabled
            if (opts->trunk_tune_private_calls == 0) {
                goto SKIPCALL;
            }

            //Skip tuning encrypted calls if enc calls are disabled -- abb formats do not carry svc bits :(
            // if (opts->trunk_tune_enc_calls == 0) goto SKIPCALL; //enable, or disable?

            //unit to unit needs work, may fail under certain conditions (first blocked, second allowed, etc) (labels should still work though)
            for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                if (state->group_array[gi].groupNumber == (unsigned long)source
                    || state->group_array[gi].groupNumber == (unsigned long)target) {
                    fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                    strncpy(mode, state->group_array[gi].groupMode, sizeof(mode) - 1);
                    mode[sizeof(mode) - 1] = '\0';
                    break;
                }
            }

            //TG hold on UU_V -- will want to disable UU_V grants while TG Hold enabled
            if (state->tg_hold != 0 && state->tg_hold != (uint32_t)target) {
                sprintf(mode, "%s", "B");
            }
            // if (state->tg_hold != 0 && state->tg_hold == target)
            // {
            // 	sprintf (mode, "%s", "A");
            // 	opts->p25_is_tuned = 0; //unlock tuner
            // }

            //tune if tuning available (centralized)
            if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq != 0) {
                    // ENC lockout: these UU grants do not carry SVC bits; when enabled,
                    // skip tuning rather than risking an ENC follow.
                    if (opts->trunk_tune_enc_calls == 0) {
                        goto SKIPCALL;
                    }
                    p25_sm_on_indiv_grant(opts, state, channel, /*svc_bits*/ 0, target, source);
                }
            }
            if (opts->p25_trunk == 0) {
                if (target == state->lasttg || target == state->lasttgR) {
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

        //Group Voice Channel Grant Update Multiple - Explicit
        if (MAC[1 + len_a] == 0x25) {
            int svc1 = MAC[2 + len_a];
            int channelt1 = (MAC[3 + len_a] << 8) | MAC[4 + len_a];
            int channelr1 = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
            int group1 = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int svc2 = MAC[9 + len_a];
            int channelt2 = (MAC[10 + len_a] << 8) | MAC[11 + len_a];
            int channelr2 = (MAC[12 + len_a] << 8) | MAC[13 + len_a];
            int group2 = (MAC[14 + len_a] << 8) | MAC[15 + len_a];
            long int freq1t = 0;
            long int freq1r = 0;
            long int freq2t = 0;
            UNUSED(freq1r);

            fprintf(stderr, "\n Group Voice Channel Grant Update Multiple - Explicit");
            fprintf(stderr, "\n  SVC [%02X] CHAN-T [%04X] CHAN-R [%04X] Group [%d][%04X]", svc1, channelt1, channelr1,
                    group1, group1);
            if (svc1 & 0x80) {
                fprintf(stderr, " Emergency");
                state->p25_call_emergency[0] = 1;
            } else {
                state->p25_call_emergency[0] = 0;
            }
            if (svc1 & 0x40) {
                fprintf(stderr, " Encrypted");
            }
            if (opts->payload == 1) //hide behind payload due to len
            {
                if (svc1 & 0x20) {
                    fprintf(stderr, " Duplex");
                }
                if (svc1 & 0x10) {
                    fprintf(stderr, " Packet");
                } else {
                    fprintf(stderr, " Circuit");
                }
                if (svc1 & 0x8) {
                    fprintf(stderr, " R"); //reserved bit is on
                }
                fprintf(stderr, " Priority %d", svc1 & 0x7); //call priority
                state->p25_call_priority[0] = (uint8_t)(svc1 & 0x7);
            } else {
                state->p25_call_priority[0] = 0;
            }
            (void)process_channel_to_freq(opts, state, channelt1);
            if (channelr1 != 0 && channelr1 != 0xFFFF) {
                (void)process_channel_to_freq(opts, state, channelr1);
            }

            fprintf(stderr, "\n  SVC [%02X] CHAN-T [%04X] CHAN-R [%04X] Group [%d][%04X]", svc2, channelt2, channelr2,
                    group2, group2);
            if (svc2 & 0x80) {
                fprintf(stderr, " Emergency");
                state->p25_call_emergency[1] = 1;
            } else {
                state->p25_call_emergency[1] = 0;
            }
            if (svc2 & 0x40) {
                fprintf(stderr, " Encrypted");
            }
            if (opts->payload == 1) //hide behind payload due to len
            {
                if (svc2 & 0x20) {
                    fprintf(stderr, " Duplex");
                }
                if (svc2 & 0x10) {
                    fprintf(stderr, " Packet");
                } else {
                    fprintf(stderr, " Circuit");
                }
                if (svc2 & 0x8) {
                    fprintf(stderr, " R"); //reserved bit is on
                }
                fprintf(stderr, " Priority %d", svc2 & 0x7); //call priority
                state->p25_call_priority[1] = (uint8_t)(svc2 & 0x7);
            } else {
                state->p25_call_priority[1] = 0;
            }
            (void)process_channel_to_freq(opts, state, channelt2);
            if (channelr2 != 0 && channelr2 != 0xFFFF) {
                (void)process_channel_to_freq(opts, state, channelr2);
            }

            //add active channel to string for ncurses display
            {
                char suf_dcg1[32];
                char suf_dcg2[32];
                p25_format_chan_suffix(state, (uint16_t)channelt1, -1, suf_dcg1, sizeof suf_dcg1);
                p25_format_chan_suffix(state, (uint16_t)channelt2, -1, suf_dcg2, sizeof suf_dcg2);
                sprintf(state->active_channel[0], "Active Ch: %04X%s TG: %d; Ch: %04X%s TG: %d; ", channelt1, suf_dcg1,
                        group1, channelt2, suf_dcg2, group2);
            }
            state->last_active_time = time(NULL);

            //Skip tuning group calls if group calls are disabled
            if (opts->trunk_tune_group_calls == 0) {
                goto SKIPCALL;
            }

            //Skip tuning encrypted calls if enc calls are disabled (override if either TG has KEY=0000 policy)
            if ((svc1 & 0x40) && (svc2 & 0x40) && opts->trunk_tune_enc_calls == 0
                && !p25_patch_tg_key_is_clear(state, group1) && !p25_patch_tg_key_is_clear(state, group2)
                && !p25_patch_sg_key_is_clear(state, group1) && !p25_patch_sg_key_is_clear(state, group2)) {
                goto SKIPCALL;
            }

            int loop = 1;
            if (channelt1 == channelt2) {
                loop = 1;
            } else {
                loop = 2;
            }
            //assigned inside loop
            long int tunable_freq = 0;
            int tunable_chan = 0;
            int tunable_group = 0;

            for (int j = 0; j < loop; j++) {
                //test svc opts for enc to tune or skip
                if (j == 0) {
                    if ((svc1 & 0x40) && opts->trunk_tune_enc_calls == 0 && !p25_patch_tg_key_is_clear(state, group1)
                        && !p25_patch_sg_key_is_clear(state, group1)) {
                        j++; //skip to next
                    }
                }
                if (j == 1) {
                    if ((svc2 & 0x40) && opts->trunk_tune_enc_calls == 0 && !p25_patch_tg_key_is_clear(state, group2)
                        && !p25_patch_sg_key_is_clear(state, group2)) {
                        goto SKIPCALL; //skip to end
                    }
                }

                //assign our internal variables for check down on if to tune one freq/group or not
                if (j == 0) {
                    tunable_freq = freq1t;
                    tunable_chan = channelt1;
                    tunable_group = group1;
                } else {
                    tunable_freq = freq2t;
                    tunable_chan = channelt2;
                    tunable_group = group2;
                }
                for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                    if (state->group_array[gi].groupNumber == (unsigned long)tunable_group) {
                        fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                        strncpy(mode, state->group_array[gi].groupMode, sizeof(mode) - 1);
                        mode[sizeof(mode) - 1] = '\0';
                        break;
                    }
                }

                //TG hold on GRP_V Multi -- block non-matching group, allow matching group
                if (state->tg_hold != 0 && state->tg_hold != (uint32_t)tunable_group) {
                    sprintf(mode, "%s", "B");
                }
                if (state->tg_hold != 0 && state->tg_hold == (uint32_t)tunable_group) {
                    sprintf(mode, "%s", "A");
                }

                //tune if tuning available (centralized)
                if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                    if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && tunable_freq != 0) {
                        int svc_bits = (j == 0) ? svc1 : svc2;
                        p25p2_mac_handle(&mac_res, opts, state, tunable_chan, svc_bits, tunable_group, /*src*/ 0);
                    }
                }
                if (opts->p25_trunk == 0) {
                    if (tunable_group == state->lasttg || tunable_group == state->lasttgR) {
                        //P1 FDMA
                        if (DSD_SYNC_IS_P25P1(state->synctype)) {
                            state->p25_vc_freq[0] = tunable_freq;
                        }
                        //P2 TDMA
                        else {
                            state->p25_vc_freq[0] = state->p25_vc_freq[1] = tunable_freq;
                        }
                    }
                }
            }
        }

        //Group Voice Channel Grant Update Multiple - Implicit
        if (MAC[1 + len_a] == 0x05) {
            int so1 = MAC[2 + len_a];
            int channel1 = (MAC[3 + len_a] << 8) | MAC[4 + len_a];
            int group1 = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
            int so2 = MAC[7 + len_a];
            int channel2 = (MAC[8 + len_a] << 8) | MAC[9 + len_a];
            int group2 = (MAC[10 + len_a] << 8) | MAC[11 + len_a];
            int so3 = MAC[12 + len_a];
            int channel3 = (MAC[13 + len_a] << 8) | MAC[14 + len_a];
            int group3 = (MAC[15 + len_a] << 8) | MAC[16 + len_a];
            long int freq1 = 0;
            long int freq2 = 0;
            long int freq3 = 0;

            fprintf(stderr, "\n Group Voice Channel Grant Update Multiple - Implicit");
            fprintf(stderr, "\n  Channel 1 [%04X] Group 1 [%d][%04X]", channel1, group1, group1);
            if (so1 & 0x80) {
                fprintf(stderr, " Emergency");
            }
            if (so1 & 0x40) {
                fprintf(stderr, " Encrypted");
            }
            if (opts->payload == 1) //hide behind payload due to len
            {
                if (so1 & 0x20) {
                    fprintf(stderr, " Duplex");
                }
                if (so1 & 0x10) {
                    fprintf(stderr, " Packet");
                } else {
                    fprintf(stderr, " Circuit");
                }
                if (so1 & 0x8) {
                    fprintf(stderr, " R"); //reserved bit is on
                }
                fprintf(stderr, " Priority %d", so1 & 0x7); //call priority
            }
            freq1 = process_channel_to_freq(opts, state, channel1);

            if (channel2 != channel1 && channel2 != 0 && channel2 != 0xFFFF) {
                fprintf(stderr, "\n  Channel 2 [%04X] Group 2 [%d][%04X]", channel2, group2, group2);
                if (so2 & 0x80) {
                    fprintf(stderr, " Emergency");
                }
                if (so2 & 0x40) {
                    fprintf(stderr, " Encrypted");
                }
                if (opts->payload == 1) //hide behind payload due to len
                {
                    if (so2 & 0x20) {
                        fprintf(stderr, " Duplex");
                    }
                    if (so2 & 0x10) {
                        fprintf(stderr, " Packet");
                    } else {
                        fprintf(stderr, " Circuit");
                    }
                    if (so2 & 0x8) {
                        fprintf(stderr, " R"); //reserved bit is on
                    }
                    fprintf(stderr, " Priority %d", so2 & 0x7); //call priority
                }
                freq2 = process_channel_to_freq(opts, state, channel2);
            }
            if (channel3 != channel2 && channel3 != 0 && channel3 != 0xFFFF) {
                fprintf(stderr, "\n  Channel 3 [%04X] Group 3 [%d][%04X]", channel3, group3, group3);
                if (so3 & 0x80) {
                    fprintf(stderr, " Emergency");
                }
                if (so3 & 0x40) {
                    fprintf(stderr, " Encrypted");
                }
                if (opts->payload == 1) //hide behind payload due to len
                {
                    if (so3 & 0x20) {
                        fprintf(stderr, " Duplex");
                    }
                    if (so3 & 0x10) {
                        fprintf(stderr, " Packet");
                    } else {
                        fprintf(stderr, " Circuit");
                    }
                    if (so3 & 0x8) {
                        fprintf(stderr, " R"); //reserved bit is on
                    }
                    fprintf(stderr, " Priority %d", so3 & 0x7); //call priority
                }
                freq3 = process_channel_to_freq(opts, state, channel3);
            }

            //add active channel to string for ncurses display
            {
                char suf_sgc1[32];
                char suf_sgc2[32];
                char suf_sgc3[32];
                p25_format_chan_suffix(state, (uint16_t)channel1, -1, suf_sgc1, sizeof suf_sgc1);
                p25_format_chan_suffix(state, (uint16_t)channel2, -1, suf_sgc2, sizeof suf_sgc2);
                p25_format_chan_suffix(state, (uint16_t)channel3, -1, suf_sgc3, sizeof suf_sgc3);
                sprintf(state->active_channel[0], "Active Ch: %04X%s TG: %d; Ch: %04X%s TG: %d; Ch: %04X%s TG: %d; ",
                        channel1, suf_sgc1, group1, channel2, suf_sgc2, group2, channel3, suf_sgc3, group3);
            }

            //add active channel to string for ncurses display (multi check)
            // if (channel3 != channel2 && channel2 != channel1 && channel3 != 0 && channel3 != 0xFFFF)
            // 	sprintf (state->active_channel[0], "Active Ch: %04X TG: %d; Ch: %04X TG: %d; Ch: %04X TG: %d; ", channel1, group1, channel2, group2, channel3, group3);

            state->last_active_time = time(NULL);

            //Skip tuning group calls if group calls are disabled
            if (opts->trunk_tune_group_calls == 0) {
                goto SKIPCALL;
            }

            //Skip tuning encrypted calls if enc calls are disabled (override if any TG has KEY=0000 policy)
            if ((so1 & 0x40) && (so2 & 0x40) && (so3 & 0x40) && opts->trunk_tune_enc_calls == 0
                && !p25_patch_tg_key_is_clear(state, group1) && !p25_patch_tg_key_is_clear(state, group2)
                && !p25_patch_tg_key_is_clear(state, group3) && !p25_patch_sg_key_is_clear(state, group1)
                && !p25_patch_sg_key_is_clear(state, group2) && !p25_patch_sg_key_is_clear(state, group3)) {
                goto SKIPCALL; //this?
            }

            int loop = 3;

            long int tunable_freq = 0;
            int tunable_chan = 0;
            int tunable_group = 0;

            for (int j = 0; j < loop; j++) {
                //test svc opts for enc to tune or skip
                if (j == 0) {
                    if ((so1 & 0x40) && opts->trunk_tune_enc_calls == 0 && !p25_patch_tg_key_is_clear(state, group1)
                        && !p25_patch_sg_key_is_clear(state, group1)) {
                        j++; //skip to next
                    }
                }
                if (j == 1) {
                    if ((so2 & 0x40) && opts->trunk_tune_enc_calls == 0 && !p25_patch_tg_key_is_clear(state, group2)
                        && !p25_patch_sg_key_is_clear(state, group2)) {
                        j++; //skip to next
                    }
                }
                if (j == 2) {
                    if ((so3 & 0x40) && opts->trunk_tune_enc_calls == 0 && !p25_patch_tg_key_is_clear(state, group3)
                        && !p25_patch_sg_key_is_clear(state, group3)) {
                        goto SKIPCALL; //skip to end
                    }
                }

                //assign our internal variables for check down on if to tune one freq/group or not
                if (j == 0) {
                    tunable_freq = freq1;
                    tunable_chan = channel1;
                    tunable_group = group1;
                } else if (j == 1) {
                    tunable_freq = freq2;
                    tunable_chan = channel2;
                    tunable_group = group2;
                } else {
                    tunable_freq = freq3;
                    tunable_chan = channel3;
                    tunable_group = group3;
                }

                for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                    if (state->group_array[gi].groupNumber == (unsigned long)tunable_group) {
                        fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                        strncpy(mode, state->group_array[gi].groupMode, sizeof(mode) - 1);
                        mode[sizeof(mode) - 1] = '\0';
                        break;
                    }
                }

                //TG hold on GRP_V Multi -- block non-matching group, allow matching group
                if (state->tg_hold != 0 && state->tg_hold != (uint32_t)tunable_group) {
                    sprintf(mode, "%s", "B");
                }
                if (state->tg_hold != 0 && state->tg_hold == (uint32_t)tunable_group) {
                    sprintf(mode, "%s", "A");
                }

                //tune if tuning available (centralized)
                if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                    if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && tunable_freq != 0) {
                        int svc_bits = (j == 0) ? so1 : ((j == 1) ? so2 : so3);
                        p25p2_mac_handle(&mac_res, opts, state, tunable_chan, svc_bits, tunable_group, /*src*/ 0);
                        j = 8; //break loop after tuning one candidate
                    }
                }
                if (opts->p25_trunk == 0) {
                    if (tunable_group == state->lasttg || tunable_group == state->lasttgR) {
                        //P1 FDMA
                        if (DSD_SYNC_IS_P25P1(state->synctype)) {
                            state->p25_vc_freq[0] = tunable_freq;
                        }
                        //P2 TDMA
                        else {
                            state->p25_vc_freq[0] = state->p25_vc_freq[1] = tunable_freq;
                        }
                    }
                }
            }
        }

        //Group Voice Channel Grant Update - Implicit
        if (MAC[1 + len_a] == 0x42) {
            int channel1 = (MAC[2 + len_a] << 8) | MAC[3 + len_a];
            int group1 = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int channel2 = (MAC[6 + len_a] << 8) | MAC[7 + len_a];
            int group2 = (MAC[8 + len_a] << 8) | MAC[9 + len_a];
            long int freq1 = 0;
            long int freq2 = 0;

            fprintf(stderr, "\n Group Voice Channel Grant Update - Implicit");
            fprintf(stderr, "\n  Channel 1 [%04X] Group 1 [%d][%04X]", channel1, group1, group1);
            freq1 = process_channel_to_freq(opts, state, channel1);
            if (channel2 != channel1 && channel2 != 0 && channel2 != 0xFFFF) {
                fprintf(stderr, "\n  Channel 2 [%04X] Group 2 [%d][%04X]", channel2, group2, group2);
                freq2 = process_channel_to_freq(opts, state, channel2);
            }

            //add active channel to string for ncurses display
            if (channel2 != channel1 && channel2 != 0 && channel2 != 0xFFFF) {
                char suf_tgc1[32];
                char suf_tgc2[32];
                p25_format_chan_suffix(state, (uint16_t)channel1, -1, suf_tgc1, sizeof suf_tgc1);
                p25_format_chan_suffix(state, (uint16_t)channel2, -1, suf_tgc2, sizeof suf_tgc2);
                sprintf(state->active_channel[0], "Active Ch: %04X%s TG: %d; Ch: %04X%s TG: %d; ", channel1, suf_tgc1,
                        group1, channel2, suf_tgc2, group2);
            } else {
                char suf_tgc3[32];
                p25_format_chan_suffix(state, (uint16_t)channel1, -1, suf_tgc3, sizeof suf_tgc3);
                sprintf(state->active_channel[0], "Active Ch: %04X%s TG: %d; ", channel1, suf_tgc3, group1);
            }
            state->last_active_time = time(NULL);

            //Skip tuning group calls if group calls are disabled
            if (opts->trunk_tune_group_calls == 0) {
                goto SKIPCALL;
            }

            //Skip tuning encrypted calls if enc calls are disabled -- abb formats do not carry svc bits :(
            // if (opts->trunk_tune_enc_calls == 0) goto SKIPCALL; //enable, or disable?

            int loop = 1;
            if (channel1 == channel2) {
                loop = 1;
            } else {
                loop = 2;
            }
            //assigned inside loop
            long int tunable_freq = 0;
            int tunable_chan = 0;
            int tunable_group = 0;

            for (int j = 0; j < loop; j++) {
                //assign our internal variables for check down on if to tune one freq/group or not
                if (j == 0) {
                    tunable_freq = freq1;
                    tunable_chan = channel1;
                    tunable_group = group1;
                } else {
                    tunable_freq = freq2;
                    tunable_chan = channel2;
                    tunable_group = group2;
                }

                for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                    if (state->group_array[gi].groupNumber == (unsigned long)tunable_group) {
                        fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                        strncpy(mode, state->group_array[gi].groupMode, sizeof(mode) - 1);
                        mode[sizeof(mode) - 1] = '\0';
                        break;
                    }
                }

                //TG hold on GRP_V Multi -- block non-matching group, allow matching group
                if (state->tg_hold != 0 && state->tg_hold != (uint32_t)tunable_group) {
                    sprintf(mode, "%s", "B");
                }
                if (state->tg_hold != 0 && state->tg_hold == (uint32_t)tunable_group) {
                    sprintf(mode, "%s", "A");
                }

                //tune if tuning available (centralized)
                if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                    if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && tunable_freq != 0) {
                        p25p2_mac_handle(&mac_res, opts, state, tunable_chan, /*svc_bits*/ 0, tunable_group, /*src*/ 0);
                        j = 8; //break loop after first tune
                    }
                }
                if (opts->p25_trunk == 0) {
                    if (tunable_group == state->lasttg || tunable_group == state->lasttgR) {
                        //P1 FDMA
                        if (DSD_SYNC_IS_P25P1(state->synctype)) {
                            state->p25_vc_freq[0] = tunable_freq;
                        }
                        //P2 TDMA
                        else {
                            state->p25_vc_freq[0] = state->p25_vc_freq[1] = tunable_freq;
                        }
                    }
                }
            }
        }

        //Group Voice Channel Grant Update - Explicit
        if (MAC[1 + len_a] == 0xC3) {
            int svc = MAC[2 + len_a];
            int channelt = (MAC[3 + len_a] << 8) | MAC[4 + len_a];
            int channelr = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
            int group = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            long int freq1 = 0;
            long int freq2 = 0;
            UNUSED(freq2);

            fprintf(stderr, "\n");

            if (svc & 0x80) {
                fprintf(stderr, " Emergency");
                state->p25_call_emergency[state->currentslot & 1] = 1;
            } else {
                state->p25_call_emergency[state->currentslot & 1] = 0;
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
                state->p25_call_priority[state->currentslot & 1] = (uint8_t)(svc & 0x7);
            } else {
                state->p25_call_priority[state->currentslot & 1] = 0;
            }
            fprintf(stderr, " Group Voice Channel Grant Update - Explicit");
            fprintf(stderr, "\n  SVC [%02X] CHAN-T [%04X] CHAN-R [%04X] Group [%d][%04X]", svc, channelt, channelr,
                    group, group);
            freq1 = process_channel_to_freq(opts, state, channelt);
            if (channelr != 0 && channelr != 0xFFFF) {
                (void)process_channel_to_freq(
                    opts, state,
                    channelr); //one system had this as channel 0xFFFF -- look up any particular meaning for that
            }

            //don't set the tg here, one multiple grant updates, will mess up the TG value on current call
            // if (slot == 0)
            // {
            // 	state->lasttg = group;
            // }
            // else state->lasttgR = group;

            for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                if (state->group_array[gi].groupNumber == (unsigned long)group) {
                    fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                    snprintf(mode, sizeof mode, "%s", state->group_array[gi].groupMode);
                    break;
                }
            }

            //TG hold on GRP_V Exp -- block non-matching group, allow matching group
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

            //tune if tuning available (centralized)
            if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq1 != 0) {
                    p25p2_mac_handle(&mac_res, opts, state, channelt, svc, group, /*src*/ 0);
                }
            }
            if (opts->p25_trunk == 0) {
                if (group == state->lasttg || group == state->lasttgR) {
                    //P1 FDMA
                    if (DSD_SYNC_IS_P25P1(state->synctype)) {
                        state->p25_vc_freq[0] = freq1;
                    }
                    //P2 TDMA
                    else {
                        state->p25_vc_freq[0] = state->p25_vc_freq[1] = freq1;
                    }
                }
            }
        }

        //SNDCP Data Page Response -- ISP
        // if (MAC[1+len_a] == 0x53)
        // {
        // 	fprintf (stderr, "\n SNDCP Data Page Response ");
        // 	int dso = MAC[3+len_a];
        // 	int ans = MAC[4+len_a];
        // 	int dac = (MAC[5+len_a] << 8) | MAC[6+len_a];
        // 	int source  = (MAC[7+len_a] << 16) | (MAC[8+len_a] << 8) | MAC[9+len_a];
        // 	fprintf (stderr, "\n ANS: %02X; DSO: %02X DAC: %02X; Source: %d;", ans, dso, dac, source);
        // 	if (ans == 0x20)      fprintf (stderr, " - Proceed;");
        // 	else if (ans == 0x21) fprintf (stderr, " - Deny;");
        // 	else if (ans == 0x22) fprintf (stderr, " - Wait;");
        // 	else                  fprintf (stderr, " - Other;"); //unspecified
        // }

        //SNDCP Reconnect Request -- ISP
        // if (MAC[1+len_a] == 0x54)
        // {
        // 	fprintf (stderr, "\n SNDCP Reconnect Request ");
        // 	int dso = MAC[3+len_a];
        // 	int dac = (MAC[4+len_a] << 8) | MAC[5+len_a];
        // 	int ds  = (MAC[6+len_a] >> 7) & 1;
        // 	int res = MAC[6+len_a] & 7;
        // 	int source  = (MAC[7+len_a] << 16) | (MAC[8+len_a] << 8) | MAC[9+len_a];
        // 	fprintf (stderr, "\n DS: %d; DSO: %02X DAC: %02X; RES: %02X; Source: %d;", ds, dso, dac, res, source);
        // }

        //SNDCP Data Channel Grant
        if (MAC[1 + len_a] == 0x54) {
            fprintf(stderr, "\n SNDCP Data Channel Grant - Explicit");
            int dso = MAC[2 + len_a];
            int channelt = (MAC[3 + len_a] << 8) | MAC[4 + len_a];
            int channelr = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
            int target = (MAC[7 + len_a] << 16) | (MAC[8 + len_a] << 8) | MAC[9 + len_a];
            fprintf(stderr, "\n  DSO: %02X; CHAN-T: %04X; CHAN-R: %04X; Target: %d;", dso, channelt, channelr, target);

            for (unsigned int gi = 0; gi < state->group_tally; gi++) {
                if (state->group_array[gi].groupNumber == (unsigned long)target) {
                    fprintf(stderr, " [%s]", state->group_array[gi].groupName);
                    snprintf(mode, sizeof mode, "%s", state->group_array[gi].groupMode);
                    break;
                }
            }

            long int freq = process_channel_to_freq(opts, state, channelt);

            //add active channel to string for ncurses display
            {
                char suf_dat[32];
                p25_format_chan_suffix(state, (uint16_t)channelt, -1, suf_dat, sizeof suf_dat);
                sprintf(state->active_channel[0], "Active Data Ch: %04X%s TGT: %d; ", channelt, suf_dat, target);
            }
            state->last_active_time = time(NULL);

            //Skip tuning data calls if data calls is disabled
            if (opts->trunk_tune_data_calls == 0) {
                goto SKIPCALL;
            }

            //TG hold on UU_V -- will want to disable UU_V grants while TG Hold enabled
            if (state->tg_hold != 0 && state->tg_hold != (uint32_t)target) {
                sprintf(mode, "%s", "B");
            }

            //tune if tuning available (centralized)
            if (opts->p25_trunk == 1 && (strcmp(mode, "DE") != 0) && (strcmp(mode, "B") != 0)) {
                if (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && freq != 0) {
                    // ENC lockout: no SVC bits in this UU data form; be conservative
                    if (opts->trunk_tune_enc_calls == 0) {
                        goto SKIPCALL;
                    }
                    p25_sm_on_indiv_grant(opts, state, channelt, /*svc_bits*/ 0, (int)target, /*src*/ 0);
                }
            }
            if (opts->p25_trunk == 0) {
                if (target == state->lasttg || target == state->lasttgR) {
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

        //SNDCP Data Page Request
        if (MAC[1 + len_a] == 0x55) {
            fprintf(stderr, "\n SNDCP Data Page Request ");
            int dso = MAC[2 + len_a];
            int dac = (MAC[3 + len_a] << 8) | MAC[4 + len_a];
            int target = (MAC[5 + len_a] << 16) | (MAC[6 + len_a] << 8) | MAC[7 + len_a];
            //P25p1 TSBK is shifted slightly on these two values
            if (DSD_SYNC_IS_P25P1(state->synctype)) {
                dac = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
                target = (MAC[7 + len_a] << 16) | (MAC[8 + len_a] << 8) | MAC[9 + len_a];
            }
            fprintf(stderr, "\n  DSO: %02X; DAC: %02X; Target: %d;", dso, dac, target);
        }

        // SNDCP (opcode 0x56 in MAC = TSBK opcode 0x16 ^ 0x40)
        // Minimal handling for P1 TSBK SNDCP data-channel messages per OP25 parity
        if (MAC[1 + len_a] == 0x56) {
            int ch1 = (MAC[2 + len_a] << 8) | MAC[3 + len_a];
            int ch2 = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            fprintf(stderr, "\n SNDCP (P1 TSBK) CH1 [%04X] CH2 [%04X]", ch1, ch2);
            // Minimal handling: just log channels, no frequency conversion per OP25 approach
        }

        //SNDCP Data Channel Announcement
        if (MAC[1 + len_a] == 0xD6) {
            fprintf(stderr, "\n SNDCP Data Channel Announcement ");
            int aa = (MAC[2 + len_a] >> 7) & 1;
            int ra = (MAC[2 + len_a] >> 6) & 1;
            int dso = MAC[2 + len_a];
            int channelt = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int channelr = (MAC[6 + len_a] << 8) | MAC[7 + len_a];
            int dac = (MAC[8 + len_a] << 8) | MAC[9 + len_a];
            fprintf(stderr, "\n  AA: %d; RA: %d; DSO: %02X; DAC: %02X; CHAN-T: %04X; CHAN-R: %04X;", aa, ra, dso, dac,
                    channelt, channelr);
            long int freq = 0;
            UNUSED(freq);
            if (channelt != 0) {
                (void)process_channel_to_freq(opts, state, channelt);
            }
            if (channelr != 0) {
                (void)process_channel_to_freq(opts, state, channelt);
            }
        }

        // MFID90 Group Regroup Add Command (0x81)
        // Layout: opcode(8)=0x81, mfid(8)=0x90, wg_len(6), sg(16), wg1(16), wg2(16)...
        if (MAC[1 + len_a] == 0x81 && MAC[2 + len_a] == 0x90) {
            int wg_len = MAC[3 + len_a] & 0x3F; // length in bytes of workgroup list
            int sg = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            fprintf(stderr, "\n MFID90 (Moto) Group Regroup Add Command\n");
            fprintf(stderr, "  SG: %d", sg);
            // Workgroup IDs start at byte 6, each is 16 bits
            int num_wg = (wg_len >= 2) ? ((wg_len - 2) / 2) : 0; // subtract sg bytes, divide by 2
            for (int wi = 0; wi < num_wg && (6 + len_a + wi * 2 + 1) < 24; wi++) {
                int wg = (MAC[6 + len_a + wi * 2] << 8) | MAC[6 + len_a + wi * 2 + 1];
                if (wg != 0) {
                    fprintf(stderr, " WG%d: %d", wi + 1, wg);
                    p25_patch_add_wgid(state, sg, wg);
                }
            }
            fprintf(stderr, "\n");
            p25_patch_update(state, sg, /*is_patch*/ 1, /*active*/ 1);
        }

        // MFID90 Group Regroup Delete Command (0x89)
        // Layout: opcode(8)=0x89, mfid(8)=0x90, wg_len(6), sg(16), wg1(16), wg2(16)...
        if (MAC[1 + len_a] == 0x89 && MAC[2 + len_a] == 0x90) {
            int wg_len = MAC[3 + len_a] & 0x3F;
            int sg = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            fprintf(stderr, "\n MFID90 (Moto) Group Regroup Delete Command\n");
            fprintf(stderr, "  SG: %d", sg);
            int num_wg = (wg_len >= 2) ? ((wg_len - 2) / 2) : 0;
            for (int wi = 0; wi < num_wg && (6 + len_a + wi * 2 + 1) < 24; wi++) {
                int wg = (MAC[6 + len_a + wi * 2] << 8) | MAC[6 + len_a + wi * 2 + 1];
                if (wg != 0) {
                    fprintf(stderr, " WG%d: %d", wi + 1, wg);
                    p25_patch_remove_wgid(state, sg, wg);
                }
            }
            fprintf(stderr, "\n");
        }

        // MFID90 Group Regroup Voice Channel Update (0x83)
        // Layout: opcode(8)=0x83, mfid(8)=0x90, reserved(8), sg(16), ch(16)
        if (MAC[1 + len_a] == 0x83 && MAC[2 + len_a] == 0x90) {
            int sg = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int channel = (MAC[6 + len_a] << 8) | MAC[7 + len_a];
            fprintf(stderr, "\n MFID90 (Moto) Group Regroup Voice Channel Update\n");
            fprintf(stderr, "  SG: %d CHAN [%04X]", sg, channel);
            long int freq = process_channel_to_freq(opts, state, channel);
            char suf[32];
            p25_format_chan_suffix(state, (uint16_t)channel, -1, suf, sizeof suf);
            sprintf(state->active_channel[slot], "MFID90 GRG VCH Upd: %04X%s SG: %d; ", channel, suf, sg);
            state->last_active_time = time(NULL);
            fprintf(stderr, "\n");
            // Route through SM for tuning consideration
            if (opts->p25_trunk == 1 && channel != 0 && freq != 0) {
                p25_sm_on_group_grant(opts, state, channel, /*svc*/ 0, sg, /*src*/ 0);
            }
        }

        // MFID90 Motorola System Information / BSI (MAC opcode 0x05)
        if (MAC[1 + len_a] == 0x05 && MAC[2 + len_a] == 0x90) {
            fprintf(stderr, "\n MFID90 (Moto) System Broadcast (BSI)\n");
            fprintf(stderr, "  Data:");
            for (int bi = 3; bi <= 9 && (bi + len_a) < 24; bi++) {
                fprintf(stderr, " %02llX", MAC[bi + len_a]);
            }
            // Show computed callsign from current WACN/SysID if available
            if (opts->show_p25_callsign_decode && (state->p2_wacn != 0 || state->p2_sysid != 0)) {
                char callsign[7];
                p25_wacn_sysid_to_callsign((uint32_t)state->p2_wacn, (uint16_t)state->p2_sysid, callsign);
                fprintf(stderr, " [%s]", callsign);
            }
            fprintf(stderr, "\n");
        }

        //the len on these indicate they are always a single messages, foregoing the +len_a index pointer
        if (MAC[1] == 0x91 && MAC[2] == 0x90) {
            uint8_t len = MAC[3]; //this indication is correct, 0x11, or 17 octets including opcode and mfid
            uint8_t mac_bits[24 * 8];
            memset(mac_bits, 0, sizeof(mac_bits));
            uint8_t bytes[24];
            memset(bytes, 0, sizeof(bytes));
            for (int i = 0; i < 24; i++) {
                bytes[i] = (uint8_t)MAC[i];
            }
            unpack_byte_array_into_bit_array(bytes + 1, mac_bits, len);
            fprintf(stderr, "\n MFID90 (Moto) Talker Alias Header");
            apx_embedded_alias_header_phase2(opts, state, state->currentslot, mac_bits);
        }

        if (MAC[1] == 0x95 && MAC[2] == 0x90) {
            uint8_t len = MAC[3]; //this indication is correct, 0x11, or 17 octets including opcode and mfid
            uint8_t mac_bits[24 * 8];
            memset(mac_bits, 0, sizeof(mac_bits));
            uint8_t bytes[24];
            memset(bytes, 0, sizeof(bytes));
            for (int i = 0; i < 24; i++) {
                bytes[i] = (uint8_t)MAC[i];
            }
            unpack_byte_array_into_bit_array(bytes + 1, mac_bits, len);
            fprintf(stderr, "\n MFID90 (Moto) Talker Alias Blocks");
            apx_embedded_alias_blocks_phase2(opts, state, state->currentslot, mac_bits);
        }

        //System Service Broadcast
        if (MAC[1 + len_a] == 0x78) {
            int TWV = MAC[2 + len_a]; //TWUID Validity
            int SSA = (MAC[3 + len_a] << 16) | (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int SSS = (MAC[6 + len_a] << 16) | (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int RPL = MAC[9 + len_a];
            fprintf(stderr, "\n System Service Broadcast - Abbreviated \n");
            fprintf(stderr, "  TWV: %02X SSA: %06X; SSS: %06X; RPL: %02X", TWV, SSA, SSS, RPL);
        }

        //RFSS Status Broadcast - Implicit
        if (MAC[1 + len_a] == 0x7A) {
            int lra = MAC[2 + len_a];
            int lsysid = ((MAC[3 + len_a] & 0xF) << 8) | MAC[4 + len_a];
            int rfssid = MAC[5 + len_a];
            int siteid = MAC[6 + len_a];
            int channel = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int sysclass = MAC[9 + len_a];
            fprintf(stderr, "\n RFSS Status Broadcast - Implicit \n");
            fprintf(stderr, "  LRA [%02X] SYSID [%03X] RFSS ID [%03d] SITE ID [%03d] CHAN [%04X] SSC [%02X] ", lra,
                    lsysid, rfssid, siteid, channel, sysclass);
            process_channel_to_freq(opts, state, channel);

            state->p2_siteid = siteid;
            state->p2_rfssid = rfssid;
            // Promote any matching IDENs to trusted on site identification
            p25_confirm_idens_for_current_site(state);
        }

        //RFSS Status Broadcast - Explicit
        if (MAC[1 + len_a] == 0xFA) {
            int lra = MAC[2 + len_a];
            int lsysid = ((MAC[3 + len_a] & 0xF) << 8) | MAC[4 + len_a];
            int rfssid = MAC[5 + len_a];
            int siteid = MAC[6 + len_a];
            int channelt = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int channelr = (MAC[9 + len_a] << 8) | MAC[10 + len_a];
            int sysclass = MAC[11 + len_a];
            fprintf(stderr, "\n RFSS Status Broadcast - Explicit \n");
            fprintf(
                stderr,
                "  LRA [%02X] SYSID [%03X] RFSS ID [%03d] SITE ID [%03d]\n  CHAN-T [%04X] CHAN-R [%02X] SSC [%02X] ",
                lra, lsysid, rfssid, siteid, channelt, channelr, sysclass);
            process_channel_to_freq(opts, state, channelt);
            process_channel_to_freq(opts, state, channelr);

            state->p2_siteid = siteid;
            state->p2_rfssid = rfssid;
            p25_confirm_idens_for_current_site(state);
        }

        //Unit-to-Unit Answer Request (UU_ANS_REQ) -- ISP?
        // if (MAC[1+len_a] == 0x45 && MAC[2+len_a] != 0x90)
        // {
        // 	int svc     = MAC[2+len_a];
        // 	int ans  = MAC[3+len_a];
        // 	int target  = (MAC[4+len_a] << 16) | (MAC[5+len_a] << 8) | MAC[6+len_a];
        // 	int source  = (MAC[7+len_a] << 16) | (MAC[8+len_a] << 8) | MAC[9+len_a];

        // 	fprintf (stderr, "\n Unit to Unit Channel Answer Request");
        // 	fprintf (stderr, "\n  SVC [%02X] Answer [%02X] Source [%d] Target [%d]", svc, ans, source, target);

        // 	if (ans == 0x20)      fprintf (stderr, " - Proceed;");
        // 	else if (ans == 0x21) fprintf (stderr, " - Deny;");
        // 	else if (ans == 0x22) fprintf (stderr, " - Wait;");
        // 	else                  fprintf (stderr, " - Other;"); //unspecified

        // }

        //TODO: Restructure this function to group standard Opcodes away from MFID 90 and MFID A4, and Unknown MFID
        //or just go to if-elseif-else layout (probably easier that way)
        //Harris A4 Opcodes
        if (MAC[1 + len_a] != 0xB0 && MAC[2 + len_a] == 0xA4) {
            // 6.2.36 Manufacturer Specific regarding octet 3 as len
            int len = MAC[3 + len_a] & 0x3F;
            int res = MAC[3 + len_a] >> 6;

            //sanity check that we don't exceed the max MAC array size
            if (len > 24) {
                len = 24; //should never exceed this len, but just in case it does
            }

            //Harris "Talker" Alias
            if (MAC[1 + len_a] == 0xA8) {
                // fprintf (stderr, "\n MFID A4 (Harris); VCH %d; TG: %d; SRC: %d; Talker Alias: ", slot, ttg, tsrc);
                fprintf(stderr, "\n MFID A4 (Harris); VCH %d;", slot);
                uint8_t bytes[24];
                memset(bytes, 0, sizeof(bytes));
                for (int8_t i = 0; i < 24; i++) {
                    bytes[i] = (uint8_t)MAC[i];
                }
                l3h_embedded_alias_decode(opts, state, slot, len, bytes);
            }

            else if (MAC[1 + len_a]
                     == 0x81) //speculative based on the EDACS message that is also flushed with all F hex values
            {
                fprintf(stderr, "\n MFID A4 (Harris) Group Regroup Bitmap: ");
                for (i = 4; i <= len; i++) {
                    fprintf(stderr, "%02llX", MAC[i + len_a]);
                }
            }

            else {
                fprintf(stderr, "\n MFID A4 (Harris); Res: %d; Len: %d; Opcode: %02llX; ", res, len,
                        MAC[1 + len_a] & 0x3F); //first two bits are the b0 and b1
                for (i = 4; i <= len; i++) {
                    fprintf(stderr, "%02llX", MAC[i + len_a]);
                }
            }

            //assign here so we don't read an extra opcode value, like MAC Release on FL-DCC-1 (0x31 opcode)
            len_b = len;
        }

        //This is now confirmed to have the Harris Talker GPS, but the structure is unusual compared to other MFID messages,
        //the A4 indicator is one octet more to the 'right' than is normative, so I cannot verify the len value (0x11, or 17).
        //Its possible the 0x80 is the 'Manufacturer Message' Opcode, which in the manual shows the rest of the
        //octets are not normative, and as such, can't say there is a len value (but should assume entire the SACCH/FACCH field)
        if (MAC[len_a + 1] == 0x80 && MAC[len_a + 2] != 0xA4 && MAC[len_a + 2] != 0x90) {
            int unk1 = MAC
                [len_a
                 + 1]; //assuming this is the octet set for the 'manufacturer specific' message, may only be the MSBit
            int unk2 = MAC[len_a + 2]; //This field is observed as 0xAA, unknown if this is an opcode, or other MFID
            int mfid =
                MAC[len_a + 3]; //This is where the 0xA4 (Harris) Identifier is found in this message, as opposed to +2
            int len = MAC[len_a + 4] & 0x3F;
            ; //0x11 or 17 dec sounds reasonable, but cannot verify
            fprintf(stderr, "\n MFID %02X (Harris); Len: %d; Opcode: %02X/%02X;", mfid, len, unk1, unk2);

            //convert bytes to bits, may move this up top
            uint8_t mac_bits[24 * 8];
            memset(mac_bits, 0, sizeof(mac_bits));
            int l, x, z = 0;
            for (l = 0; l < 24; l++) {
                for (x = 0; x < 8; x++) {
                    mac_bits[z++] = (((uint8_t)MAC[l] << x) & 0x80) >> 7;
                }
            }

            int tsrc = 0;
            if (slot == 0 && state->lastsrc != 0) {
                tsrc = state->lastsrc;
            }
            if (slot == 1 && state->lastsrcR != 0) {
                tsrc = state->lastsrcR;
            }

            // harris_gps (opts, state, slot, mac_bits); //fallback
            nmea_harris(opts, state, mac_bits + 0, tsrc, slot); //new

            //debug - just dump payload
            // for (i = 0; i < 24; i++)
            // 		fprintf (stderr, " %02llX", MAC[i]);

            len_b = 17;
        }

        //Tait Observed "tdma micro-slot counter", this can be a single message in a facch idle, or a ndary message in a sacch channel update
        if (MAC[len_a + 1] == 0xB5 && MAC[len_a + 2] == 0xD8) {
            uint8_t mfid = MAC[len_a + 2];
            uint16_t sc =
                (MAC[len_a + 4] << 8)
                + MAC[len_a + 5]; //its possible that not all bits in these two bytes are slots, similar to sync_bcst
            uint8_t len =
                MAC[len_a + 3] & 0x3F; //5 on this opcode (including opcode, mfid, len, and two bytes for slot count)

            //confirmed, this is the same as the sync_bcst slot counter value up to 8000 (0x1F40)
            sc &= 0x1FFF;

            fprintf(stderr, "\n MFID %02X (Tait); Len: %d; Micro Slot Counter: %04X;", mfid, len, sc);

            len_b = 5;
        }

        //look for other unknown Tait opcodes
        if (MAC[len_a + 1] != 0xB5 && MAC[len_a + 2] == 0xD8) {
            uint8_t mfid = MAC[len_a + 2];
            uint8_t len = MAC[len_a + 3] & 0x3F;
            fprintf(stderr, "\n MFID %02X (Tait); Len: %d; Opcode: %02llX;", mfid, len, MAC[len_a + 1]);

            //sanity check
            if (len > 24) {
                len = 24;
            }

            //dump entire payload
            fprintf(stderr, " Payload: ");
            for (i = 4; i < len; i++) {
                fprintf(stderr, "%02llX", MAC[i + len_a]);
            }

            len_b = len;
        }

        /*
		Maybe this is why some of these SYNC_BCST PDUs have zeroes in most fields

		Information provided in the SYNC_BCST message may be used for purposes other
		than providing synchronization to a TDMA voice channel. These purposes are beyond
		the scope of this document. SUs not interested in synchronization with the TDMA voice
		channel may ignore the status of the US bit.
		*/

        //Synchronization Broadcast (SYNC_BCST)
        if (MAC[1 + len_a] == 0x70) {
            //NOTE: I've observed the minute value on Harris (Duke) does not work the same (rolls over every ~3 minutes)
            //as it does on a Moto system (actual minute value), may want to expose and check mm or mc bits (AABC-D Page 199-200)
            fprintf(stderr, "\n Synchronization Broadcast");
            int us = (MAC[3 + len_a] >> 3) & 0x1;  //synced or unsynced FDMA to TDMA
            int ist = (MAC[3 + len_a] >> 2) & 0x1; //IST bit tells us if time is realiable, synced to external source
            int mm = (MAC[3 + len_a] >> 1) & 0x1;  //Minute / Microslot Boundary Unlocked
            int mc = (((MAC[3 + len_a] >> 0) & 0x1) << 1) + (((MAC[4 + len_a] >> 7) & 0x1) << 0); //Minute Correction
            int vl = (MAC[4 + len_a] >> 6) & 0x1; //Local Time Offset if Valid
            int ltoff = (MAC[4 + len_a] & 0x3F);
            int year = MAC[5 + len_a] >> 1;
            int month = ((MAC[5 + len_a] & 0x1) << 3) | (MAC[6 + len_a] >> 5);
            int day = (MAC[6 + len_a] & 0x1F);
            int hour = MAC[7 + len_a] >> 3;
            int min = ((MAC[7 + len_a] & 0x7) << 3) | (MAC[8 + len_a] >> 5);
            int slots = ((MAC[8 + len_a] & 0x1F) << 8) | MAC[9 + len_a];
            int sign = (ltoff & 0x20) >> 5;
            float offhour = 0;

            if (opts->payload == 1) {
                fprintf(stderr, "\n");
                if (us) {
                    fprintf(stderr, " Unsynchronized Slots;");
                }
                if (ist) {
                    fprintf(stderr, " External System Time Sync;");
                }
                if (mm) {
                    fprintf(stderr, " Minute / Microslots Boundary Unlocked;"); //just a rolling counter
                }
                if (mc) {
                    fprintf(stderr, " Minute Correction: +%.01f ms;", (float)mc * 2.5f);
                }
                if (vl) {
                    fprintf(stderr, " Local Time Offset Valid;");
                }
            }

            //calculate local time (on system) by looking at offset and subtracting 30 minutes increments, or divide by 2 for hourly
            if (sign == 1) {
                offhour = -((float)(ltoff & 0x1F) / 2.0f);
            } else {
                offhour = ((float)(ltoff & 0x1F) / 2.0f);
            }

            int seconds = slots / 135; //very rough estimation, but may be close enough for grins
            if (seconds > 59) {
                seconds = 59; //sanity check for rounding error
            }

            if (year != 0) //if time is synced in this PDU
            {
                fprintf(stderr, "\n  Date: 20%02d.%02d.%02d Time: %02d:%02d:%02d UTC", year, month, day, hour, min,
                        seconds);
                if (offhour != 0) { //&& vl == 1
                    fprintf(stderr, "\n  Local Time Offset: %.01f Hours;", offhour);
                }
            }
            if (opts->payload == 1) {
                fprintf(stderr, "\n US: %d; IST: %d; MM: %d; MC: %d; VL: %d; Sync Slots: %d; ", us, ist, mm, mc, vl,
                        slots);
            }
        }

        //identifier update VHF/UHF
        if (MAC[1 + len_a] == 0x74) {
            state->p25_chan_iden = MAC[2 + len_a] >> 4;
            int iden = state->p25_chan_iden;
            int bw_vu = (MAC[2 + len_a] & 0xF);
            state->p25_trans_off[iden] = (MAC[3 + len_a] << 6) | (MAC[4 + len_a] >> 2);
            state->p25_chan_spac[iden] = ((MAC[4 + len_a] & 0x3) << 8) | MAC[5 + len_a];
            state->p25_base_freq[iden] =
                (MAC[6 + len_a] << 24) | (MAC[7 + len_a] << 16) | (MAC[8 + len_a] << 8) | (MAC[9 + len_a] << 0);

            //this is causing more issues -- need a different way to check on this
            // if (state->p25_chan_spac[iden] == 0x64) //tdma
            // {
            // 	state->p25_chan_type[iden] = 4; //
            // 	state->p25_chan_tdma[iden] = 1; //
            // }
            // else //fdma
            // {
            // 	state->p25_chan_type[iden] = 1; //
            // 	state->p25_chan_tdma[iden] = 0; //
            // }

            state->p25_chan_type[iden] = 1; //set as old values for now
            // Preserve explicit hints. When unknown, prefer TDMA if the system has
            // already shown P25p2 voice so we do not misclassify VC grants as FDMA.
            if (state->p25_chan_tdma_explicit[iden] == 0) {
                int tdma_hint = (state->p25_sys_is_tdma == 1) ? 1 : (state->p25_chan_tdma[iden] & 0x1);
                state->p25_chan_tdma[iden] = tdma_hint;
                state->p25_chan_tdma_explicit[iden] = 0; // unknown/implicit
            }

            // Record provenance (use current system/site context); trust if on CC
            state->p25_iden_wacn[iden] = state->p2_wacn;
            state->p25_iden_sysid[iden] = state->p2_sysid;
            state->p25_iden_rfss[iden] = state->p2_rfssid;
            state->p25_iden_site[iden] = state->p2_siteid;
            state->p25_iden_trust[iden] = (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0) ? 2 : 1;

            fprintf(stderr, "\n Identifier Update UHF/VHF\n");
            fprintf(stderr,
                    "  Channel Identifier [%01X] BW [%01X] Transmit Offset [%04X]\n  Channel Spacing [%03X] Base "
                    "Frequency [%08lX] [%09ld]",
                    state->p25_chan_iden, bw_vu, state->p25_trans_off[iden], state->p25_chan_spac[iden],
                    state->p25_base_freq[iden], state->p25_base_freq[iden] * 5);
        }

        //identifier update (Non-TDMA 6.2.22) (Non-VHF-UHF) //with signed offset, bit trans_off >> 8; bit number 9
        if (MAC[1 + len_a] == 0x7D) {
            state->p25_chan_iden = MAC[2 + len_a] >> 4;
            int iden = state->p25_chan_iden;

            state->p25_chan_type[iden] = 1;
            state->p25_chan_tdma[iden] = 0;
            state->p25_chan_tdma_explicit[iden] = 1; // explicit Non-TDMA
            int bw = ((MAC[2 + len_a] & 0xF) << 5) | ((MAC[3 + len_a] & 0xF8) >> 2);
            state->p25_trans_off[iden] = (MAC[3 + len_a] << 6) | (MAC[4 + len_a] >> 2);
            state->p25_chan_spac[iden] = ((MAC[4 + len_a] & 0x3) << 8) | MAC[5 + len_a];
            state->p25_base_freq[iden] =
                (MAC[6 + len_a] << 24) | (MAC[7 + len_a] << 16) | (MAC[8 + len_a] << 8) | (MAC[9 + len_a] << 0);

            // Record provenance (use current system/site context); trust if on CC
            state->p25_iden_wacn[iden] = state->p2_wacn;
            state->p25_iden_sysid[iden] = state->p2_sysid;
            state->p25_iden_rfss[iden] = state->p2_rfssid;
            state->p25_iden_site[iden] = state->p2_siteid;
            state->p25_iden_trust[iden] = (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0) ? 2 : 1;

            fprintf(stderr, "\n Identifier Update (8.3.1.23)\n");
            fprintf(stderr,
                    "  Channel Identifier [%01X] BW [%01X] Transmit Offset [%04X]\n  Channel Spacing [%03X] Base "
                    "Frequency [%08lX] [%09ld]",
                    state->p25_chan_iden, bw, state->p25_trans_off[iden], state->p25_chan_spac[iden],
                    state->p25_base_freq[iden], state->p25_base_freq[iden] * 5);
        }

        //identifier update for TDMA, Abbreviated
        if (MAC[1 + len_a] == 0x73) {
            state->p25_chan_iden = MAC[2 + len_a] >> 4;
            int iden = state->p25_chan_iden;
            state->p25_chan_tdma[iden] = 1;
            state->p25_chan_tdma_explicit[iden] = 2; // explicit TDMA
            state->p25_chan_type[iden] = MAC[2 + len_a] & 0xF;
            state->p25_trans_off[iden] = (MAC[3 + len_a] << 6) | (MAC[4 + len_a] >> 2);
            state->p25_chan_spac[iden] = ((MAC[4 + len_a] & 0x3) << 8) | MAC[5 + len_a];
            state->p25_base_freq[iden] =
                (MAC[6 + len_a] << 24) | (MAC[7 + len_a] << 16) | (MAC[8 + len_a] << 8) | (MAC[9 + len_a] << 0);

            // Record provenance (use current system/site context); trust if on CC
            state->p25_iden_wacn[iden] = state->p2_wacn;
            state->p25_iden_sysid[iden] = state->p2_sysid;
            state->p25_iden_rfss[iden] = state->p2_rfssid;
            state->p25_iden_site[iden] = state->p2_siteid;
            state->p25_iden_trust[iden] = (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0) ? 2 : 1;

            fprintf(stderr, "\n Identifier Update for TDMA - Abbreviated\n");
            fprintf(stderr,
                    "  Channel Identifier [%01X] Channel Type [%01X] Transmit Offset [%04X]\n  Channel Spacing [%03X] "
                    "Base Frequency [%08lX] [%09ld]",
                    state->p25_chan_iden, state->p25_chan_type[iden], state->p25_trans_off[iden],
                    state->p25_chan_spac[iden], state->p25_base_freq[iden], state->p25_base_freq[iden] * 5);
        }

        //identifier update for TDMA, Extended
        if (MAC[1 + len_a] == 0xF3) {
            state->p25_chan_iden = MAC[3 + len_a] >> 4;
            int iden = state->p25_chan_iden;
            state->p25_chan_tdma[iden] = 1;
            state->p25_chan_tdma_explicit[iden] = 2; // explicit TDMA
            state->p25_chan_type[iden] = MAC[3 + len_a] & 0xF;
            state->p25_trans_off[iden] = (MAC[4 + len_a] << 6) | (MAC[5 + len_a] >> 2);
            state->p25_chan_spac[iden] = ((MAC[5 + len_a] & 0x3) << 8) | MAC[6 + len_a];
            state->p25_base_freq[iden] =
                (MAC[7 + len_a] << 24) | (MAC[8 + len_a] << 16) | (MAC[9 + len_a] << 8) | (MAC[10 + len_a] << 0);
            int lwacn = (MAC[11 + len_a] << 12) | (MAC[12 + len_a] << 4) | ((MAC[13 + len_a] & 0xF0) >> 4);
            int lsysid = ((MAC[13 + len_a] & 0xF) << 8) | MAC[14 + len_a];

            // Record provenance from payload + current RFSS/SITE; trust if on matching CC
            state->p25_iden_wacn[iden] = lwacn;
            state->p25_iden_sysid[iden] = lsysid;
            state->p25_iden_rfss[iden] = state->p2_rfssid;
            state->p25_iden_site[iden] = state->p2_siteid;
            state->p25_iden_trust[iden] =
                (state->p25_cc_freq != 0 && opts->p25_is_tuned == 0 && state->p2_wacn == (unsigned long long)lwacn
                 && state->p2_sysid == (unsigned long long)lsysid)
                    ? 2
                    : 1;

            fprintf(stderr, "\n Identifier Update for TDMA - Extended\n");
            fprintf(stderr,
                    "  Channel Identifier [%01X] Channel Type [%01X] Transmit Offset [%04X]\n  Channel Spacing [%03X] "
                    "Base Frequency [%08lX] [%09ld]",
                    state->p25_chan_iden, state->p25_chan_type[iden], state->p25_trans_off[iden],
                    state->p25_chan_spac[iden], state->p25_base_freq[iden], state->p25_base_freq[iden] * 5);
            fprintf(stderr, "\n  WACN [%04X] SYSID [%04X]", lwacn, lsysid);
        }

        //Secondary Control Channel Broadcast, Explicit
        if (MAC[1 + len_a] == 0xE9) {
            int rfssid = MAC[2 + len_a];
            int siteid = MAC[3 + len_a];
            int channelt = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int channelr = (MAC[6 + len_a] << 8) | MAC[7 + len_a];
            int sysclass = MAC[8 + len_a];

            // state->p2_is_lcch == 1
            fprintf(stderr, "\n Secondary Control Channel Broadcast - Explicit\n");
            fprintf(stderr, "  RFSS [%03d] SITE ID [%03d] CHAN-T [%04X] CHAN-R [%04X] SSC [%02X]", rfssid, siteid,
                    channelt, channelr, sysclass);

            process_channel_to_freq(opts, state, channelt);
            process_channel_to_freq(opts, state, channelr);

            state->p2_siteid = siteid;
            state->p2_rfssid = rfssid;
        }

        //Secondary Control Channel Broadcast, Implicit
        if (MAC[1 + len_a] == 0x79) {
            int rfssid = MAC[2 + len_a];
            int siteid = MAC[3 + len_a];
            int channel1 = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int sysclass1 = MAC[6 + len_a];
            int channel2 = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int sysclass2 = MAC[9 + len_a];
            long int freq1 = 0;
            long int freq2 = 0;
            // state->p2_is_lcch == 1
            fprintf(stderr, "\n Secondary Control Channel Broadcast - Implicit\n");
            fprintf(stderr, "  RFSS[%03d] SITE ID [%03d] CHAN1 [%04X] SSC [%02X] CHAN2 [%04X] SSC [%02X]", rfssid,
                    siteid, channel1, sysclass1, channel2, sysclass2);

            freq1 = process_channel_to_freq(opts, state, channel1);
            freq2 = process_channel_to_freq(opts, state, channel2);

            //place the cc freq into the list at index 0 if 0 is empty so we can hunt for rotating CCs without user LCN list
            if (state->trunk_lcn_freq[1] == 0) {
                state->trunk_lcn_freq[1] = freq1;
                state->trunk_lcn_freq[2] = freq2;
                state->lcn_freq_count = 3; //increment to three
            }

            state->p2_siteid = siteid;
            state->p2_rfssid = rfssid;
        }

        //MFID90 Group Regroup Voice Channel User - Abbreviated
        if (MAC[1 + len_a] == 0x80 && MAC[2 + len_a] == 0x90) {

            int gr = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int src = (MAC[6 + len_a] << 16) | (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            fprintf(stderr, "\n VCH %d - Super Group %d SRC %d ", slot + 1, gr, src);
            fprintf(stderr, "MFID90 Group Regroup Voice");
            state->gi[slot] = 0;
            // Treat observed Super Group activity as an active patch (vendor-specific signaling may differ)
            p25_patch_update(state, gr, /*is_patch*/ 1, /*active*/ 1);

            if (slot == 0) {
                state->lasttg = gr;
                if (src != 0) {
                    state->lastsrc = src;
                    // Clear alias at start/update of talker for this call (don’t reuse across calls)
                    state->generic_talker_alias[0][0] = '\0';
                    state->generic_talker_alias_src[0] = 0;
                }
            } else {
                state->lasttgR = gr;
                if (src != 0) {
                    state->lastsrcR = src;
                    // Clear alias at start/update of talker for this call (don’t reuse across calls)
                    state->generic_talker_alias[1][0] = '\0';
                    state->generic_talker_alias_src[1] = 0;
                }
            }
        }
        //MFID90 Group Regroup Voice Channel User - Extended
        if (MAC[1 + len_a] == 0xA0 && MAC[2 + len_a] == 0x90) {

            int gr = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
            int src = (MAC[7 + len_a] << 16) | (MAC[8 + len_a] << 8) | MAC[9 + len_a];
            fprintf(stderr, "\n VCH %d - Super Group %d SRC %d ", slot + 1, gr, src);
            fprintf(stderr, "MFID90 Group Regroup Voice");
            state->gi[slot] = 0;
            p25_patch_update(state, gr, /*is_patch*/ 1, /*active*/ 1);

            uint32_t mfid90_wacn = (MAC[10 + len_a] << 16) | (MAC[11 + len_a] << 8) | (MAC[12 + len_a] & 0xF0);
            mfid90_wacn >>= 4;
            uint16_t mfid90_sys = (uint16_t)(((MAC[12 + len_a] & 0x0F) << 8) | MAC[13 + len_a]);
            fprintf(stderr, " EXT - FQSUID: %05X:%03X.%d", mfid90_wacn, mfid90_sys, src);

            if (slot == 0) {
                state->lasttg = gr;
                if (src != 0) {
                    state->lastsrc = src;
                    state->generic_talker_alias[0][0] = '\0';
                    state->generic_talker_alias_src[0] = 0;
                }
            } else {
                state->lasttgR = gr;
                if (src != 0) {
                    state->lastsrcR = src;
                    state->generic_talker_alias[1][0] = '\0';
                    state->generic_talker_alias_src[1] = 0;
                }
            }
            if (src != 0 && gr != 0) {
                p25_ga_add(state, (uint32_t)src, (uint16_t)gr);
            }
        }

        //MFIDA4 Group Regroup Explicit Encryption Command
        if (MAC[1 + len_a] == 0xB0 && MAC[2 + len_a] == 0xA4) //&& MAC[2+len_a] == 0xA4
        {
            int len_grg = MAC[3 + len_a] & 0x3F; //MFID Len in Octets
            int tga = MAC[4 + len_a] >> 5;       //3 bit TGA values from GRG_Options
            int ssn = MAC[4 + len_a] & 0x1F;     //5 bit SSN from from GRG_Options

            fprintf(stderr, "\n MFID A4 (Harris) Group Regroup Explicit Encryption Command\n");
            // if (len_grg) fprintf (stderr, " Len: %02d", len_grg); //debug
            if ((tga & 4) == 4) {
                fprintf(stderr, " Simulselect"); //one-way regroup
            } else {
                fprintf(stderr, " Patch"); //two-way regroup
            }
            if (tga & 1) {
                fprintf(stderr, " Active;"); //activated
            } else {
                fprintf(stderr, " Inactive;"); //deactivated
            }

            //debug
            // fprintf (stderr, " T:%d G:%d A:%d;", (tga >> 2) & 1, (tga >> 1) & 1, tga & 1);

            fprintf(stderr, " SSN: %02d;", ssn);

            if ((tga & 0x2) == 2) //group WGID to supergroup
            {
                int sg = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
                int key = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
                int alg = MAC[9 + len_a];
                int t1 = (MAC[10 + len_a] << 8) | MAC[11 + len_a];
                int t2 = (MAC[12 + len_a] << 8) | MAC[13 + len_a];
                int t3 = (MAC[14 + len_a] << 8) | MAC[15 + len_a];
                int t4 = (MAC[16 + len_a] << 8) | MAC[17 + len_a];
                UNUSED4(t1, t2, t3, t4);
                fprintf(stderr, " SG: %d; KEY: %04X; ALG: %02X;\n  ", sg, key, alg);
                int a = 0;
                int wgid = 0;

                //sanity check
                // if ((len_grg + len_a > 20)) len_grg = 20 - len_a;

                for (int i = 10; i <= len_grg;) {
                    //failsafe to prevent oob array
                    if ((i + len_a) > 20) {
                        goto END_PDU;
                    }
                    wgid = (MAC[10 + len_a + a] << 8) | MAC[11 + len_a + a];
                    fprintf(stderr, "WGID: %d; ", wgid);
                    p25_patch_add_wgid(state, sg, wgid);
                    a = a + 2;
                    i = i + 2;
                }

                // Update patch tracker for this SG (two-way patch if bit4 of TGA is 0)
                int is_patch = ((tga & 0x4) == 0) ? 1 : 0;
                int active = (tga & 0x1) ? 1 : 0;
                p25_patch_update(state, sg, is_patch, active);
                p25_patch_set_kas(state, sg, key, alg, ssn);

            }

            else if ((tga & 0x2) == 0) //individual WUID to supergroup
            {
                int sg = (MAC[5 + len_a] << 8) | MAC[6 + len_a];
                int key = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
                int t1 = (MAC[9 + len_a] << 16) | (MAC[10 + len_a] << 8) | MAC[11 + len_a];
                int t2 = (MAC[12 + len_a] << 16) | (MAC[13 + len_a] << 8) | MAC[14 + len_a];
                int t3 = (MAC[15 + len_a] << 16) | (MAC[16 + len_a] << 8) | MAC[17 + len_a];
                fprintf(stderr, "  SG: %d KEY: %04X", sg, key);
                fprintf(stderr, " WUID: %d; WUID: %d; WUID: %d; ", t1, t2, t3);
                p25_patch_add_wuid(state, sg, (uint32_t)t1);
                p25_patch_add_wuid(state, sg, (uint32_t)t2);
                p25_patch_add_wuid(state, sg, (uint32_t)t3);

                // Update patch tracker
                int is_patch = ((tga & 0x4) == 0) ? 1 : 0;
                int active = (tga & 0x1) ? 1 : 0;
                p25_patch_update(state, sg, is_patch, active);
                p25_patch_set_kas(state, sg, key, -1, ssn);
            }
        }

        //Unit Registration Response -- Extended vPDU (MBT will not make it here)
        if (MAC[1 + len_a] == 0xEC && MAC[0] != 0x07) {
            int res = (MAC[3 + len_a] >> 2) & 0x3F;
            int RV = (MAC[2 + len_a] >> 0) & 0x3;
            int src = (MAC[8 + len_a] << 16) | (MAC[9 + len_a] << 8) | MAC[10 + len_a];
            int uwacn = (MAC[4 + len_a] << 12) | (MAC[5 + len_a] << 4) | ((MAC[6 + len_a] & 0xF0) >> 4);
            int usys = ((MAC[6 + len_a] & 0xF) << 8) | MAC[7 + len_a];
            fprintf(stderr, "\n Unit Registration Response - WACN: %05X; SYS: %03X; SRC: %d", uwacn, usys, src);
            if (res) {
                fprintf(stderr, " RES: %d;", res);
            }
            if (RV == 0) {
                fprintf(stderr, " REG_ACCEPT;");
                // Track affiliated RID
                p25_aff_register(state, (uint32_t)src);
            }
            if (RV == 1) {
                fprintf(stderr, " REG_FAIL;"); //RFSS was unable to verify
            }
            if (RV == 2) {
                fprintf(stderr, " REG_DENY;"); //Not allowed at this location
            }
            if (RV == 3) {
                fprintf(stderr, " REG_REFUSED;"); //WUID invalid but re-register after a user stimulus
            }
            fprintf(stderr, " - Extended;");
        }

        //Unit Registration Response -- Abbreviated TSBK and vPDU
        if (MAC[1 + len_a] == 0x6C) {
            /*
			Unit Registration Response TSBK
			P25 PDU Payload
				[07][6C][01][EC][72][70][EC][72][70][EC][00][00]
				[00][00][00][00][00][00][00][00][00][00][00][00]
			*/
            int k = 1; //vPDU is one octet higher than TSBK
            if (MAC[len_a] == 0x07) {
                k = 0;
            }
            int res = (MAC[2 + len_a + k] >> 6) & 0x3;
            int RV = (MAC[2 + len_a + k] >> 4) & 0x3;
            int usite = ((MAC[2 + len_a + k] & 0xF) << 8) | MAC[3 + len_a + k];
            int sid = (MAC[4 + len_a + k] << 16) | (MAC[5 + len_a + k] << 8) | MAC[6 + len_a + k];
            int src = (MAC[7 + len_a + k] << 16) | (MAC[8 + len_a + k] << 8) | MAC[9 + len_a + k];
            fprintf(stderr, "\n Unit Registration Response - SITE: %03X SRC_ID: %d SRC: %d", usite, sid, src);
            if (res) {
                fprintf(stderr, " RES: %d;", res);
            }
            if (RV == 0) {
                fprintf(stderr, " REG_ACCEPT;");
                // Track affiliated RID
                p25_aff_register(state, (uint32_t)src);
            }
            if (RV == 1) {
                fprintf(stderr, " REG_FAIL;"); //RFSS was unable to verify
            }
            if (RV == 2) {
                fprintf(stderr, " REG_DENY;"); //Not allowed at this location
            }
            if (RV == 3) {
                fprintf(stderr, " REG_REFUSED;"); //WUID invalid but re-register after a user stimulus
            }
        }

        //Unit Registration Command -- TSBK and vPDU
        if (MAC[1 + len_a] == 0x6D && MAC[0 + len_a] != 0x07) {
            int k = 2; //vPDU is two octets higher than TSBK
            if (MAC[len_a] == 0x07) {
                k = 0;
            }
            int src = (MAC[2 + len_a + k] << 16) | (MAC[3 + len_a + k] << 8) | MAC[4 + len_a + k];
            int tgt = (MAC[5 + len_a + k] << 16) | (MAC[6 + len_a + k] << 8) | MAC[7 + len_a + k];
            fprintf(stderr, "\n Unit Registration - SRC: %d; TGT: %d;", src, tgt);
        }

        //Unit Deregistration Acknowlegement (TSBK and vPDU are the same)
        if (MAC[1 + len_a] == 0x6F || MAC[1 + len_a] == 0xEF) {
            int src = (MAC[7 + len_a] << 16) | (MAC[8 + len_a] << 8) | MAC[9 + len_a];
            int uwacn = (MAC[3 + len_a] << 12) | (MAC[4 + len_a] << 4) | ((MAC[5 + len_a] & 0xF0) >> 4);
            int usys = ((MAC[5 + len_a] & 0xF) << 8) | MAC[6 + len_a];
            fprintf(stderr, "\n Unit Deregistration Acknowlegement - WACN: %05X; SYS: %03X; SRC: %d", uwacn, usys, src);
            if (MAC[1 + len_a] == 0xEF) {
                fprintf(stderr, " - Extended;");
            }
            // Remove RID from affiliation table
            p25_aff_deregister(state, (uint32_t)src);
        }

        //Authentication Demand
        if (MAC[1 + len_a] == 0x71 || MAC[1 + len_a] == 0xF1) {
            fprintf(stderr, "\n Authentication Demand;");
            if (MAC[1 + len_a] == 0xF1) {
                fprintf(stderr, " - Extended;");
            }
        }

        //Authentication FNE Response
        if (MAC[1 + len_a] == 0x72 || MAC[1 + len_a] == 0xF2) {
            fprintf(stderr, "\n Authentication FNE Response;");
            if (MAC[1 + len_a] == 0xF2) {
                fprintf(stderr, " - Extended;");
            }
        }

        //MAC_Release for Forced/Unforced Audio or Call Preemption vPDU
        if (MAC[1 + len_a] == 0x31) {
            int uf = (MAC[2 + len_a] >> 7) & 1;
            int ca = (MAC[2 + len_a] >> 6) & 1;
            int resr1 = MAC[2 + len_a] & 0x1F;
            int add = (MAC[3 + len_a] << 16) | (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int resr2 = MAC[6 + len_a] >> 4;
            int cc = ((MAC[6 + len_a] & 0xF) << 8) | MAC[7 + len_a];

            fprintf(stderr, "\n MAC Release:  ");
            if (uf) {
                fprintf(stderr, "Forced; ");
            } else {
                fprintf(stderr, "Unforced; ");
            }
            if (ca) {
                fprintf(stderr, "Audio Preemption; ");
            } else {
                fprintf(stderr, "Call Preemption; ");
            }
            fprintf(stderr, "RES1: %d; ", resr1);
            fprintf(stderr, "RES2: %d; ", resr2);
            fprintf(stderr, "TGT: %d; ", add);
            fprintf(stderr, "CC: %03X; ", cc);

            // Gate only the logical slot indicated by this vPDU and let the
            // state machine decide whether to return to CC based on the
            // opposite slot's activity. This prevents dropping an active clear
            // call on the other timeslot when a release applies to a single
            // stream.
            int eslot = slot; // vPDU uses current logical slot mapping
            state->p25_p2_audio_allowed[eslot] = 0;
            // Flush any residual audio queued for this slot to avoid artifact bleed
            p25_p2_audio_ring_reset(state, eslot);

            // Determine if opposite slot is active using P25 gates/jitter and recent MAC_ACTIVE
            double mac_hold = 0.75; // seconds; override via DSD_NEO_P25_MAC_HOLD
            {
                const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                if (state && state->p25_cfg_mac_hold_s > 0.0) {
                    mac_hold = state->p25_cfg_mac_hold_s;
                } else if (cfg && cfg->p25_mac_hold_is_set) {
                    mac_hold = cfg->p25_mac_hold_s;
                }
            }
            double nowm2 = dsd_time_now_monotonic_s();
            double noww2 = (double)time(NULL);
            int os = eslot ^ 1;
            double voice_hold = 0.75; // seconds; override via DSD_NEO_P25_VOICE_HOLD
            {
                const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                if (cfg && cfg->p25_voice_hold_is_set) {
                    voice_hold = cfg->p25_voice_hold_s;
                }
            }
            double dt_voice =
                (state->last_vc_sync_time_m > 0.0)
                    ? (nowm2 - state->last_vc_sync_time_m)
                    : ((state->last_vc_sync_time != 0) ? (noww2 - (double)state->last_vc_sync_time) : 1e9);
            int recent_voice = (dt_voice <= voice_hold);
            double dt_mac =
                (state->p25_p2_last_mac_active_m[os] > 0.0)
                    ? (nowm2 - state->p25_p2_last_mac_active_m[os])
                    : ((state->p25_p2_last_mac_active[os] != 0) ? (noww2 - (double)state->p25_p2_last_mac_active[os])
                                                                : 1e9);
            int other_audio = state->p25_p2_audio_allowed[os] || (state->p25_p2_audio_ring_count[os] > 0)
                              || (state->p25_p2_last_mac_active[os] != 0 && dt_mac <= mac_hold) || recent_voice;
            if (!other_audio) {
                // Only force release outside the VC grace window to avoid dropping
                // an opposite-slot clear call that hasn't opened its gates yet.
                double vc_grace = (state->p25_cfg_vc_grace_s > 0.0) ? state->p25_cfg_vc_grace_s : 0.75;
                if (!(state->p25_cfg_vc_grace_s > 0.0)) {
                    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                    if (cfg && cfg->p25_vc_grace_is_set) {
                        vc_grace = cfg->p25_vc_grace_s;
                    }
                }
                double nowm3 = dsd_time_now_monotonic_s();
                double dt_since_tune3 =
                    (state->p25_last_vc_tune_time_m > 0.0) ? (nowm3 - state->p25_last_vc_tune_time_m) : 1e9;
                if (dt_since_tune3 >= vc_grace) {
                    state->p25_sm_force_release = 1;
                    p25_sm_on_release(opts, state);
                } else {
                    // Keep VC during grace; banner for this slot will be cleared below on END/IDLE
                }
            } else {
                // Keep VC; other slot still has audio. Clear banner for this slot.
                if (eslot == 0) {
                    snprintf(state->call_string[0], sizeof state->call_string[0], "%s", "                     ");
                } else {
                    snprintf(state->call_string[1], sizeof state->call_string[1], "%s", "                     ");
                }
            }
        }

        //1 or 21, group voice channel message, abb and ext
        if (MAC[1 + len_a] == 0x1 || MAC[1 + len_a] == 0x21) {
            int svc = MAC[2 + len_a];
            int gr = (MAC[3 + len_a] << 8) | MAC[4 + len_a];
            int src = (MAC[5 + len_a] << 16) | (MAC[6 + len_a] << 8) | MAC[7 + len_a];
            unsigned long long int src_suid = 0;

            if (MAC[1 + len_a] == 0x21) {
                src_suid = (MAC[8 + len_a] << 48ULL) | (MAC[9 + len_a] << 40ULL) | (MAC[10 + len_a] << 32ULL)
                           | (MAC[11 + len_a] << 24ULL) | (MAC[12 + len_a] << 16ULL) | (MAC[13 + len_a] << 8ULL)
                           | (MAC[14 + len_a] << 0ULL);

                src = src_suid & 0xFFFFFF; //last 24 of completed SUID?
            }

            fprintf(stderr, "\n VCH %d - TG: %d; SRC: %d; ", slot + 1, gr, src);
            // Mark recent slot activity on group voice MAC (Extended/Abbrev)
            state->p25_p2_last_mac_active[slot] = time(NULL);

            if (MAC[1 + len_a] == 0x21) {
                fprintf(stderr, "SUID: %08llX-%08d; ", src_suid >> 24, src);
            }

            if (svc & 0x80) {
                fprintf(stderr, " Emergency");
                state->p25_call_emergency[state->currentslot & 1] = 1;
            } else {
                state->p25_call_emergency[state->currentslot & 1] = 0;
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
                state->p25_call_priority[state->currentslot & 1] = (uint8_t)(svc & 0x7);
            } else {
                state->p25_call_priority[state->currentslot & 1] = 0;
            }

            fprintf(stderr, " Group Voice");
            state->gi[slot] = 0;

            // Persist SVC bits for event history (per-slot)
            if ((slot & 1) == 0) {
                state->dmr_so = (uint16_t)svc;
            } else {
                state->dmr_soR = (uint16_t)svc;
            }

            sprintf(state->call_string[slot], "   Group ");
            if (svc & 0x80) {
                dsd_append(state->call_string[slot], sizeof state->call_string[slot], " Emergency  ");
            } else if (svc & 0x40) {
                dsd_append(state->call_string[slot], sizeof state->call_string[slot], " Encrypted  ");
            } else {
                dsd_append(state->call_string[slot], sizeof state->call_string[slot], "            ");
            }

            if (MAC[1 + len_a] == 0x21) {
                fprintf(stderr, " - Extended ");
            } else {
                fprintf(stderr, " - Abbreviated ");
            }

            if (slot == 0) {
                state->lasttg = gr;
                if (src != 0) {
                    state->lastsrc = src;
                    state->generic_talker_alias[0][0] = '\0';
                    state->generic_talker_alias_src[0] = 0;
                }
            } else {
                state->lasttgR = gr;
                if (src != 0) {
                    state->lastsrcR = src;
                    state->generic_talker_alias[1][0] = '\0';
                    state->generic_talker_alias_src[1] = 0;
                }
            }

            // VPDU fallback: if SVC indicates encryption and ENC lockout is enabled,
            // terminate the encrypted slot (mute only this slot) and return to CC
            // if the opposite slot is not active.
            if ((svc & 0x40) && opts->p25_trunk == 1 && opts->p25_is_tuned == 1 && opts->trunk_tune_enc_calls == 0) {
                // Mark TG as ENC LO for visibility in UI/event log
                int ttg = gr;
                if (ttg != 0 && !p25_patch_tg_key_is_clear(state, ttg) && !p25_patch_sg_key_is_clear(state, ttg)) {
                    // Mark and emit once via centralized helper
                    p25_emit_enc_lockout_once(opts, state, (uint8_t)slot, ttg, /*svc_bits*/ 0);
                }
                // Gate this slot only and flush any queued audio to avoid residue
                state->p25_p2_audio_allowed[slot] = 0;
                p25_p2_audio_ring_reset(state, slot);
                int other = slot ^ 1;
                double mac_hold = 0.75; // seconds; override via DSD_NEO_P25_MAC_HOLD
                {
                    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                    if (state && state->p25_cfg_mac_hold_s > 0.0) {
                        mac_hold = state->p25_cfg_mac_hold_s;
                    } else if (cfg && cfg->p25_mac_hold_is_set) {
                        mac_hold = cfg->p25_mac_hold_s;
                    }
                }
                double nowm2 = dsd_time_now_monotonic_s();
                double noww2 = (double)time(NULL);
                double voice_hold = 0.6; // seconds; override via DSD_NEO_P25_VOICE_HOLD
                {
                    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                    if (cfg && cfg->p25_voice_hold_is_set) {
                        voice_hold = cfg->p25_voice_hold_s;
                    }
                }
                double dt_voice =
                    (state->last_vc_sync_time_m > 0.0)
                        ? (nowm2 - state->last_vc_sync_time_m)
                        : ((state->last_vc_sync_time != 0) ? (noww2 - (double)state->last_vc_sync_time) : 1e9);
                int recent_voice = (dt_voice <= voice_hold);
                double dt_mac = (state->p25_p2_last_mac_active_m[other] > 0.0)
                                    ? (nowm2 - state->p25_p2_last_mac_active_m[other])
                                    : ((state->p25_p2_last_mac_active[other] != 0)
                                           ? (noww2 - (double)state->p25_p2_last_mac_active[other])
                                           : 1e9);
                int other_audio = state->p25_p2_audio_allowed[other] || (state->p25_p2_audio_ring_count[other] > 0)
                                  || (state->p25_p2_last_mac_active[other] != 0 && dt_mac <= mac_hold) || recent_voice;
                if (!other_audio) {
                    fprintf(stderr, " No Enc Following on P25p2 Trunking (VCH SVC ENC); ");
                    // Defer immediate return to CC during a short post‑tune grace to
                    // avoid dropping a clear opposite-slot call that has not yet opened
                    // its audio gates.
                    double vc_grace = (state->p25_cfg_vc_grace_s > 0.0) ? state->p25_cfg_vc_grace_s : 0.75;
                    if (!(state->p25_cfg_vc_grace_s > 0.0)) {
                        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                        if (cfg && cfg->p25_vc_grace_is_set) {
                            vc_grace = cfg->p25_vc_grace_s;
                        }
                    }
                    double nowm = dsd_time_now_monotonic_s();
                    double dt_since_tune =
                        (state->p25_last_vc_tune_time_m > 0.0) ? (nowm - state->p25_last_vc_tune_time_m) : 1e9;
                    if (dt_since_tune >= vc_grace) {
                        fprintf(stderr, "Return to CC; \n");
                        state->p25_sm_force_release = 1;
                        p25_sm_on_release(opts, state);
                    } else {
                        fprintf(stderr, "Defer (VC grace); stay on VC. \n");
                    }
                } else {
                    fprintf(stderr,
                            " No Enc Following on P25p2 Trunking (VCH SVC ENC); Other slot active; stay on VC. \n");
                    // Keep encryption fields intact so gating remains correct; clear banner only.
                    if (slot == 0) {
                        snprintf(state->call_string[0], sizeof state->call_string[0], "%s", "                     ");
                    } else {
                        snprintf(state->call_string[1], sizeof state->call_string[1], "%s", "                     ");
                    }
                }
            }
        }

        //1 or 21, unit to unit voice channel message, abb and ext
        if (MAC[1 + len_a] == 0x2 || MAC[1 + len_a] == 0x22) {
            int svc = MAC[2 + len_a];
            int gr = (MAC[3 + len_a] << 16) | (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int src = (MAC[6 + len_a] << 16) | (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            unsigned long long int src_suid = 0;

            if (MAC[1 + len_a] == 0x21) {
                src_suid = (MAC[9 + len_a] << 48ULL) | (MAC[10 + len_a] << 40ULL) | (MAC[11 + len_a] << 32ULL)
                           | (MAC[12 + len_a] << 24ULL) | (MAC[13 + len_a] << 16ULL) | (MAC[14 + len_a] << 8ULL)
                           | (MAC[15 + len_a] << 0ULL);

                src = src_suid & 0xFFFFFF;
            }

            fprintf(stderr, "\n VCH %d - TGT: %d; SRC %d; ", slot + 1, gr, src);
            // Mark recent slot activity on unit-to-unit voice MAC (Extended/Abbrev)
            state->p25_p2_last_mac_active[slot] = time(NULL);

            if (MAC[1 + len_a] == 0x22) {
                fprintf(stderr, "SUID: %08llX-%08d; ", src_suid >> 24, src);
            }

            if (svc & 0x80) {
                fprintf(stderr, " Emergency");
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
            }

            fprintf(stderr, " Unit to Unit Voice");
            state->gi[slot] = 1;

            // Persist SVC bits for event history (per-slot)
            if ((slot & 1) == 0) {
                state->dmr_so = (uint16_t)svc;
            } else {
                state->dmr_soR = (uint16_t)svc;
            }

            sprintf(state->call_string[slot], " Private ");
            if (svc & 0x80) {
                dsd_append(state->call_string[slot], sizeof state->call_string[slot], " Emergency  ");
            } else if (svc & 0x40) {
                dsd_append(state->call_string[slot], sizeof state->call_string[slot], " Encrypted  ");
            } else {
                dsd_append(state->call_string[slot], sizeof state->call_string[slot], "            ");
            }

            if (slot == 0) {
                state->lasttg = gr;
                if (src != 0) {
                    state->lastsrc = src;
                    if (state->generic_talker_alias_src[0] != (uint32_t)src) {
                        state->generic_talker_alias[0][0] = '\0';
                        state->generic_talker_alias_src[0] = 0;
                    }
                }
            } else {
                state->lasttgR = gr;
                if (src != 0) {
                    state->lastsrcR = src;
                    if (state->generic_talker_alias_src[1] != (uint32_t)src) {
                        state->generic_talker_alias[1][0] = '\0';
                        state->generic_talker_alias_src[1] = 0;
                    }
                }
            }

            // VPDU fallback for UU_V: encrypted per SVC and ENC lockout enabled
            if ((svc & 0x40) && opts->p25_trunk == 1 && opts->p25_is_tuned == 1 && opts->trunk_tune_enc_calls == 0) {
                // Mark target as ENC LO (use same table for display), unless GRG policy is KEY=0000
                int ttg = gr;
                if (ttg != 0 && !p25_patch_tg_key_is_clear(state, ttg) && !p25_patch_sg_key_is_clear(state, ttg)) {
                    // Mark and emit once via centralized helper
                    p25_emit_enc_lockout_once(opts, state, (uint8_t)slot, ttg, /*svc_bits*/ 0);
                }
                // Gate this slot only
                state->p25_p2_audio_allowed[slot] = 0;
                int other = slot ^ 1;
                int other_audio = state->p25_p2_audio_allowed[other] || state->p25_p2_audio_ring_count[other] > 0;
                if (!other_audio) {
                    fprintf(stderr, " No Enc Following on P25p2 Trunking (VCH SVC ENC); ");
                    double vc_grace = (state->p25_cfg_vc_grace_s > 0.0) ? state->p25_cfg_vc_grace_s : 0.75;
                    if (!(state->p25_cfg_vc_grace_s > 0.0)) {
                        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                        if (cfg && cfg->p25_vc_grace_is_set) {
                            vc_grace = cfg->p25_vc_grace_s;
                        }
                    }
                    double nowm = dsd_time_now_monotonic_s();
                    double dt_since_tune =
                        (state->p25_last_vc_tune_time_m > 0.0) ? (nowm - state->p25_last_vc_tune_time_m) : 1e9;
                    if (dt_since_tune >= vc_grace) {
                        fprintf(stderr, "Return to CC; \n");
                        state->p25_sm_force_release = 1;
                        p25_sm_on_release(opts, state);
                    } else {
                        fprintf(stderr, "Defer (VC grace); stay on VC. \n");
                    }
                } else {
                    fprintf(stderr,
                            " No Enc Following on P25p2 Trunking (VCH SVC ENC); Other slot active; stay on VC. \n");
                    // Keep encryption fields intact so gating remains correct; clear banner only.
                    if (slot == 0) {
                        snprintf(state->call_string[0], sizeof state->call_string[0], "%s", "                     ");
                    } else {
                        snprintf(state->call_string[1], sizeof state->call_string[1], "%s", "                     ");
                    }
                }
            }
        }

        //network status broadcast, abbreviated (TDMA NET_STS)
        if (MAC[1 + len_a] == 0x7B) {
            int lra = MAC[2 + len_a];
            int lwacn = (MAC[3 + len_a] << 12) | (MAC[4 + len_a] << 4) | ((MAC[5 + len_a] & 0xF0) >> 4);
            int lsysid = ((MAC[5 + len_a] & 0xF) << 8) | MAC[6 + len_a];
            int channel = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int sysclass = MAC[9 + len_a];
            int lcolorcode = ((MAC[10 + len_a] & 0xF) << 8) | MAC[11 + len_a];
            UNUSED(sysclass);
            fprintf(stderr, "\n Network Status Broadcast - Abbreviated \n");
            fprintf(stderr, "  LRA [%02X] WACN [%05X] SYSID [%03X] NAC [%03X] CHAN-T [%04X]", lra, lwacn, lsysid,
                    lcolorcode, channel);
            long int cc_freq = process_channel_to_freq(opts, state, channel);
            if (cc_freq > 0) {
                state->p25_cc_freq = cc_freq;
                long neigh_a[1] = {state->p25_cc_freq};
                p25_sm_on_neighbor_update(opts, state, neigh_a, 1);
                state->p25_sys_is_tdma = 1; // system carries Phase 2 voice (TDMA present)
                state->p25_cc_is_tdma = 1;  // TDMA control channel (QPSK, 6000 sym/s)

                // Only update system identity and potentially reset IDEN tables when
                // values are sane (non-zero) and we have a valid frequency mapping.
                if (state->p2_hardset == 0) { // prevent bogus data from wiping tables
                    if ((lwacn != 0 || lsysid != 0)
                        && ((state->p2_wacn != 0 || state->p2_sysid != 0)
                            && (state->p2_wacn != (unsigned long long)lwacn
                                || state->p2_sysid != (unsigned long long)lsysid))) {
                        p25_reset_iden_tables(state);
                    }
                    if (lwacn != 0 || lsysid != 0) {
                        state->p2_wacn = lwacn;
                        state->p2_sysid = lsysid;
                        state->p2_cc = lcolorcode;
                    }
                }

                //place the cc freq into the list at index 0 if 0 is empty, or not the same,
                //so we can hunt for rotating CCs without user LCN list
                if (state->trunk_lcn_freq[0] == 0 || state->trunk_lcn_freq[0] != state->p25_cc_freq) {
                    state->trunk_lcn_freq[0] = state->p25_cc_freq;
                }
                // Confirm any IDENs now that CC identity is known
                p25_confirm_idens_for_current_site(state);
            } else {
                // Invalid/unknown channel mapping: ignore to avoid wiping IDEN tables from bogus frames
                fprintf(stderr, "\n  P25 NSB: ignoring invalid channel->freq (CHAN-T=%04X)", channel);
            }
        }
        //network status broadcast, extended (TDMA NET_STS-EXT)
        if (MAC[1 + len_a] == 0xFB) {
            int lra = MAC[2 + len_a];
            int lwacn = (MAC[3 + len_a] << 12) | (MAC[4 + len_a] << 4) | ((MAC[5 + len_a] & 0xF0) >> 4);
            int lsysid = ((MAC[5 + len_a] & 0xF) << 8) | MAC[6 + len_a];
            int channelt = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int channelr = (MAC[9 + len_a] << 8) | MAC[10 + len_a];
            int sysclass = MAC[9 + len_a];
            int lcolorcode = ((MAC[12 + len_a] & 0xF) << 8) | MAC[13 + len_a];
            UNUSED(sysclass);
            fprintf(stderr, "\n Network Status Broadcast - Extended \n");
            fprintf(stderr, "  LRA [%02X] WACN [%05X] SYSID [%03X] NAC [%03X] CHAN-T [%04X] CHAN-R [%04X]", lra, lwacn,
                    lsysid, lcolorcode, channelt, channelr);
            long int nf1 = process_channel_to_freq(opts, state, channelt);
            long int nf2 = process_channel_to_freq(opts, state, channelr);
            if (nf1 > 0) {
                state->p25_cc_freq = nf1;
                long neigh_b[2] = {nf1, nf2};
                p25_sm_on_neighbor_update(opts, state, neigh_b, 2);
                state->p25_sys_is_tdma = 1;   // system carries Phase 2 voice (TDMA present)
                state->p25_cc_is_tdma = 1;    // TDMA control channel (QPSK, 6000 sym/s)
                if (state->p2_hardset == 0) { // prevent bogus data from wiping tables
                    if ((lwacn != 0 || lsysid != 0)
                        && ((state->p2_wacn != 0 || state->p2_sysid != 0)
                            && (state->p2_wacn != (unsigned long long)lwacn
                                || state->p2_sysid != (unsigned long long)lsysid))) {
                        p25_reset_iden_tables(state);
                    }
                    if (lwacn != 0 || lsysid != 0) {
                        state->p2_wacn = lwacn;
                        state->p2_sysid = lsysid;
                        state->p2_cc = lcolorcode;
                    }
                }
                p25_confirm_idens_for_current_site(state);
            } else {
                fprintf(stderr, "\n  P25 NSB-EXT: ignoring invalid channel->freq (CHAN-T=%04X)", channelt);
            }
        }

        //Adjacent Status Broadcast, abbreviated
        if (MAC[1 + len_a] == 0x7C) {
            int lra = MAC[2 + len_a];
            int cfva = MAC[3 + len_a] >> 4;
            int lsysid = ((MAC[3 + len_a] & 0xF) << 8) | MAC[4 + len_a];
            int rfssid = MAC[5 + len_a];
            int siteid = MAC[6 + len_a];
            int channelt = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int sysclass = MAC[9 + len_a];
            fprintf(stderr, "\n Adjacent Status Broadcast - Abbreviated\n");
            fprintf(stderr, "  LRA [%02X] RFSS[%03d] SITE [%03d] SYSID [%03X] CHAN-T [%04X] SSC [%02X]\n  ", lra,
                    rfssid, siteid, lsysid, channelt, sysclass);
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
            long int af1 = process_channel_to_freq(opts, state, channelt);
            long neigh_c[1] = {af1};
            p25_sm_on_neighbor_update(opts, state, neigh_c, 1);
        }

        //Adjacent Status Broadcast, extended
        if (MAC[1 + len_a] == 0xFC) {
            int lra = MAC[2 + len_a];
            int cfva = MAC[3 + len_a] >> 4;
            int lsysid = ((MAC[3 + len_a] & 0xF) << 8) | MAC[4 + len_a];
            int rfssid = MAC[5 + len_a];
            int siteid = MAC[6 + len_a];
            int channelt = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int channelr = (MAC[9 + len_a] << 8) | MAC[10 + len_a];
            int sysclass = MAC[11 + len_a]; //need to re-check this
            fprintf(stderr, "\n Adjacent Status Broadcast - Extended\n");
            fprintf(stderr,
                    "  LRA [%02X] RFSS[%03d] SITE [%03d] SYSID [%03X] CHAN-T [%04X] CHAN-R [%04X] SSC [%02X]\n  ", lra,
                    rfssid, siteid, lsysid, channelt, channelr, sysclass);
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
            long int af2 = process_channel_to_freq(opts, state, channelt);
            long int af3 = process_channel_to_freq(opts, state, channelr);
            long neigh_d[2] = {af2, af3};
            p25_sm_on_neighbor_update(opts, state, neigh_d, 2);
        }

        // Adjacent Status Broadcast - Extended Explicit (op25 parity: 0xFE)
        // Layout: LRA(8), CFVA(4), SYSID(12), RFSSID(8), SITEID(8), CHAN-T(16), CHAN-R(16), SSC(8)
        if (MAC[1 + len_a] == 0xFE) {
            int lra = MAC[2 + len_a];
            int cfva = MAC[3 + len_a] >> 4;
            int lsysid = ((MAC[3 + len_a] & 0xF) << 8) | MAC[4 + len_a];
            int rfssid = MAC[5 + len_a];
            int siteid = MAC[6 + len_a];
            int channelt = (MAC[7 + len_a] << 8) | MAC[8 + len_a];
            int channelr = (MAC[9 + len_a] << 8) | MAC[10 + len_a];
            int sysclass = MAC[11 + len_a];
            fprintf(stderr, "\n Adjacent Status Broadcast - Extended Explicit\n");
            fprintf(stderr,
                    "  LRA [%02X] RFSS[%03d] SITE [%03d] SYSID [%03X] CHAN-T [%04X] CHAN-R [%04X] SSC [%02X]\n  ", lra,
                    rfssid, siteid, lsysid, channelt, channelr, sysclass);
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
            long int af4 = process_channel_to_freq(opts, state, channelt);
            long int af5 = process_channel_to_freq(opts, state, channelr);
            long neigh_e[2] = {af4, af5};
            p25_sm_on_neighbor_update(opts, state, neigh_e, 2);
        }

        // Group Affiliation Response (op25 parity: TSBK 0x28 -> MAC 0x68)
        // Layout: LG(1), GAV(2), AGA(16), GA(16), TA(24)
        if (MAC[1 + len_a] == 0x68) {
            int k = 1; // vPDU offset
            if (MAC[len_a] == 0x07) {
                k = 0; // TSBK offset
            }
            int lg = (MAC[2 + len_a + k] >> 7) & 0x1;
            int gav = (MAC[2 + len_a + k] >> 5) & 0x3;
            int aga = (MAC[3 + len_a + k] << 8) | MAC[4 + len_a + k];
            int ga = (MAC[5 + len_a + k] << 8) | MAC[6 + len_a + k];
            int ta = (MAC[7 + len_a + k] << 16) | (MAC[8 + len_a + k] << 8) | MAC[9 + len_a + k];
            fprintf(stderr, "\n Group Affiliation Response");
            fprintf(stderr, "\n  LG: %d GAV: %d AGA: %d GA: %d TA: %d", lg, gav, aga, ga, ta);
            // Track RID affiliation (TA is the target address / unit ID)
            if (gav == 0) { // Accepted
                p25_aff_register(state, (uint32_t)ta);
                // Also track group affiliation (RID -> TG mapping)
                p25_ga_add(state, (uint32_t)ta, (uint32_t)ga);
            }
        }

        // Secondary Control Channel Broadcast - Explicit (from P1 TSBK 0x29 -> MAC 0x69)
        // Layout: RFSSID(8), SITEID(8), CHAN-T1(16), CHAN-R1(16), SSC(8)
        if (MAC[1 + len_a] == 0x69) {
            int rfssid = MAC[2 + len_a];
            int siteid = MAC[3 + len_a];
            int channelt = (MAC[4 + len_a] << 8) | MAC[5 + len_a];
            int channelr = (MAC[6 + len_a] << 8) | MAC[7 + len_a];
            int sysclass = MAC[8 + len_a];
            fprintf(stderr, "\n Secondary Control Channel Broadcast - Explicit (from P1 TSBK)\n");
            fprintf(stderr, "  RFSS [%03d] SITE ID [%03d] CHAN-T [%04X] CHAN-R [%04X] SSC [%02X]", rfssid, siteid,
                    channelt, channelr, sysclass);
            long int sccf = process_channel_to_freq(opts, state, channelt);
            long int sccfr = process_channel_to_freq(opts, state, channelr);
            // Add to CC candidate list for hunting
            if (sccf > 0 && state->trunk_lcn_freq[1] == 0) {
                state->trunk_lcn_freq[1] = sccf;
                state->lcn_freq_count = 2;
            } else if (sccf > 0 && state->trunk_lcn_freq[2] == 0 && sccf != state->trunk_lcn_freq[1]) {
                state->trunk_lcn_freq[2] = sccf;
                state->lcn_freq_count = 3;
            }
            // Notify state machine of neighbor CC
            if (sccf > 0) {
                long neigh_scc[2] = {sccf, sccfr};
                p25_sm_on_neighbor_update(opts, state, neigh_scc, (sccfr > 0) ? 2 : 1);
            }
            state->p2_siteid = siteid;
            state->p2_rfssid = rfssid;
        }

        // Power Control Signal Quality (op25 parity: MAC 0x30)
        // Layout: TA(24), RF(4), BER(4)
        if (MAC[1 + len_a] == 0x30 && MAC[2 + len_a] != 0xA4 && MAC[2 + len_a] != 0x90) {
            // Exclude MFID messages (0xA4 = Harris, 0x90 = Moto) which use same opcode
            int ta = (MAC[2 + len_a] << 16) | (MAC[3 + len_a] << 8) | MAC[4 + len_a];
            int rf = (MAC[5 + len_a] >> 4) & 0xF;
            int ber = MAC[5 + len_a] & 0xF;
            fprintf(stderr, "\n Power Control Signal Quality");
            fprintf(stderr, "\n  Target Address: %d RF: 0x%X BER: 0x%X", ta, rf, ber);
        }

    SKIPCALL:; //do nothing

        if ((len_b + len_c) < 24 && len_c != 0) {
            len_a = len_b;
        } else {
            goto END_PDU;
        }
    }

END_PDU:
    state->p2_is_lcch = 0;
    //debug printing
    if (opts->payload == 1 && MAC[1] != 0) //print only if not a null type //&& MAC[1] != 0 //&& MAC[2] != 0
    {
        fprintf(stderr, "%s", KCYN);
        fprintf(stderr, "\n P25 PDU Payload\n  ");
        for (int i = 0; i < 24; i++) {
            fprintf(stderr, "[%02llX]", MAC[i]);
            if (i == 11) {
                fprintf(stderr, "\n  ");
            }
        }
        fprintf(stderr, "%s", KNRM);
    }
}

// Local bounded append helper (reused pattern across modules)
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
