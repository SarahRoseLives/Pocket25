// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
 * p25p1_tsbk.c
 * P25p1 Trunking Signal Block Handler (with majority-vote over 3 reps)
 *
 * LWVMOBILE
 * 2022-10 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dibit.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#ifdef USE_RTLSDR
#include <dsd-neo/runtime/rtl_stream_metrics_hooks.h>
#endif
#include <dsd-neo/protocol/p25/p25_12.h>
#include <dsd-neo/protocol/p25/p25_callsign.h>
#include <dsd-neo/protocol/p25/p25_crc.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/protocol/p25/p25_vpdu.h>
#include <dsd-neo/runtime/colors.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

void
processTSBK(dsd_opts* opts, dsd_state* state) {
    state->p25_p1_duid_tsbk++;

    // Reset counters and buffers to avoid carryover from voice paths
    state->voice_counter[0] = 0;
    state->voice_counter[1] = 0;
    memset(state->s_l4, 0, sizeof(state->s_l4));
    memset(state->s_r4, 0, sizeof(state->s_r4));
    opts->slot_preference = 2;

    // Ensure slot index is sane when swapping protocols
    state->currentslot = 0;

    // Clear stale Active Channel messages after idle
    const time_t now = time(NULL);
    if ((now - state->last_active_time) > 3) {
        memset(state->active_channel, 0, sizeof(state->active_channel));
    }

    // Buffers
    uint8_t tsbk_dibit[98];
    memset(tsbk_dibit, 0, sizeof(tsbk_dibit));

    uint8_t tsbk_reliab[98]; // per-dibit reliability for soft decoding
    memset(tsbk_reliab, 255, sizeof(tsbk_reliab));

    uint8_t tsbk_byte[12]; // per repetition
    memset(tsbk_byte, 0, sizeof(tsbk_byte));

    // Store decoded 96 bits from each of the 3 repetitions
    uint8_t rep_bits[3][96];
    memset(rep_bits, 0, sizeof(rep_bits));
    uint8_t rep_bytes[3][12];
    memset(rep_bytes, 0, sizeof(rep_bytes));
    int rep_crc[3] = {-2, -2, -2};

    unsigned long long int PDU[24];
    memset(PDU, 0, sizeof(PDU));

    int tsbk_decoded_bits[96];
    memset(tsbk_decoded_bits, 0, sizeof(tsbk_decoded_bits));

    int i, j, k, x;
    int err = -2; // CRC16 result (majority)

    int protectbit = 0;
    int MFID = 0xFF;

    // Status-dibit skipping state
    int skipdibit = 36 - 14;
    int status_count = 1;
    int dibit_count = 0;
    UNUSED(status_count);
    UNUSED(dibit_count);

    // Collect up to 3 repetitions of 101 dibits (with status dibits interlaced)
    int reps_got = 0;
    for (j = 0; j < 3; j++) {
        k = 0;
        for (i = 0; i < 101; i++) {
            uint8_t rel = 255;
            int dibit = getDibitWithReliability(opts, state, &rel);
            if ((skipdibit / 36) == 0) {
                dibit_count++;
                tsbk_dibit[k] = (uint8_t)dibit;
                tsbk_reliab[k] = rel;
                k++;
            } else {
                skipdibit = 0;
                status_count++;
            }
            skipdibit++;
        }

        // 1/2-rate soft decode this repetition
        (void)p25_12_soft(tsbk_dibit, tsbk_reliab, tsbk_byte);

        // Convert decoded bytes into a 96-bit MSB-first vector
        k = 0;
        for (i = 0; i < 12; i++) {
            for (x = 0; x < 8; x++) {
                tsbk_decoded_bits[k++] = ((tsbk_byte[i] << x) & 0x80) >> 7;
            }
        }
        for (i = 0; i < 96; i++) {
            rep_bits[j][i] = (uint8_t)(tsbk_decoded_bits[i] & 1);
        }
        memcpy(rep_bytes[j], tsbk_byte, 12);

        // Compute per-repetition CRC16 over first 80 bits for later selection
        rep_crc[j] = crc16_lb_bridge(tsbk_decoded_bits, 80);

        reps_got++;

        // If this repetition indicates Last Block, further reps typically stop.
        // Use what we have for majority to avoid blending with the next message.
        int lb_rep = (tsbk_byte[0] >> 7) & 0x1;
        if (lb_rep) {
            break;
        }
    }

    // Majority-vote across available repetitions (1..3)
    uint8_t maj_bits[96];
    memset(maj_bits, 0, sizeof(maj_bits));
    int reps = (reps_got > 0) ? reps_got : 1;
    int thresh = (reps >= 2) ? ((reps + 1) / 2) : 1;
    for (i = 0; i < 96; i++) {
        int sum = 0;
        for (int r = 0; r < reps; r++) {
            sum += (int)rep_bits[r][i];
        }
        maj_bits[i] = (uint8_t)((sum >= thresh) ? 1 : 0);
    }

    // Select best repetition: prefer any CRC-pass rep; otherwise fall back to majority
    int sel_idx = -1;
    for (int r = 0; r < reps; r++) {
        if (rep_crc[r] == 0) {
            sel_idx = r;
            break;
        }
    }
    if (sel_idx >= 0) {
        // Use passing repetition
        memcpy(tsbk_byte, rep_bytes[sel_idx], 12);
        err = 0;
    } else {
        // No rep passed; compute CRC on majority and use majority bytes
        int maj_bits_int[96];
        for (i = 0; i < 96; i++) {
            maj_bits_int[i] = (int)maj_bits[i];
        }
        err = crc16_lb_bridge(maj_bits_int, 80);
        // Rebuild bytes from majority bits for downstream parsing
        for (i = 0; i < 12; i++) {
            int byte = 0;
            for (x = 0; x < 8; x++) {
                byte = (byte << 1) | (maj_bits[(i * 8) + x] & 1);
            }
            tsbk_byte[i] = (uint8_t)byte;
        }
    }

    // Update FEC counters once per message
    if (err == 0) {
        // Refresh CC activity on any good TSBK decode to keep the SM from
        // hunting prematurely when CC is healthy but TSBK cadence is sparse.
        state->last_cc_sync_time = time(NULL);
        state->last_cc_sync_time_m = dsd_time_now_monotonic_s();
        state->p25_p1_fec_ok++;
#ifdef USE_RTLSDR
        dsd_rtl_stream_metrics_hook_p25p1_ber_update(1, 0);
#endif
    } else {
        state->p25_p1_fec_err++;
#ifdef USE_RTLSDR
        dsd_rtl_stream_metrics_hook_p25p1_ber_update(0, 1);
#endif
    }

    // Basic field extraction
    MFID = tsbk_byte[1];
    protectbit = (tsbk_byte[0] >> 6) & 0x1;
    /* lb not needed here: only used intra-repetition for early break */

    // Prepare a MAC-like PDU form when applicable
    PDU[0] = 0x07; // P25p1 TSBK DUID
    PDU[1] = tsbk_byte[0] & 0x3F;
    PDU[2] = tsbk_byte[2];
    PDU[3] = tsbk_byte[3];
    PDU[4] = tsbk_byte[4];
    PDU[5] = tsbk_byte[5];
    PDU[6] = tsbk_byte[6];
    PDU[7] = tsbk_byte[7];
    PDU[8] = tsbk_byte[8];
    PDU[9] = tsbk_byte[9];
    PDU[10] = 0; // strip CRC for vPDU search
    PDU[11] = 0;
    PDU[1] ^= 0x40; // flip to match MAC_PDU flavor (3D -> 7D)

    // Downstream handling on majority-decoded frame
    if (MFID < 0x2 && protectbit == 0 && err == 0 && PDU[1] != 0x7B) {
        fprintf(stderr, "%s", KYEL);
        process_MAC_VPDU(opts, state, 0, PDU);
        fprintf(stderr, "%s", KNRM);
    } else if (MFID == 0x90 && protectbit == 0 && err == 0) {
        // Motorola MFID90 Group Regroup opcodes
        int opcode = tsbk_byte[0] & 0x3F;
        fprintf(stderr, "%s", KYEL);

        if (opcode == 0x00) {
            // MFID90 GRG Add Command: sg(16), ga1(16), ga2(16), ga3(16)
            int sg = (tsbk_byte[2] << 8) | tsbk_byte[3];
            int ga1 = (tsbk_byte[4] << 8) | tsbk_byte[5];
            int ga2 = (tsbk_byte[6] << 8) | tsbk_byte[7];
            int ga3 = (tsbk_byte[8] << 8) | tsbk_byte[9];
            fprintf(stderr, "\n MFID90 (Moto) Group Regroup Add Command\n");
            fprintf(stderr, "  SG: %d", sg);
            if (ga1 != 0) {
                fprintf(stderr, " GA1: %d", ga1);
                p25_patch_add_wgid(state, sg, ga1);
            }
            if (ga2 != 0) {
                fprintf(stderr, " GA2: %d", ga2);
                p25_patch_add_wgid(state, sg, ga2);
            }
            if (ga3 != 0) {
                fprintf(stderr, " GA3: %d", ga3);
                p25_patch_add_wgid(state, sg, ga3);
            }
            fprintf(stderr, "\n");
            p25_patch_update(state, sg, /*is_patch*/ 1, /*active*/ 1);

        } else if (opcode == 0x01) {
            // MFID90 GRG Del Command: sg(16), ga1(16), ga2(16), ga3(16)
            int sg = (tsbk_byte[2] << 8) | tsbk_byte[3];
            int ga1 = (tsbk_byte[4] << 8) | tsbk_byte[5];
            int ga2 = (tsbk_byte[6] << 8) | tsbk_byte[7];
            int ga3 = (tsbk_byte[8] << 8) | tsbk_byte[9];
            fprintf(stderr, "\n MFID90 (Moto) Group Regroup Delete Command\n");
            fprintf(stderr, "  SG: %d", sg);
            if (ga1 != 0) {
                fprintf(stderr, " GA1: %d", ga1);
                p25_patch_remove_wgid(state, sg, ga1);
            }
            if (ga2 != 0) {
                fprintf(stderr, " GA2: %d", ga2);
                p25_patch_remove_wgid(state, sg, ga2);
            }
            if (ga3 != 0) {
                fprintf(stderr, " GA3: %d", ga3);
                p25_patch_remove_wgid(state, sg, ga3);
            }
            fprintf(stderr, "\n");

        } else if (opcode == 0x02) {
            // MFID90 GRG Channel Grant: reserved(8), ch(16), sg(16), sa(24)
            int channel = (tsbk_byte[3] << 8) | tsbk_byte[4];
            int sg = (tsbk_byte[5] << 8) | tsbk_byte[6];
            int source = (tsbk_byte[7] << 16) | (tsbk_byte[8] << 8) | tsbk_byte[9];
            fprintf(stderr, "\n MFID90 (Moto) Group Regroup Channel Grant\n");
            fprintf(stderr, "  CHAN [%04X] SG: %d SRC: %d", channel, sg, source);
            long int freq = process_channel_to_freq(opts, state, channel);
            char suf[32];
            p25_format_chan_suffix(state, (uint16_t)channel, -1, suf, sizeof suf);
            sprintf(state->active_channel[0], "MFID90 GRG Grant: %04X%s SG: %d; ", channel, suf, sg);
            state->last_active_time = time(NULL);
            fprintf(stderr, "\n");
            // Route through SM for tuning (GRG grants don't carry SVC bits)
            if (opts->p25_trunk == 1 && freq != 0) {
                p25_sm_on_group_grant(opts, state, channel, /*svc*/ 0, sg, source);
            }

        } else if (opcode == 0x03) {
            // MFID90 GRG Channel Grant Update: ch1(16), sg1(16), ch2(16), sg2(16)
            int ch1 = (tsbk_byte[2] << 8) | tsbk_byte[3];
            int sg1 = (tsbk_byte[4] << 8) | tsbk_byte[5];
            int ch2 = (tsbk_byte[6] << 8) | tsbk_byte[7];
            int sg2 = (tsbk_byte[8] << 8) | tsbk_byte[9];
            fprintf(stderr, "\n MFID90 (Moto) Group Regroup Channel Grant Update\n");
            fprintf(stderr, "  CH1 [%04X] SG1: %d  CH2 [%04X] SG2: %d", ch1, sg1, ch2, sg2);
            long int freq1 = (ch1 != 0) ? process_channel_to_freq(opts, state, ch1) : 0;
            long int freq2 = (ch2 != 0) ? process_channel_to_freq(opts, state, ch2) : 0;
            char suf1[32], suf2[32];
            p25_format_chan_suffix(state, (uint16_t)ch1, -1, suf1, sizeof suf1);
            p25_format_chan_suffix(state, (uint16_t)ch2, -1, suf2, sizeof suf2);
            sprintf(state->active_channel[0], "MFID90 GRG Upd: %04X%s SG: %d; ", ch1, suf1, sg1);
            state->last_active_time = time(NULL);
            fprintf(stderr, "\n");
            // Route both through SM for tuning consideration
            if (opts->p25_trunk == 1 && ch1 != 0 && freq1 != 0) {
                p25_sm_on_group_grant(opts, state, ch1, /*svc*/ 0, sg1, /*src*/ 0);
            }
            if (opts->p25_trunk == 1 && ch2 != 0 && freq2 != 0) {
                p25_sm_on_group_grant(opts, state, ch2, /*svc*/ 0, sg2, /*src*/ 0);
            }

        } else if (opcode == 0x09) {
            // MFID90 Motorola Scan Marker Broadcast
            // Per OP25: mk(4), ms(8), value(16) - used for scan priority/ordering
            int mk = (tsbk_byte[2] >> 4) & 0x0F;
            int ms = tsbk_byte[3];
            int value = (tsbk_byte[4] << 8) | tsbk_byte[5];
            fprintf(stderr, "\n MFID90 (Moto) Scan Marker Broadcast\n");
            fprintf(stderr, "  MK: %d MS: %d Value: %d\n", mk, ms, value);

        } else if (opcode == 0x0A) {
            // MFID90 Motorola Emergency Alarm Activation
            // Source address triggering the emergency
            int source = (tsbk_byte[7] << 16) | (tsbk_byte[8] << 8) | tsbk_byte[9];
            fprintf(stderr, "\n MFID90 (Moto) Emergency Alarm Activation\n");
            fprintf(stderr, "  Source: %d", source);
            fprintf(stderr, " %s** EMERGENCY **%s\n", KRED, KYEL);

        } else if (opcode == 0x0B) {
            // MFID90 Motorola System Information / BSI
            // Field layout is proprietary; log raw bytes and show computed callsign from WACN/SysID
            fprintf(stderr, "\n MFID90 (Moto) System Information (BSI)\n");
            fprintf(stderr, "  Data: %02X %02X %02X %02X %02X %02X %02X %02X", tsbk_byte[2], tsbk_byte[3], tsbk_byte[4],
                    tsbk_byte[5], tsbk_byte[6], tsbk_byte[7], tsbk_byte[8], tsbk_byte[9]);
            // Show computed callsign from current WACN/SysID if available
            if (opts->show_p25_callsign_decode && (state->p2_wacn != 0 || state->p2_sysid != 0)) {
                char callsign[7];
                p25_wacn_sysid_to_callsign((uint32_t)state->p2_wacn, (uint16_t)state->p2_sysid, callsign);
                fprintf(stderr, " [Callsign: %s]", callsign);
            }
            fprintf(stderr, "\n");
        }

        fprintf(stderr, "%s", KNRM);
    } else if (MFID == 0xA4 && protectbit == 0 && err == 0) {
        // Harris regrouping summaries
        if ((tsbk_byte[0] & 0x3F) == 0x30) {
            int sg = (tsbk_byte[3] << 8) | tsbk_byte[4];
            int key = (tsbk_byte[5] << 8) | tsbk_byte[6];
            int add = (tsbk_byte[7] << 16) | (tsbk_byte[8] << 8) | tsbk_byte[9];
            int tga = tsbk_byte[2] >> 5;
            int ssn = tsbk_byte[2] & 0x1F;
            fprintf(stderr, "%s", KYEL);
            fprintf(stderr, "\n MFID A4 (Harris) Group Regroup Explicit Encryption Command\n");
            if ((tga & 0x2) == 2) {
                fprintf(stderr, "  SG: %d; KEY: %04X; WGID: %d; ", sg, key, add);
                p25_patch_add_wgid(state, sg, add);
            } else {
                fprintf(stderr, "  SG: %d; KEY: %04X; WUID: %d; ", sg, key, add);
                p25_patch_add_wuid(state, sg, (uint32_t)add);
            }
            fprintf(stderr, (tga & 0x4) ? " Simulselect" : " Patch");
            fprintf(stderr, (tga & 0x1) ? " Active;" : " Inactive;");
            fprintf(stderr, " SSN: %02d \n", ssn);
            int is_patch = ((tga & 0x4) == 0) ? 1 : 0;
            int active = (tga & 0x1) ? 1 : 0;
            p25_patch_update(state, sg, is_patch, active);
            // TSBK form carries KEY and SSN; ALG not present here.
            p25_patch_set_kas(state, sg, key, -1, ssn);
        }
    } else if (protectbit == 0 && err == 0 && (tsbk_byte[0] & 0x3F) == 0x3B) {
        // Network Status Broadcast (Abbreviated)
        long int wacn = (tsbk_byte[3] << 12) | (tsbk_byte[4] << 4) | (tsbk_byte[5] >> 4);
        int sysid = ((tsbk_byte[5] & 0xF) << 8) | tsbk_byte[6];
        int channel = (tsbk_byte[7] << 8) | tsbk_byte[8];
        fprintf(stderr, "%s", KYEL);
        fprintf(stderr, "\n Network Status Broadcast TSBK - Abbreviated \n");
        fprintf(stderr, "  WACN [%05lX] SYSID [%03X] NAC [%03llX]", wacn, sysid, state->p2_cc);
        if (opts->show_p25_callsign_decode) {
            char callsign[7];
            p25_wacn_sysid_to_callsign((uint32_t)wacn, (uint16_t)sysid, callsign);
            fprintf(stderr, " [%s]", callsign);
        }
        state->p25_cc_freq = process_channel_to_freq(opts, state, channel);
        long neigh[1] = {state->p25_cc_freq};
        p25_sm_on_neighbor_update(opts, state, neigh, 1);
        state->p25_cc_is_tdma = 0;
        if (state->trunk_lcn_freq[0] == 0 || state->trunk_lcn_freq[0] != state->p25_cc_freq) {
            state->trunk_lcn_freq[0] = state->p25_cc_freq;
        }
        if (state->p2_hardset == 0) {
            state->p2_wacn = wacn;
            state->p2_sysid = sysid;
        }
        p25_confirm_idens_for_current_site(state);
    }

    fprintf(stderr, "%s", KNRM);
    fprintf(stderr, "\n");

    // When on a CC, rotate the symbol out file every hour, if enabled
    rotate_symbol_out_file(opts, state);
}
