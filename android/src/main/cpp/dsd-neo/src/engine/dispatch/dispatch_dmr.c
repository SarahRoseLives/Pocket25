// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/dmr/dmr.h>

#include <stdio.h>

int
dsd_dispatch_matches_dmr(int synctype) {
    return DSD_SYNC_IS_DMR(synctype);
}

void
dsd_dispatch_handle_dmr(dsd_opts* opts, dsd_state* state) {

    //Start DMR Types
    if (DSD_SYNC_IS_DMR(state->synctype)) //BS 10-13, MS voice/data 32-33, RC 34
    {

        //print manufacturer strings to branding, disabled 0x10 moto other systems can use that fid set
        //0x06 is trident, but when searching, apparently, they developed con+, but was bought by moto?
        if (state->dmr_mfid == 0x10)
            ; //sprintf (state->dmr_branding, "%s",  "Motorola");
        else if (state->dmr_mfid == 0x68) {
            sprintf(state->dmr_branding, "%s", "  Hytera");
        } else if (state->dmr_mfid == 0x58) {
            sprintf(state->dmr_branding, "%s", "    Tait");
        }

        //disabling these due to random data decodes setting an odd mfid, could be legit, but only for that one packet?
        //or, its just a decode error somewhere
        // else if (state->dmr_mfid == 0x20) sprintf (state->dmr_branding, "%s", "JVC Kenwood");
        // else if (state->dmr_mfid == 0x04) sprintf (state->dmr_branding, "%s", "Flyde Micro");
        // else if (state->dmr_mfid == 0x05) sprintf (state->dmr_branding, "%s", "PROD-EL SPA");
        // else if (state->dmr_mfid == 0x06) sprintf (state->dmr_branding, "%s", "Motorola"); //trident/moto con+
        // else if (state->dmr_mfid == 0x07) sprintf (state->dmr_branding, "%s", "RADIODATA");
        // else if (state->dmr_mfid == 0x08) sprintf (state->dmr_branding, "%s", "Hytera");
        // else if (state->dmr_mfid == 0x09) sprintf (state->dmr_branding, "%s", "ASELSAN");
        // else if (state->dmr_mfid == 0x0A) sprintf (state->dmr_branding, "%s", "Kirisun");
        // else if (state->dmr_mfid == 0x0B) sprintf (state->dmr_branding, "%s", "DMR Association");
        // else if (state->dmr_mfid == 0x13) sprintf (state->dmr_branding, "%s", "EMC S.P.A.");
        // else if (state->dmr_mfid == 0x1C) sprintf (state->dmr_branding, "%s", "EMC S.P.A.");
        // else if (state->dmr_mfid == 0x33) sprintf (state->dmr_branding, "%s", "Radio Activity");
        // else if (state->dmr_mfid == 0x3C) sprintf (state->dmr_branding, "%s", "Radio Activity");
        // else if (state->dmr_mfid == 0x77) sprintf (state->dmr_branding, "%s", "Vertex Standard");

        //disable so radio id doesn't blink in and out during ncurses and aggressive_framesync
        state->nac = 0;

        if (opts->errorbars == 1) {
            if (opts->verbose > 0) {
                //fprintf (stderr,"inlvl: %2i%% ", (int)state->max / 164);
            }
        }
        if ((state->synctype == DSD_SYNC_DMR_BS_VOICE_NEG) || (state->synctype == DSD_SYNC_DMR_BS_VOICE_POS)
            || (state->synctype == DSD_SYNC_DMR_MS_VOICE)) //DMR Voice Modes
        {

            sprintf(state->fsubtype, " VOICE        ");
            if (opts->dmr_stereo == 0 && state->synctype < DSD_SYNC_DMR_MS_VOICE) {
                sprintf(state->slot1light, " slot1 ");
                sprintf(state->slot2light, " slot2 ");
                //we can safely open MBE on any MS or mono handling
                if ((opts->mbe_out_dir[0] != 0) && (opts->mbe_out_f == NULL)) {
                    openMbeOutFile(opts, state);
                }
                if (opts->p25_trunk == 0) {
                    dmrMSBootstrap(opts, state);
                }
            }
            if (opts->dmr_mono == 1 && state->synctype == DSD_SYNC_DMR_MS_VOICE) {
                //we can safely open MBE on any MS or mono handling
                if ((opts->mbe_out_dir[0] != 0) && (opts->mbe_out_f == NULL)) {
                    openMbeOutFile(opts, state);
                }
                if (opts->p25_trunk == 0) {
                    dmrMSBootstrap(opts, state);
                }
            }
            if (opts->dmr_stereo == 1) //opts->dmr_stereo == 1
            {
                state->dmr_stereo = 1; //set the state to 1 when handling pure voice frames
                if (state->synctype >= DSD_SYNC_DMR_MS_VOICE) {
                    //we can safely open MBE on any MS or mono handling
                    if ((opts->mbe_out_dir[0] != 0) && (opts->mbe_out_f == NULL)) {
                        openMbeOutFile(opts, state);
                    }
                    if (opts->p25_trunk == 0) {
                        dmrMSBootstrap(opts, state); //bootstrap into MS Bootstrap (voice only)
                    }
                } else {
                    dmrBSBootstrap(opts, state); //bootstrap into BS Bootstrap
                }
            }
        } else if ((state->synctype == DSD_SYNC_DMR_MS_DATA)
                   || (state->synctype == DSD_SYNC_DMR_RC_DATA)) //MS Data and RC data
        {
            if (opts->mbe_out_f != NULL) {
                closeMbeOutFile(opts, state);
            }
            if (opts->mbe_out_fR != NULL) {
                closeMbeOutFileR(opts, state);
            }
            if (opts->p25_trunk == 0) {
                dmrMSData(opts, state);
            }
        } else {
            if (opts->dmr_stereo == 0) {
                if (opts->mbe_out_f != NULL) {
                    closeMbeOutFile(opts, state);
                }
                if (opts->mbe_out_fR != NULL) {
                    closeMbeOutFileR(opts, state);
                }

                state->err_str[0] = 0;
                sprintf(state->slot1light, " slot1 ");
                sprintf(state->slot2light, " slot2 ");
                dmr_data_sync(opts, state);
            }
            //switch dmr_stereo to 0 when handling BS data frame syncs with processDMRdata
            if (opts->dmr_stereo == 1) {
                if (opts->mbe_out_f != NULL) {
                    closeMbeOutFile(opts, state);
                }
                if (opts->mbe_out_fR != NULL) {
                    closeMbeOutFileR(opts, state);
                }

                state->dmr_stereo = 0; //set the state to zero for handling pure data frames
                sprintf(state->slot1light, " slot1 ");
                sprintf(state->slot2light, " slot2 ");
                dmr_data_sync(opts, state);
            }
        }
        return;
    }
}
