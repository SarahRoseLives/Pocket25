// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
 * p25p2_xcch.c
 * Phase 2 SACCH/FACCH/LCCH Handling
 *
 * LWVMOBILE
 * 2022-09 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_crc.h>
#include <dsd-neo/protocol/p25/p25_lfsr.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/protocol/p25/p25_vpdu.h>
#include <dsd-neo/runtime/colors.h>
#include <dsd-neo/runtime/p25_p2_audio_ring.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

void
process_SACCH_MAC_PDU(dsd_opts* opts, dsd_state* state, int payload[180]) {
    //Figure out which PDU we are looking at, see above info on 8.4.1
    //reorganize bits into bytes and process accordingly

    //new slot variable with flipped assignment for SACCH
    uint8_t slot = (state->currentslot ^ 1) & 1;

    unsigned long long int SMAC[24] = {0}; //22.5 bytes for SACCH MAC PDUs
    int byte = 0;
    int k = 0;
    for (int j = 0; j < 22; j++) {
        for (int i = 0; i < 8; i++) {
            byte = byte << 1;
            byte = byte | payload[k];
            k++;
        }
        SMAC[j] = byte;
        byte = 0; //reset byte
    }
    SMAC[22] = (payload[176] << 7) | (payload[177] << 6) | (payload[178] << 5) | (payload[179] << 4);

    int opcode = 0;
    opcode = (payload[0] << 2) | (payload[1] << 1) | (payload[2] << 0);
    int mac_offset = 0;
    mac_offset = (payload[3] << 2) | (payload[4] << 1) | (payload[5] << 0);
    int res = (payload[6] << 1) | (payload[7] << 0);
    int b = 9;
    b = (payload[8] << 1) | (payload[9] << 0); //combined b1 and b2
    int mco_a = 0;
    // Message Carrying Octets (6 bits) packed in payload[10..15]
    mco_a = (payload[10] << 5) | (payload[11] << 4) | (payload[12] << 3) | (payload[13] << 2) | (payload[14] << 1)
            | (payload[15] << 0);
    UNUSED3(mac_offset, b, mco_a);

    //get the second mco after determining first message length, see what second mco is and plan accordingly
    int mco_b = 69;
    UNUSED(mco_b);

    //attempt CRC12 check to validate or reject PDU
    int err = -2;
    if (state->p2_is_lcch == 0) {
        err = crc12_xb_bridge(payload, 180 - 12);
        if (err != 0) //CRC Failure, warn or skip.
        {
            if (SMAC[1] == 0x0) //NULL PDU Check, pass if NULL type
            {
                //fprintf (stderr, " NULL ");
            } else {
                fprintf(stderr, " CRC12 ERR S");
                goto END_SMAC;
            }
        }
    }
    if (state->p2_is_lcch == 1) {
        // Per TIA-102 Phase 2 LCCH, CRC16 covers the full fixed data span
        // preceding the 16-bit CRC (total ACCH block is 180 bits). Do not
        // derive the span from MCO; vendors may leave unused octets as 0s
        // yet CRC still spans the full region. Thus, use 164 data bits.
        const int len = 164; // 164 data + 16 CRC = 180 bits
        err = crc16_lb_bridge(payload, len);
        if (err != 0) //CRC Failure, warn or skip.
        {
            if (SMAC[1] == 0x0) //NULL PDU Check, pass if NULL type
            {
                //fprintf (stderr, " NULL ");
                state->p2_is_lcch = 0; //turn flag off here
                goto END_SMAC;
            } else //permit MAC_SIGNAL on CRC ERR if -F option called
            {
                if (opts->aggressive_framesync == 1) {
                    fprintf(stderr, " CRC16 ERR L");
                    // If this is a MAC_SIGNAL opcode, still clear per-slot audio
                    // gates and flush rings before exiting to avoid stale gates
                    // wedging the trunking SM release.
                    if (opcode == 0x0) {
                        // Gate only the indicated slot on MAC_SIGNAL; do not
                        // disrupt the opposite slot which may still carry
                        // clear audio. SACCH slot mapping is inverted.
                        state->p25_p2_audio_allowed[slot] = 0;
                        p25_p2_audio_ring_reset(state, slot);
                    }
                    state->p2_is_lcch = 0; //turn flag off here
                    goto END_SMAC;
                }
            }
        }
    }

    //remember, slots are inverted here, so set the opposite ones
    //monitor, test, and remove these if they cause issues due to inversion
    if (opcode == 0x0) {
        fprintf(stderr, " MAC_SIGNAL ");
        //warn user instead of failing
        if (err != 0) {
            fprintf(stderr, "%s", KRED);
            fprintf(stderr, "CRC16 ERR ");
        }
        fprintf(stderr, "%s", KYEL);
        process_MAC_VPDU(opts, state, 1, SMAC);
        fprintf(stderr, "%s", KNRM);
        // Do not change per-slot audio gating on MAC_SIGNAL. LCCH/MAC_SIGNAL
        // indicates control traffic on the logical channel but not an explicit
        // end of PTT; premature gating here can suppress a clear call that
        // arrives on the opposite slot while an encrypted call is ongoing.
        // Audio gating is managed on MAC_PTT/ACTIVE (enable) and MAC_END/IDLE (disable).
    }
    //do not permit MAC_PTT with CRC errs, help prevent false positives on calls
    if (opcode == 0x1 && err == 0) {
        fprintf(stderr, " MAC_PTT ");
        fprintf(stderr, "%s", KGRN);
        // SM event: PTT on logical slot
        p25_sm_emit_ptt(opts, state, slot);
        // Mark recent activity for this logical slot to avoid early bounce
        state->p25_p2_last_mac_active[slot] = time(NULL);
        state->p25_p2_last_mac_active_m[slot] = dsd_time_now_monotonic_s();
        state->p25_p2_last_mac_active_m[slot] = dsd_time_now_monotonic_s();
        // SACCH uses inverted mapping; 'slot' is the logical voice channel
        if (slot == 0) {
            //reset fourv_counter and dropbyte on PTT
            state->fourv_counter[0] = 0;
            state->voice_counter[0] = 0;
            state->dropL = 256;

            fprintf(stderr, "\n VCH 1 - ");
            //check that src is not zero first, some harris and other patch systems may do this,
            //but that also causes an issue in the new event logger if the active channel has a src, but mac_ptt has 0
            uint32_t src = (SMAC[13] << 16) | (SMAC[14] << 8) | SMAC[15];
            if (src != 0) {
                state->lastsrc = (SMAC[13] << 16) | (SMAC[14] << 8) | SMAC[15];
            }
            state->lasttg = (SMAC[16] << 8) | SMAC[17];

            fprintf(stderr, "TG %d ", state->lasttg);
            fprintf(stderr, "SRC %d ", src);

            /*
			When the talker radio is initiating an individual call (unit to unit or telephone interconnect), the reserved group ID of zero
			is used in the group address portion of the MAC_PTT PDU and the source ID is that of the talker radio.

			The outbound SACCH for the talker radio containing the talker ID information is required at the
			talker radio site and optional at other sites. (So, if its from an external site, then it can be zero?)

			*/

            // if (state->lastsrc == 0) fprintf (stderr, "External ");

            state->payload_algid = SMAC[10];
            state->payload_keyid = (SMAC[11] << 8) | SMAC[12];
            state->payload_miP = (SMAC[1] << 56) | (SMAC[2] << 48) | (SMAC[3] << 40) | (SMAC[4] << 32) | (SMAC[5] << 24)
                                 | (SMAC[6] << 16) | (SMAC[7] << 8) | (SMAC[8] << 0);

            if (state->payload_algid != 0x80 && state->payload_algid != 0x0) {
                fprintf(stderr, "%s", KYEL);
                fprintf(stderr, "\n         ALG ID: 0x%02X", state->payload_algid);
                fprintf(stderr, " KEY ID: 0x%04X", state->payload_keyid);
                fprintf(stderr, " MI: 0x%016llX", state->payload_miP);
                fprintf(stderr, " MPTT");
                if (state->R != 0 && state->payload_algid == 0xAA) {
                    fprintf(stderr, " Key 0x%010llX", state->R);
                }
                if (state->R != 0 && state->payload_algid == 0x81) {
                    fprintf(stderr, " Key 0x%016llX", state->R);
                }

                if ((state->payload_algid == 0x84 || state->payload_algid == 0x89) && state->aes_key_loaded[0] == 1) {
                    fprintf(stderr, "\n ");
                    fprintf(stderr, "Key: %016llX %016llX ", state->A1[0], state->A2[0]);
                    if (state->payload_algid == 0x84) {
                        fprintf(stderr, "%016llX %016llX", state->A3[0], state->A4[0]);
                    }
                    // opts->unmute_encrypted_p25 = 1; //needed?
                }

                //expand 64-bit MI to 128-bit for AES
                if (state->payload_algid == 0x84 || state->payload_algid == 0x89) {
                    LFSR128(state);
                }
                // Emit ENC event to SM for lockout decision
                p25_sm_emit_enc(opts, state, slot, state->payload_algid, state->payload_keyid, state->lasttg);
            }

            //reset gain
            if (opts->floating_point == 1) {
                state->aout_gain = opts->audio_gain;
            }
            // Conditionally enable audio only if clear or decryptable
            {
                int allow_audio = 0;
                int alg = (slot == 0) ? state->payload_algid : state->payload_algidR;
                unsigned long long key = (slot == 0) ? state->R : state->RR;
                int aes_loaded = state->aes_key_loaded[slot];
                if (alg == 0 || alg == 0x80 || ((alg == 0xAA || alg == 0x81 || alg == 0x9F) && key != 0)
                    || ((alg == 0x84 || alg == 0x89) && aes_loaded == 1)) {
                    allow_audio = 1; // clear or decryptable with key
                }
                state->p25_p2_audio_allowed[slot] = allow_audio;
                // Only set PTT burst indicator when audio is allowed; encrypted/locked-out
                // calls should not influence audio routing decisions
                if (allow_audio) {
                    state->dmrburstL = 20;
                }
            }
        }

        if (slot == 1) {
            //reset fourv_counter and dropbyte on PTT
            state->fourv_counter[1] = 0;
            state->voice_counter[1] = 0;
            state->dropR = 256;
            state->payload_algidR = 0; //zero this out as well

            fprintf(stderr, "\n VCH 2 - ");
            //check that src is not zero first, some harris and other patch systems may do this,
            //but that also causes an issue in the new event logger if the active channel has a src, but mac_ptt has 0
            uint32_t src = (SMAC[13] << 16) | (SMAC[14] << 8) | SMAC[15];
            if (src != 0) {
                state->lastsrcR = (SMAC[13] << 16) | (SMAC[14] << 8) | SMAC[15];
            }
            state->lasttgR = (SMAC[16] << 8) | SMAC[17];

            fprintf(stderr, "TG %d ", state->lasttgR);
            fprintf(stderr, "SRC %d ", src);

            // if (state->lastsrcR == 0) fprintf (stderr, "External ");

            state->payload_algidR = SMAC[10];
            state->payload_keyidR = (SMAC[11] << 8) | SMAC[12];
            state->payload_miN = (SMAC[1] << 56) | (SMAC[2] << 48) | (SMAC[3] << 40) | (SMAC[4] << 32) | (SMAC[5] << 24)
                                 | (SMAC[6] << 16) | (SMAC[7] << 8) | (SMAC[8] << 0);

            if (state->payload_algidR != 0x80 && state->payload_algidR != 0x0) {
                fprintf(stderr, "%s", KYEL);
                fprintf(stderr, "\n         ALG ID: 0x%02X", state->payload_algidR);
                fprintf(stderr, " KEY ID: 0x%04X", state->payload_keyidR);
                fprintf(stderr, " MI: 0x%016llX", state->payload_miN);
                fprintf(stderr, " MPTT");

                if (state->RR != 0 && state->payload_algidR == 0xAA) {
                    fprintf(stderr, " Key 0x%010llX", state->RR);
                }
                if (state->RR != 0 && state->payload_algidR == 0x81) {
                    fprintf(stderr, " Key 0x%016llX", state->RR);
                }
                if ((state->payload_algidR == 0x84 || state->payload_algidR == 0x89) && state->aes_key_loaded[1] == 1) {
                    fprintf(stderr, "\n ");
                    fprintf(stderr, "Key: %016llX %016llX ", state->A1[1], state->A2[1]);
                    if (state->payload_algidR == 0x84) {
                        fprintf(stderr, "%016llX %016llX", state->A3[1], state->A4[1]);
                    }
                    // opts->unmute_encrypted_p25 = 1; //needed?
                }

                //expand 64-bit MI to 128-bit for AES
                if (state->payload_algidR == 0x84 || state->payload_algidR == 0x89) {
                    LFSR128(state);
                }
                // Emit ENC event to SM for lockout decision
                p25_sm_emit_enc(opts, state, slot, state->payload_algidR, state->payload_keyidR, state->lasttgR);
            }

            //reset gain
            if (opts->floating_point == 1) {
                state->aout_gainR = opts->audio_gain;
            }
            // Conditionally enable audio only if clear or decryptable
            {
                int allow_audio = 0;
                int alg = (slot == 0) ? state->payload_algid : state->payload_algidR;
                unsigned long long key = (slot == 0) ? state->R : state->RR;
                int aes_loaded = state->aes_key_loaded[slot];
                if (alg == 0 || alg == 0x80 || ((alg == 0xAA || alg == 0x81 || alg == 0x9F) && key != 0)
                    || ((alg == 0x84 || alg == 0x89) && aes_loaded == 1)) {
                    allow_audio = 1;
                }
                state->p25_p2_audio_allowed[slot] = allow_audio;
                // Only set PTT burst indicator when audio is allowed; encrypted/locked-out
                // calls should not influence audio routing decisions
                if (allow_audio) {
                    state->dmrburstR = 20;
                }
            }
        }

        if (opts->payload == 1) {
            fprintf(stderr, "\n MAC_PTT_PAYLOAD_S OFFSET: %d RES: %d \n ", mac_offset, res);
            for (int i = 0; i < 24; i++) {
                if (i == 12) {
                    fprintf(stderr, "\n ");
                }
                fprintf(stderr, "[%02llX]", SMAC[i]);
            }
        }

        //reset voice counter at MAC_PTT (inverted, triple check please)
        if (state->currentslot == 1 && state->payload_algidR == 0x81) {
            state->DMRvcL = 0;
        }

        if (state->currentslot == 0 && state->payload_algid == 0x81) {
            state->DMRvcR = 0;
        }

        //reset voice counter after 2V (AES 256)
        if (state->currentslot == 1 && state->payload_algidR == 0x84) {
            state->DMRvcL = 0;
        }

        if (state->currentslot == 0 && state->payload_algid == 0x84) {
            state->DMRvcR = 0;
        }

        //reset voice counter after 2V (AES 128)
        if (state->currentslot == 1 && state->payload_algidR == 0x89) {
            state->DMRvcL = 0;
        }

        if (state->currentslot == 0 && state->payload_algid == 0x89) {
            state->DMRvcR = 0;
        }

        fprintf(stderr, "%s", KNRM);
    }
    //do not permit MAC_PTT_END with CRC errs, help prevent false positives on calls
    if (opcode == 0x2 && err == 0) {
        fprintf(stderr, " MAC_END_PTT ");
        fprintf(stderr, "%s", KRED);
        // SM event: END on logical slot
        p25_sm_emit_end(opts, state, slot);
        // Mark end-of-PTT for this logical slot to allow SM tick to release
        // early once per-slot audio/jitter drains.
        state->p25_p2_last_end_ptt[slot] = time(NULL);
        // SACCH logical slot index
        if (slot == 0) {

            state->fourv_counter[0] = 0;
            state->voice_counter[0] = 0;
            state->dropL = 256;
            state->dmrburstL = 23;
            state->payload_algid = 0;
            state->payload_keyid = 0;

            fprintf(stderr, "\n VCH 1 - ");
            fprintf(stderr, "TG %d ", state->lasttg);
            fprintf(stderr, "SRC %d ", state->lastsrc);

            //print it and then zero out
            state->lastsrc = 0;
            state->lasttg = 0;

            //close any open MBEout files
            if (opts->mbe_out_f != NULL) {
                closeMbeOutFile(opts, state);
            }

            //blank the call string here -- slot variable is already flipped accordingly for sacch
            sprintf(state->call_string[slot], "%s", "                     "); //21 spaces -- wrong placement!

            // Gate this slot so no new frames are queued; let jitter ring drain.
            state->p25_p2_audio_allowed[slot] = 0;
            // Release decision handled by SM tick based on hangtime/slot activity.

            //reset gain
            if (opts->floating_point == 1) {
                state->aout_gain = opts->audio_gain;
            }

            //clear stale keys if loaded
            if (state->keyloader == 1) {
                state->R = 0;
                state->A1[0] = 0;
                state->A2[0] = 0;
                state->A3[0] = 0;
                state->A4[0] = 0;
                state->aes_key_loaded[0] = 0;
                // state->H = 0; //shim for above (this apply here?)
            }

            // Clear GPS data for this slot
            state->dmr_embedded_gps[0][0] = '\0';
            state->dmr_lrrp_gps[0][0] = '\0';
        }
        if (slot == 1) {

            state->fourv_counter[1] = 0;
            state->voice_counter[1] = 0;
            state->dropR = 256;
            state->dmrburstR = 23;
            state->payload_algidR = 0;
            state->payload_keyidR = 0;

            fprintf(stderr, "\n VCH 2 - ");
            fprintf(stderr, "TG %d ", state->lasttgR);
            fprintf(stderr, "SRC %d ", state->lastsrcR);

            //print it and then zero out
            state->lastsrcR = 0;
            state->lasttgR = 0;

            //close any open MBEout files
            if (opts->mbe_out_fR != NULL) {
                closeMbeOutFileR(opts, state);
            }

            //blank the call string here -- slot variable is already flipped accordingly for sacch
            sprintf(state->call_string[slot], "%s", "                     "); //21 spaces -- wrong placement!

            // Gate this slot so no new frames are queued; let jitter ring drain.
            state->p25_p2_audio_allowed[slot] = 0;
            // Release decision handled by SM tick based on hangtime/slot activity.

            //reset gain
            if (opts->floating_point == 1) {
                state->aout_gainR = opts->audio_gain;
            }

            //clear stale keys if loaded
            if (state->keyloader == 1) {
                state->RR = 0;
                state->A1[1] = 0;
                state->A2[1] = 0;
                state->A3[1] = 0;
                state->A4[1] = 0;
                state->aes_key_loaded[1] = 0;
                // state->H = 0; //shim for above (this apply here?)
            }

            // Clear GPS data for this slot
            state->dmr_embedded_gps[1][0] = '\0';
            state->dmr_lrrp_gps[1][0] = '\0';
        }

        // Disable audio for this slot. Do not flush the jitter ring; let it
        // drain to avoid chopping tails when IDLE arrives closely after voice.
        state->p25_p2_audio_allowed[slot] = 0;
        // Do not force release here; rely on the trunking SM tick and
        // hangtime/grace logic to decide teardown. Immediate release on
        // back-to-back MAC_IDLE can bounce on marginal signals before
        // MAC_ACTIVE is observed.

        fprintf(stderr, "%s", KNRM);
    }
    if (opcode == 0x3 && err == 0) {
        if (slot == 0) {
            state->dmrburstL = 24;
        } else {
            state->dmrburstR = 24;
        }
        fprintf(stderr, " MAC_IDLE ");
        fprintf(stderr, "%s", KYEL);
        process_MAC_VPDU(opts, state, 1, SMAC);
        fprintf(stderr, "%s", KNRM);
        // SM event: IDLE on logical slot
        p25_sm_emit_idle(opts, state, slot);

        //blank the call string here -- slot variable is already flipped accordingly for sacch
        sprintf(state->call_string[slot], "%s", "                     "); //21 spaces

        // Disable audio for this slot
        state->p25_p2_audio_allowed[slot] = 0;
        // Clear Packet/Data flag for this slot on IDLE
        state->p25_call_is_packet[slot] = 0;
        // Release decision handled by SM tick based on hangtime/slot activity.
    }
    if (opcode == 0x4 && err == 0) {
        fprintf(stderr, " MAC_ACTIVE ");
        fprintf(stderr, "%s", KYEL);
        process_MAC_VPDU(opts, state, 1, SMAC);
        // Mark recent activity for this logical slot to avoid early bounce
        state->p25_p2_last_mac_active[slot] = time(NULL);
        state->p25_p2_last_mac_active_m[slot] = dsd_time_now_monotonic_s();
        fprintf(stderr, "%s", KNRM);
        // Enable audio per policy (respect encryption, key presence, and ignore stale packet bit when clear)
        {
            int allow_audio = 0;
            int alg = (slot == 0) ? state->payload_algid : state->payload_algidR;
            unsigned long long key = (slot == 0) ? state->R : state->RR;
            int aes_loaded = state->aes_key_loaded[slot];
            // If stream is clear or decryptable, enable regardless of a stale Packet/Data flag
            if (alg == 0 || alg == 0x80 || ((alg == 0xAA || alg == 0x81 || alg == 0x9F) && key != 0)
                || ((alg == 0x84 || alg == 0x89) && aes_loaded == 1)) {
                allow_audio = 1;
            } else {
                // Otherwise, suppress audio for Packet/Data sessions
                allow_audio = state->p25_call_is_packet[slot] ? 0 : allow_audio;
            }
            state->p25_p2_audio_allowed[slot] = allow_audio;

            // Only set voice-active burst indicator when audio is allowed;
            // encrypted/locked-out calls should not influence audio routing
            // SACCH uses inverted slot mapping; use 'slot' (not currentslot)
            if (allow_audio) {
                if (slot == 0) {
                    state->dmrburstL = 21;
                } else {
                    state->dmrburstR = 21;
                }
            }
        }

        // Emit ENC event for dual indication (SM tracks pending/confirmed)
        {
            int alg = (slot == 0) ? state->payload_algid : state->payload_algidR;
            int keyid = (slot == 0) ? state->payload_keyid : state->payload_keyidR;
            int tg = (slot == 0) ? state->lasttg : state->lasttgR;
            if (alg != 0 && alg != 0x80) {
                p25_sm_emit_enc(opts, state, slot, alg, keyid, tg);
            }
        }
    }
    if (opcode == 0x6 && err == 0) {
        if (state->currentslot == 1) {
            state->dmrburstL = 22;
            //close any open MBEout files
            if (opts->mbe_out_f != NULL) {
                closeMbeOutFile(opts, state);
            }
        } else {
            state->dmrburstR = 22;
            //close any open MBEout files
            if (opts->mbe_out_fR != NULL) {
                closeMbeOutFileR(opts, state);
            }
        }
        fprintf(stderr, " MAC_HANGTIME ");
        fprintf(stderr, "%s", KYEL);
        process_MAC_VPDU(opts, state, 1, SMAC);
        fprintf(stderr, "%s", KNRM);
    }

END_SMAC:
    return;
}

void
process_FACCH_MAC_PDU(dsd_opts* opts, dsd_state* state, int payload[156]) {
    //Figure out which PDU we are looking at, see above info on 8.4.1
    //reorganize bits into bytes and process accordingly

    //new slot variable
    uint8_t slot = state->currentslot;

    unsigned long long int FMAC[24] = {0}; //19.5 bytes for FACCH MAC PDUs, add padding to end
    int byte = 0;
    int k = 0;
    for (int j = 0; j < 19; j++) {
        for (int i = 0; i < 8; i++) {
            byte = byte << 1;
            byte = byte | payload[k];
            k++;
        }
        FMAC[j] = byte;
        byte = 0; //reset byte
    }
    FMAC[19] = (payload[152] << 7) | (payload[153] << 6) | (payload[154] << 5) | (payload[155] << 4);

    //add padding bytes so we can have a unified variable MAC PDU handler
    for (int i = 0; i < 3; i++) {
        FMAC[i + 20] = 0;
    }

    int opcode = 0;
    opcode = (payload[0] << 2) | (payload[1] << 1) | (payload[2] << 0);
    int mac_offset = 0;
    mac_offset = (payload[3] << 2) | (payload[4] << 1) | (payload[5] << 0);
    int res = (payload[6] << 1) | (payload[7] << 0);
    UNUSED(mac_offset);

    //attempt CRC check to validate or reject PDU
    int err = -2;
    if (state->p2_is_lcch == 0) {
        err = crc12_xb_bridge(payload, 156 - 12);
        if (err != 0) //CRC Failure, warn or skip.
        {
            if (FMAC[1] == 0x0) //NULL PDU Check, pass if NULL
            {
                //fprintf (stderr, " NULL ");
            } else {
                fprintf(stderr, " CRC12 ERR F");
                goto END_FMAC;
            }
        }
    }

    //Not sure if a MAC Signal will come on a FACCH or not, so disable to prevent falsing
    // if (opcode == 0x0)
    // {
    // 	fprintf (stderr, " MAC_SIGNAL ");
    // 	fprintf (stderr, "%s", KYEL);
    // 	process_MAC_VPDU(opts, state, 0, FMAC);
    // 	fprintf (stderr, "%s", KNRM);
    // }

    if (opcode == 0x1 && err == 0) {

        fprintf(stderr, " MAC_PTT  ");
        fprintf(stderr, "%s", KGRN);
        if (state->currentslot == 0) {
            //reset fourv_counter and dropbyte on PTT
            state->fourv_counter[0] = 0;
            state->voice_counter[0] = 0;
            state->dropL = 256;

            state->dmrburstL = 20;
            fprintf(stderr, "\n VCH 1 - ");
            //check that src is not zero first, some harris and other patch systems may do this,
            //but that also causes an issue in the new event logger if the active channel has a src, but mac_ptt has 0
            uint32_t src = (FMAC[13] << 16) | (FMAC[14] << 8) | FMAC[15];
            if (src != 0) {
                state->lastsrc = (FMAC[13] << 16) | (FMAC[14] << 8) | FMAC[15];
            }
            state->lasttg = (FMAC[16] << 8) | FMAC[17];

            fprintf(stderr, "TG %d ", state->lasttg);
            fprintf(stderr, "SRC %d ", src);

            // if (state->lastsrc == 0) fprintf (stderr, "External ");

            state->payload_algid = FMAC[10];
            state->payload_keyid = (FMAC[11] << 8) | FMAC[12];
            state->payload_miP = (FMAC[1] << 56) | (FMAC[2] << 48) | (FMAC[3] << 40) | (FMAC[4] << 32) | (FMAC[5] << 24)
                                 | (FMAC[6] << 16) | (FMAC[7] << 8) | (FMAC[8] << 0);

            if (state->payload_algid != 0x80 && state->payload_algid != 0x0) {
                fprintf(stderr, "%s", KYEL);
                fprintf(stderr, "\n         ALG ID: 0x%02X", state->payload_algid);
                fprintf(stderr, " KEY ID: 0x%04X", state->payload_keyid);
                fprintf(stderr, " MI: 0x%016llX", state->payload_miP);
                fprintf(stderr, " MPTT");
                if (state->R != 0 && state->payload_algid == 0xAA) {
                    fprintf(stderr, " Key 0x%010llX", state->R);
                }
                if (state->R != 0 && state->payload_algid == 0x81) {
                    fprintf(stderr, " Key 0x%016llX", state->R);
                }

                if ((state->payload_algid == 0x84 || state->payload_algid == 0x89) && state->aes_key_loaded[0] == 1) {
                    fprintf(stderr, "\n ");
                    fprintf(stderr, "Key: %016llX %016llX ", state->A1[0], state->A2[0]);
                    if (state->payload_algid == 0x84) {
                        fprintf(stderr, "%016llX %016llX", state->A3[0], state->A4[0]);
                    }
                    // opts->unmute_encrypted_p25 = 1; //needed?
                }

                //expand 64-bit MI to 128-bit for AES
                if (state->payload_algid == 0x84 || state->payload_algid == 0x89) {
                    LFSR128(state);
                }
                // Emit ENC event to SM for lockout decision
                p25_sm_emit_enc(opts, state, slot, state->payload_algid, state->payload_keyid, state->lasttg);
            }

            //reset gain
            if (opts->floating_point == 1) {
                state->aout_gain = opts->audio_gain;
            }
            // Conditionally enable audio only if clear or decryptable
            {
                int allow_audio = 0;
                int alg = (slot == 0) ? state->payload_algid : state->payload_algidR;
                unsigned long long key = (slot == 0) ? state->R : state->RR;
                int aes_loaded = state->aes_key_loaded[slot];
                if (alg == 0 || alg == 0x80 || ((alg == 0xAA || alg == 0x81 || alg == 0x9F) && key != 0)
                    || ((alg == 0x84 || alg == 0x89) && aes_loaded == 1)) {
                    allow_audio = 1; // clear or decryptable with key
                }
                state->p25_p2_audio_allowed[slot] = allow_audio;
            }
        }

        if (state->currentslot == 1) {
            //reset fourv_counter and dropbyte on PTT
            state->fourv_counter[1] = 0;
            state->voice_counter[1] = 0;
            state->dropR = 256;

            state->dmrburstR = 20;
            fprintf(stderr, "\n VCH 2 - ");
            //check that src is not zero first, some harris and other patch systems may do this,
            //but that also causes an issue in the new event logger if the active channel has a src, but mac_ptt has 0
            uint32_t src = (FMAC[13] << 16) | (FMAC[14] << 8) | FMAC[15];
            if (src != 0) {
                state->lastsrcR = (FMAC[13] << 16) | (FMAC[14] << 8) | FMAC[15];
            }
            state->lasttgR = (FMAC[16] << 8) | FMAC[17];

            fprintf(stderr, "TG %d ", state->lasttgR);
            fprintf(stderr, "SRC %d ", src);

            // if (state->lastsrcR == 0) fprintf (stderr, "External ");

            state->payload_algidR = FMAC[10];
            state->payload_keyidR = (FMAC[11] << 8) | FMAC[12];
            state->payload_miN = (FMAC[1] << 56) | (FMAC[2] << 48) | (FMAC[3] << 40) | (FMAC[4] << 32) | (FMAC[5] << 24)
                                 | (FMAC[6] << 16) | (FMAC[7] << 8) | (FMAC[8] << 0);

            if (state->payload_algidR != 0x80 && state->payload_algidR != 0x0) {
                fprintf(stderr, "%s", KYEL);
                fprintf(stderr, "\n         ALG ID: 0x%02X", state->payload_algidR);
                fprintf(stderr, " KEY ID: 0x%04X", state->payload_keyidR);
                fprintf(stderr, " MI: 0x%016llX", state->payload_miN);
                fprintf(stderr, " MPTT");

                if (state->RR != 0 && state->payload_algidR == 0xAA) {
                    fprintf(stderr, " Key 0x%010llX", state->RR);
                }
                if (state->RR != 0 && state->payload_algidR == 0x81) {
                    fprintf(stderr, " Key 0x%016llX", state->RR);
                }
                if ((state->payload_algidR == 0x84 || state->payload_algidR == 0x89) && state->aes_key_loaded[1] == 1) {
                    fprintf(stderr, "\n ");
                    fprintf(stderr, "Key: %016llX %016llX ", state->A1[1], state->A2[1]);
                    if (state->payload_algidR == 0x84) {
                        fprintf(stderr, "%016llX %016llX", state->A3[1], state->A4[1]);
                    }
                    // opts->unmute_encrypted_p25 = 1; //needed?
                }

                //expand 64-bit MI to 128-bit for AES
                if (state->payload_algidR == 0x84 || state->payload_algidR == 0x89) {
                    LFSR128(state);
                }
                // Emit ENC event to SM for lockout decision
                p25_sm_emit_enc(opts, state, slot, state->payload_algidR, state->payload_keyidR, state->lasttgR);
            }

            //reset gain
            if (opts->floating_point == 1) {
                state->aout_gainR = opts->audio_gain;
            }
            // Conditionally enable audio only if clear or decryptable
            {
                int allow_audio = 0;
                int alg = (slot == 0) ? state->payload_algid : state->payload_algidR;
                unsigned long long key = (slot == 0) ? state->R : state->RR;
                int aes_loaded = state->aes_key_loaded[slot];
                if (alg == 0 || alg == 0x80 || ((alg == 0xAA || alg == 0x81 || alg == 0x9F) && key != 0)
                    || ((alg == 0x84 || alg == 0x89) && aes_loaded == 1)) {
                    allow_audio = 1;
                }
                state->p25_p2_audio_allowed[slot] = allow_audio;
            }
        }

        if (opts->payload == 1) {
            fprintf(stderr, "\n MAC_PTT_PAYLOAD_F OFFSET: %d RES: %d \n ", mac_offset, res);
            for (int i = 0; i < 24; i++) {
                if (i == 12) {
                    fprintf(stderr, "\n ");
                }
                fprintf(stderr, "[%02llX]", FMAC[i]);
            }
        }
        fprintf(stderr, "%s", KNRM);

        //reset voice counter at MAC_PTT
        if (state->currentslot == 0 && state->payload_algid == 0x81) {
            state->DMRvcL = 0;
        }

        if (state->currentslot == 1 && state->payload_algidR == 0x81) {
            state->DMRvcR = 0;
        }

        //reset voice counter after 2V (AES 256)
        if (state->currentslot == 0 && state->payload_algid == 0x84) {
            state->DMRvcL = 0;
        }

        if (state->currentslot == 1 && state->payload_algidR == 0x84) {
            state->DMRvcR = 0;
        }

        //reset voice counter after 2V (AES 128)
        if (state->currentslot == 0 && state->payload_algid == 0x89) {
            state->DMRvcL = 0;
        }

        if (state->currentslot == 1 && state->payload_algidR == 0x89) {
            state->DMRvcR = 0;
        }
    }
    if (opcode == 0x2 && err == 0) {
        fprintf(stderr, " MAC_END_PTT ");
        fprintf(stderr, "%s", KRED);
        // SM event: END on this slot
        p25_sm_emit_end(opts, state, slot);
        if (state->currentslot == 0) {

            state->fourv_counter[0] = 0;
            state->voice_counter[0] = 0;
            state->dropL = 256;
            state->dmrburstL = 23;
            state->payload_algid = 0; //zero this out as well
            state->payload_keyid = 0;

            fprintf(stderr, "\n VCH 1 - ");
            fprintf(stderr, "TG %d ", state->lasttg);
            fprintf(stderr, "SRC %d ", state->lastsrc);

            //print it and then zero out
            state->lastsrc = 0;
            state->lasttg = 0;

            //close any open MBEout files
            if (opts->mbe_out_f != NULL) {
                closeMbeOutFile(opts, state);
            }

            //blank the call string here
            sprintf(state->call_string[slot], "%s", "                     "); //21 spaces

            //reset gain
            if (opts->floating_point == 1) {
                state->aout_gain = opts->audio_gain; //reset
            }

            //clear stale keys if loaded
            if (state->keyloader == 1) {
                state->R = 0;
                state->A1[0] = 0;
                state->A2[0] = 0;
                state->A3[0] = 0;
                state->A4[0] = 0;
                state->aes_key_loaded[0] = 0;
                // state->H = 0; //shim for above (this apply here?)
            }

            // Clear GPS data for this slot
            state->dmr_embedded_gps[0][0] = '\0';
            state->dmr_lrrp_gps[0][0] = '\0';
        }
        if (state->currentslot == 1) {

            state->fourv_counter[1] = 0;
            state->voice_counter[1] = 0;
            state->dropR = 256;
            state->dmrburstR = 23;
            state->payload_algidR = 0; //zero this out as well
            state->payload_keyidR = 0;

            fprintf(stderr, "\n VCH 2 - ");
            fprintf(stderr, "TG %d ", state->lasttgR);
            fprintf(stderr, "SRC %d ", state->lastsrcR);

            //print it and then zero out
            state->lastsrcR = 0;
            state->lasttgR = 0;

            //close any open MBEout files
            if (opts->mbe_out_fR != NULL) {
                closeMbeOutFileR(opts, state);
            }

            //reset gain
            if (opts->floating_point == 1) {
                state->aout_gainR = opts->audio_gain;
            }

            //clear stale keys if loaded
            if (state->keyloader == 1) {
                state->RR = 0;
                state->A1[1] = 0;
                state->A2[1] = 0;
                state->A3[1] = 0;
                state->A4[1] = 0;
                state->aes_key_loaded[1] = 0;
                // state->H = 0; //shim for above (this apply here?)
            }

            // Clear GPS data for this slot
            state->dmr_embedded_gps[1][0] = '\0';
            state->dmr_lrrp_gps[1][0] = '\0';
        }

        // Gate this slot so no new frames are queued; let jitter ring drain.
        state->p25_p2_audio_allowed[slot] = 0;
        // Release decision handled by SM tick based on hangtime/slot activity.

        fprintf(stderr, "%s", KNRM);
    }
    if (opcode == 0x3 && err == 0) {
        //what else should we zero out here?
        //disable any of the lines below if issues are observed
        if (state->currentslot == 0) {
            state->payload_algid = 0;
            state->payload_keyid = 0;
            state->dmrburstL = 24;
            state->fourv_counter[0] = 0;
            state->voice_counter[0] = 0;
            state->lastsrc = 0;
            state->lasttg = 0;

        } else {
            state->payload_algidR = 0;
            state->payload_keyidR = 0;
            state->dmrburstR = 24;
            state->fourv_counter[1] = 0;
            state->voice_counter[1] = 0;
            state->lastsrcR = 0;
            state->lasttgR = 0;
        }
        fprintf(stderr, " MAC_IDLE ");
        fprintf(stderr, "%s", KYEL);
        process_MAC_VPDU(opts, state, 0, FMAC);
        fprintf(stderr, "%s", KNRM);

        //blank the call string here
        sprintf(state->call_string[slot], "%s", "                     "); //21 spaces
        // SM event: IDLE on this slot
        p25_sm_emit_idle(opts, state, slot);
        // Disable audio for this slot
        state->p25_p2_audio_allowed[slot] = 0;
        // Flush ring for this slot to drop any residual samples
        p25_p2_audio_ring_reset(state, slot);
        // Release decision handled by SM tick based on hangtime/slot activity.
    }
    if (opcode == 0x4 && err == 0) {
        if (state->currentslot == 0) {
            state->dmrburstL = 21;
        } else {
            state->dmrburstR = 21;
        }

        fprintf(stderr, " MAC_ACTIVE ");
        fprintf(stderr, "%s", KYEL);
        process_MAC_VPDU(opts, state, 0, FMAC);
        fprintf(stderr, "%s", KNRM);
        // SM event: ACTIVE on logical slot
        p25_sm_emit_active(opts, state, slot);
        // Enable audio per policy (respect encryption, key presence, and ignore stale packet bit when clear)
        {
            int allow_audio = 0;
            int alg = (slot == 0) ? state->payload_algid : state->payload_algidR;
            unsigned long long key = (slot == 0) ? state->R : state->RR;
            int aes_loaded = state->aes_key_loaded[slot];
            if (alg == 0 || alg == 0x80 || ((alg == 0xAA || alg == 0x81 || alg == 0x9F) && key != 0)
                || ((alg == 0x84 || alg == 0x89) && aes_loaded == 1)) {
                allow_audio = 1; // clear or decryptable with key
            } else {
                allow_audio = state->p25_call_is_packet[slot] ? 0 : allow_audio;
            }
            state->p25_p2_audio_allowed[slot] = allow_audio;
        }

        // Emit ENC event for dual indication (SM tracks pending/confirmed)
        {
            int alg = (slot == 0) ? state->payload_algid : state->payload_algidR;
            int keyid = (slot == 0) ? state->payload_keyid : state->payload_keyidR;
            int tg = (slot == 0) ? state->lasttg : state->lasttgR;
            if (alg != 0 && alg != 0x80) {
                p25_sm_emit_enc(opts, state, slot, alg, keyid, tg);
            }
        }
    }
    if (opcode == 0x6 && err == 0) {
        if (state->currentslot == 0) {
            state->dmrburstL = 22;
            //close any open MBEout files
            if (opts->mbe_out_f != NULL) {
                closeMbeOutFile(opts, state);
            }
        } else {
            state->dmrburstR = 22;
            //close any open MBEout files
            if (opts->mbe_out_fR != NULL) {
                closeMbeOutFileR(opts, state);
            }
        }
        fprintf(stderr, " MAC_HANGTIME ");
        fprintf(stderr, "%s", KYEL);
        process_MAC_VPDU(opts, state, 0, FMAC);
        fprintf(stderr, "%s", KNRM);
    }

END_FMAC:
    return;
}
