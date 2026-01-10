// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dibit.h>
#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/frame.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/io/control.h>
#include <dsd-neo/protocol/p25/p25.h>
#include <dsd-neo/protocol/p25/p25p1_check_nid.h>
#include <dsd-neo/runtime/colors.h>

#include <mbelib.h>

#include <stdio.h>
#include <string.h>

int
dsd_dispatch_matches_p25p1(int synctype) {
    return DSD_SYNC_IS_P25P1(synctype);
}

void
dsd_dispatch_handle_p25p1(dsd_opts* opts, dsd_state* state) {

    int i, j, dibit;
    char duid[3];
    char nac[13];
    UNUSED(nac);

    char bch_code[63];
    int index_bch_code;
    unsigned char parity;
    char v;
    int new_nac;
    char new_duid[3];
    int check_result;

    nac[12] = 0;
    duid[2] = 0;

    // Read the NAC, 12 bits
    j = 0;
    index_bch_code = 0;
    for (i = 0; i < 6; i++) {
        dibit = getDibit(opts, state);

        v = 1 & (dibit >> 1); // bit 1
        nac[j] = v + '0';
        j++;
        bch_code[index_bch_code] = v;
        index_bch_code++;

        v = 1 & dibit; // bit 0
        nac[j] = v + '0';
        j++;
        bch_code[index_bch_code] = v;
        index_bch_code++;
    }
    //this one setting bogus nac data
    // state->nac = strtol (nac, NULL, 2);

    // Read the DUID, 4 bits
    for (i = 0; i < 2; i++) {
        dibit = getDibit(opts, state);
        duid[i] = dibit + '0';

        bch_code[index_bch_code] = 1 & (dibit >> 1); // bit 1
        index_bch_code++;
        bch_code[index_bch_code] = 1 & dibit; // bit 0
        index_bch_code++;
    }

    // Read the BCH data for error correction of NAC and DUID
    for (i = 0; i < 3; i++) {
        dibit = getDibit(opts, state);

        bch_code[index_bch_code] = 1 & (dibit >> 1); // bit 1
        index_bch_code++;
        bch_code[index_bch_code] = 1 & dibit; // bit 0
        index_bch_code++;
    }
    // Intermission: read and discard the status dibit
    (void)getDibit(opts, state);
    // ... continue reading the BCH error correction data
    for (i = 0; i < 20; i++) {
        dibit = getDibit(opts, state);

        bch_code[index_bch_code] = 1 & (dibit >> 1); // bit 1
        index_bch_code++;
        bch_code[index_bch_code] = 1 & dibit; // bit 0
        index_bch_code++;
    }

    // Read the parity bit
    dibit = getDibit(opts, state);
    bch_code[index_bch_code] = 1 & (dibit >> 1); // bit 1
    parity = (1 & dibit);                        // bit 0

    // Check if the NID is correct
    check_result = check_NID(bch_code, &new_nac, new_duid, parity);
    if (check_result == 1) {
        if (new_nac != state->nac) {
            // NAC fixed by error correction
            state->nac = new_nac;
            //apparently, both 0 and 0xFFF can the BCH code on signal drop
            if (state->p2_hardset == 0 && new_nac != 0 && new_nac != 0xFFF) {
                state->p2_cc = new_nac;
            }
            state->debug_header_errors++;
        }
        if (strcmp(new_duid, duid) != 0) {
            // DUID fixed by error correction
            //fprintf (stderr,"Fixing DUID %s -> %s\n", duid, new_duid);
            duid[0] = new_duid[0];
            duid[1] = new_duid[1];
            state->debug_header_errors++;
        }
    } else {
        if (check_result == -1 && opts->verbose > 0) {
            fprintf(stderr, "%s", KRED);
            fprintf(stderr, " NID PARITY MISMATCH ");
            fprintf(stderr, "%s", KNRM);
        }
        // Check of NID failed and unable to recover its value
        //fprintf (stderr,"NID error\n");
        duid[0] = 'E';
        duid[1] = 'E';
        state->debug_header_critical_errors++;
    }

    if (strcmp(duid, "00") == 0) {
        // Header Data Unit
        if (opts->errorbars == 1) {
            printFrameInfo(opts, state);
            fprintf(stderr, " HDU\n");
        }
        if (opts->mbe_out_dir[0] != 0) {
            if (opts->mbe_out_f != NULL) {
                closeMbeOutFile(opts, state);
            }
            if (opts->mbe_out_f == NULL) {
                openMbeOutFile(opts, state);
            }
        }
        mbe_initMbeParms(state->cur_mp, state->prev_mp, state->prev_mp_enhanced);
        state->lastp25type = 2;
        state->dmrburstL = 25;
        state->currentslot = 0;
        sprintf(state->fsubtype, " HDU          ");
        processHDU(opts, state);
    } else if (strcmp(duid, "11") == 0) {
        // Logical Link Data Unit 1
        if (opts->errorbars == 1) {
            printFrameInfo(opts, state);
            fprintf(stderr, " LDU1  ");
        }
        if (opts->mbe_out_dir[0] != 0) {
            if (opts->mbe_out_f == NULL) {
                openMbeOutFile(opts, state);
            }
        }
        state->lastp25type = 1;
        state->dmrburstL = 26;
        state->currentslot = 0;
        sprintf(state->fsubtype, " LDU1         ");
        state->numtdulc = 0;

        processLDU1(opts, state);
    } else if (strcmp(duid, "22") == 0) {
        // Logical Link Data Unit 2
        state->dmrburstL = 27;
        state->currentslot = 0;
        if (opts->errorbars == 1) {
            printFrameInfo(opts, state);
            if (state->lastp25type != 1) {
                // Late entry: short calls or mid-call tuning can land on an
                // LDU2 first. Decode it anyway so voice isn't lost.
                fprintf(stderr, " LDU2 (late entry)  ");
            } else {
                fprintf(stderr, " LDU2  ");
            }
        }
        if (opts->mbe_out_dir[0] != 0) {
            if (opts->mbe_out_f == NULL) {
                openMbeOutFile(opts, state);
            }
        }
        state->lastp25type = 2;
        sprintf(state->fsubtype, " LDU2         ");
        state->numtdulc = 0;
        processLDU2(opts, state);
    } else if (strcmp(duid, "33") == 0) {
        // Terminator with subsequent Link Control
        state->dmrburstL = 28;
        if (opts->errorbars == 1) {
            printFrameInfo(opts, state);
            fprintf(stderr, " TDULC\n");
        }
        if (opts->mbe_out_dir[0] != 0) {
            if (opts->mbe_out_f != NULL) {
                closeMbeOutFile(opts, state);
            }
        }
        mbe_initMbeParms(state->cur_mp, state->prev_mp, state->prev_mp_enhanced);
        // state->lasttg = 0;
        // state->lastsrc = 0;
        state->lastp25type = 0;
        state->err_str[0] = 0;
        sprintf(state->fsubtype, " TDULC        ");
        // Clear GPS data on call termination
        state->dmr_embedded_gps[0][0] = '\0';
        state->dmr_lrrp_gps[0][0] = '\0';
        state->numtdulc++;
        if ((opts->resume > 0) && (state->numtdulc > opts->resume)) {
            resumeScan(opts, state);
        }
        processTDULC(opts, state);
        state->err_str[0] = 0;
    } else if (strcmp(duid, "03") == 0) {
        // Terminator without subsequent Link Control
        state->dmrburstL = 28;
        if (opts->errorbars == 1) {
            printFrameInfo(opts, state);
            fprintf(stderr, " TDU\n");
        }
        if (opts->mbe_out_dir[0] != 0) {
            if (opts->mbe_out_f != NULL) {
                closeMbeOutFile(opts, state);
            }
        }
        mbe_initMbeParms(state->cur_mp, state->prev_mp, state->prev_mp_enhanced);
        state->lasttg = 0;
        state->lastsrc = 0;
        state->lastp25type = 0;
        state->err_str[0] = 0;
        sprintf(state->fsubtype, " TDU          ");
        // Clear GPS data on call termination
        state->dmr_embedded_gps[0][0] = '\0';
        state->dmr_lrrp_gps[0][0] = '\0';

        processTDU(opts, state);
    } else if (strcmp(duid, "13") == 0) {
        state->dmrburstL = 29;
        if (opts->errorbars == 1) {
            printFrameInfo(opts, state);
            fprintf(stderr, " TSBK");
        }
        if (opts->mbe_out_dir[0] != 0) {
            if (opts->mbe_out_f != NULL) {
                closeMbeOutFile(opts, state);
            }
            if (opts->mbe_out_fR != NULL) {
                closeMbeOutFileR(opts, state);
            }
        }
        if (opts->resume > 0) {
            resumeScan(opts, state);
        }
        state->lasttg = 0;
        state->lastsrc = 0;
        state->lastp25type = 3;
        sprintf(state->fsubtype, " TSBK         ");

        processTSBK(opts, state);

    } else if (strcmp(duid, "30") == 0) {
        state->dmrburstL = 29;
        if (opts->errorbars == 1) {
            printFrameInfo(opts, state);
            fprintf(stderr, " MPDU\n"); //multi block format PDU
        }
        if (opts->mbe_out_dir[0] != 0) {
            if (opts->mbe_out_f != NULL) {
                closeMbeOutFile(opts, state);
            }
            if (opts->mbe_out_fR != NULL) {
                closeMbeOutFileR(opts, state);
            }
        }
        if (opts->resume > 0) {
            resumeScan(opts, state);
        }
        state->lastp25type = 4;
        sprintf(state->fsubtype, " MPDU         ");

        processMPDU(opts, state);
    }

    else {
        state->lastp25type = 0;
        sprintf(state->fsubtype, "              ");
        if (opts->errorbars == 1) {
            printFrameInfo(opts, state);
            // fprintf (stderr," duid:%s *Unknown DUID*\n", duid);
            fprintf(stderr, " duid:%s \n", duid); //DUID ERR
        }
    }
}
