// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
* dsd_events.c
* DSD-FME event history init, watchdog, push, and related functions
*
*
* LWVMOBILE
* 2025-05 DSD-FME Florida Man Edition
*-----------------------------------------------------------------------------*/

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/protocol/edacs/edacs_afs.h>
#include <dsd-neo/runtime/git_ver.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

// Safe bounded copy helper that tolerates potential overlap
static inline void
copy_str_field(char* dst, const char* src, size_t cap) {
    if (dst == NULL || src == NULL || cap == 0) {
        return;
    }
    size_t n = strnlen(src, cap - 1);
    memmove(dst, src, n);
    dst[n] = '\0';
}

//init each event history struct passed into here
void
init_event_history(Event_History_I* event_struct, uint8_t start, uint8_t stop) {
    for (uint8_t i = start; i < stop; i++) {
        event_struct->Event_History_Items[i].write = 0;
        event_struct->Event_History_Items[i].color_pair = 4;
        event_struct->Event_History_Items[i].systype = -1;
        event_struct->Event_History_Items[i].subtype = -1;
        event_struct->Event_History_Items[i].sys_id1 = 0;
        event_struct->Event_History_Items[i].sys_id2 = 0;
        event_struct->Event_History_Items[i].sys_id3 = 0;
        event_struct->Event_History_Items[i].sys_id4 = 0;
        event_struct->Event_History_Items[i].sys_id5 = 0;
        event_struct->Event_History_Items[i].gi = 0;
        event_struct->Event_History_Items[i].enc = 0;
        event_struct->Event_History_Items[i].enc_alg = 0;
        event_struct->Event_History_Items[i].enc_key = 0;
        event_struct->Event_History_Items[i].mi = 0;
        event_struct->Event_History_Items[i].svc = 0;
        event_struct->Event_History_Items[i].source_id = 0;
        event_struct->Event_History_Items[i].target_id = 0;
        event_struct->Event_History_Items[i].src_str[0] = '\0';
        event_struct->Event_History_Items[i].tgt_str[0] = '\0';
        event_struct->Event_History_Items[i].t_name[0] = '\0';
        event_struct->Event_History_Items[i].s_name[0] = '\0';
        event_struct->Event_History_Items[i].t_mode[0] = '\0';
        event_struct->Event_History_Items[i].s_mode[0] = '\0';
        event_struct->Event_History_Items[i].channel = 0;
        event_struct->Event_History_Items[i].event_time = 0;

        memset(event_struct->Event_History_Items[i].pdu, 0, sizeof(event_struct->Event_History_Items[0].pdu));
        event_struct->Event_History_Items[i].sysid_string[0] = '\0';
        event_struct->Event_History_Items[i].alias[0] = '\0';
        event_struct->Event_History_Items[i].gps_s[0] = '\0';
        event_struct->Event_History_Items[i].text_message[0] = '\0';
        event_struct->Event_History_Items[i].event_string[0] = '\0';
        event_struct->Event_History_Items[i].internal_str[0] = '\0';
    }
}

void
push_event_history(Event_History_I* event_struct) {

    //Fixed, had it going in the wrong direction first time
    for (uint8_t i = 254; i >= 1; i--) {
        event_struct->Event_History_Items[i].write = event_struct->Event_History_Items[i - 1].write;
        event_struct->Event_History_Items[i].color_pair = event_struct->Event_History_Items[i - 1].color_pair;
        event_struct->Event_History_Items[i].systype = event_struct->Event_History_Items[i - 1].systype;
        event_struct->Event_History_Items[i].subtype = event_struct->Event_History_Items[i - 1].subtype;
        event_struct->Event_History_Items[i].sys_id1 = event_struct->Event_History_Items[i - 1].sys_id1;
        event_struct->Event_History_Items[i].sys_id2 = event_struct->Event_History_Items[i - 1].sys_id2;
        event_struct->Event_History_Items[i].sys_id3 = event_struct->Event_History_Items[i - 1].sys_id3;
        event_struct->Event_History_Items[i].sys_id4 = event_struct->Event_History_Items[i - 1].sys_id4;
        event_struct->Event_History_Items[i].sys_id5 = event_struct->Event_History_Items[i - 1].sys_id5;
        event_struct->Event_History_Items[i].gi = event_struct->Event_History_Items[i - 1].gi;
        event_struct->Event_History_Items[i].enc = event_struct->Event_History_Items[i - 1].enc;
        event_struct->Event_History_Items[i].enc_alg = event_struct->Event_History_Items[i - 1].enc_alg;
        event_struct->Event_History_Items[i].enc_key = event_struct->Event_History_Items[i - 1].enc_key;
        event_struct->Event_History_Items[i].mi = event_struct->Event_History_Items[i - 1].mi;
        event_struct->Event_History_Items[i].svc = event_struct->Event_History_Items[i - 1].svc;
        event_struct->Event_History_Items[i].source_id = event_struct->Event_History_Items[i - 1].source_id;
        event_struct->Event_History_Items[i].target_id = event_struct->Event_History_Items[i - 1].target_id;
        copy_str_field(event_struct->Event_History_Items[i].src_str, event_struct->Event_History_Items[i - 1].src_str,
                       sizeof event_struct->Event_History_Items[i].src_str);
        copy_str_field(event_struct->Event_History_Items[i].tgt_str, event_struct->Event_History_Items[i - 1].tgt_str,
                       sizeof event_struct->Event_History_Items[i].tgt_str);
        copy_str_field(event_struct->Event_History_Items[i].t_name, event_struct->Event_History_Items[i - 1].t_name,
                       sizeof event_struct->Event_History_Items[i].t_name);
        copy_str_field(event_struct->Event_History_Items[i].s_name, event_struct->Event_History_Items[i - 1].s_name,
                       sizeof event_struct->Event_History_Items[i].s_name);
        copy_str_field(event_struct->Event_History_Items[i].t_mode, event_struct->Event_History_Items[i - 1].t_mode,
                       sizeof event_struct->Event_History_Items[i].t_mode);
        copy_str_field(event_struct->Event_History_Items[i].s_mode, event_struct->Event_History_Items[i - 1].s_mode,
                       sizeof event_struct->Event_History_Items[i].s_mode);
        event_struct->Event_History_Items[i].channel = event_struct->Event_History_Items[i - 1].channel;
        event_struct->Event_History_Items[i].event_time = event_struct->Event_History_Items[i - 1].event_time;

        memcpy(event_struct->Event_History_Items[i].pdu, event_struct->Event_History_Items[i - 1].pdu,
               sizeof(event_struct->Event_History_Items[0].pdu));
        copy_str_field(event_struct->Event_History_Items[i].sysid_string,
                       event_struct->Event_History_Items[i - 1].sysid_string,
                       sizeof event_struct->Event_History_Items[i].sysid_string);
        copy_str_field(event_struct->Event_History_Items[i].alias, event_struct->Event_History_Items[i - 1].alias,
                       sizeof event_struct->Event_History_Items[i].alias);
        copy_str_field(event_struct->Event_History_Items[i].gps_s, event_struct->Event_History_Items[i - 1].gps_s,
                       sizeof event_struct->Event_History_Items[i].gps_s);
        copy_str_field(event_struct->Event_History_Items[i].text_message,
                       event_struct->Event_History_Items[i - 1].text_message,
                       sizeof event_struct->Event_History_Items[i].text_message);
        copy_str_field(event_struct->Event_History_Items[i].event_string,
                       event_struct->Event_History_Items[i - 1].event_string,
                       sizeof event_struct->Event_History_Items[i].event_string);
        copy_str_field(event_struct->Event_History_Items[i].internal_str,
                       event_struct->Event_History_Items[i - 1].internal_str,
                       sizeof event_struct->Event_History_Items[i].internal_str);
    }
}

void
write_event_to_log_file(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t swrite,
                        char* event_string) //pass completed event string here that is in the struct
{

    //open log file
    FILE* event_log_file;
    event_log_file = fopen(opts->event_out_file, "a");

    if (event_log_file != NULL) {
        fprintf(event_log_file, "%s ", event_string);
        if (swrite == 1) {
            fprintf(event_log_file, "Slot %d; ", slot + 1);
        }
        fprintf(event_log_file, "\n");

        if (state->event_history_s[slot].Event_History_Items[0].text_message[0] != '\0') {
            fprintf(event_log_file, "%s \n", state->event_history_s[slot].Event_History_Items[0].text_message);
        }
        if (state->event_history_s[slot].Event_History_Items[0].alias[0] != '\0') {
            fprintf(event_log_file, " Talker Alias: %s \n", state->event_history_s[slot].Event_History_Items[0].alias);
        }
        if (state->event_history_s[slot].Event_History_Items[0].gps_s[0] != '\0') {
            fprintf(event_log_file, " GPS: %s \n", state->event_history_s[slot].Event_History_Items[0].gps_s);
        }
        if (state->event_history_s[slot].Event_History_Items[0].internal_str[0] != '\0') {
            fprintf(event_log_file, " DSD-neo: %s \n",
                    state->event_history_s[slot].Event_History_Items[0].internal_str);
        }

        //flush and close log file
        fflush(event_log_file);
        fclose(event_log_file);
    }
}

// run once per loop to check for and push and update event history
void
watchdog_event_history(dsd_opts* opts, dsd_state* state, uint8_t slot) {

    //create a pointer to the current slot event history
    Event_History_I* event_struct = &state->event_history_s[slot];

    //is this a TDMA slot (append Slot value to end of written event history)
    uint8_t swrite = 0;

    //who is currently talking
    uint32_t source_id = 0;

    //last values pulled from the event history
    uint32_t last_source_id = event_struct->Event_History_Items[0].source_id;

    if (slot == 0) {
        source_id = state->lastsrc;
    } else {
        source_id = state->lastsrcR;
    }

    //If DMR BS or P25P2, then flag the swrite, so write can append slot value to event history log
    if (DSD_SYNC_IS_DMR_BS(state->lastsynctype) || DSD_SYNC_IS_P25P2(state->lastsynctype)) {
        swrite = 1;
    }

    if (slot == 0) //BUGFIX: generic catch on FDMA systems so that we don't write duplicate data to slot 2 event history
    {
        //NXDN RID (TODO: Changeover to lastsrc later on)
        if (DSD_SYNC_IS_NXDN(state->lastsynctype)) {
            source_id = state->nxdn_last_rid;
        }

        if (DSD_SYNC_IS_YSF(state->lastsynctype)) //YSF Fusion
        {
            source_id = 0;
            if (strncmp(state->ysf_src, "          ", 10) != 0) //if this field does not have ten spaces in it
            {
                for (uint8_t i = 0; i < 11; i++) {
                    source_id += state->ysf_src[i]; //convert to sum value to make a distinct enough src value
                }
            }
        }

        if (state->lastsynctype == DSD_SYNC_M17_STR_POS || state->lastsynctype == DSD_SYNC_M17_STR_NEG
            || state->lastsynctype == DSD_SYNC_M17_LSF_POS || state->lastsynctype == DSD_SYNC_M17_LSF_NEG) //M17 STR
        {
            source_id = (uint32_t)state->m17_src;
        }

        if (DSD_SYNC_IS_DSTAR(state->lastsynctype)) //DSTAR
        {
            source_id = 0;
            for (uint8_t i = 0; i < 12; i++) {
                source_id += state->dstar_src[i]; //convert to sum value to make a distinct enough src value
            }

            //need a strncmp here for 8 spaces in this field first so we don't blip a blank into the event history
            if (strncmp(state->dstar_src, "        ", 8) == 0) {
                source_id = 0;
            }
        }

        if (DSD_SYNC_IS_DPMR(state->lastsynctype)) //dPMR
        {
            source_id = 0;
            for (uint8_t i = 0; i < 20; i++) {
                source_id += state->dpmr_caller_id[i]; //convert to sum value to make a distinct enough src value
            }

            //need a strncmp here for 8 spaces in this field first so we don't blip a blank into the event history
            if (strncmp(state->dpmr_caller_id, "      ", 6) == 0) {
                source_id = 0;
            }
        }

        if (DSD_SYNC_IS_EDACS(state->lastsynctype)) //EDACS Calls
        {
            source_id = 0;
            if (opts->p25_is_tuned == 1) {
                source_id = state->lastsrc;
            }
        }
    }

    //call alert beep when new call detected
    if (last_source_id == 0 && source_id != 0 && opts->call_alert == 1) {
        beeper(opts, state, slot, 40, 86, 3);
    }

    if (source_id != last_source_id && last_source_id != 0) {

        if (opts->event_out_file[0] != 0) {
            write_event_to_log_file(opts, state, slot, swrite, event_struct->Event_History_Items[0].event_string);
        }

        event_struct->Event_History_Items[0].write = 1; //written, or pushed at this point

        if (opts->static_wav_file == 0) {

            if (slot == 0 && opts->wav_out_f != NULL) {
                opts->wav_out_f =
                    close_and_rename_wav_file(opts->wav_out_f, opts->wav_out_file, opts->wav_out_dir, event_struct);
                opts->wav_out_f = open_wav_file(opts->wav_out_dir, opts->wav_out_file, 8000, 0);
            }

            else if (slot == 1 && opts->wav_out_fR != NULL) {
                opts->wav_out_fR =
                    close_and_rename_wav_file(opts->wav_out_fR, opts->wav_out_fileR, opts->wav_out_dir, event_struct);
                opts->wav_out_fR = open_wav_file(opts->wav_out_dir, opts->wav_out_fileR, 8000, 0);
            }
        }

        push_event_history(event_struct);
        init_event_history(event_struct, 0, 1);

        //clear out some strings and things
        memset(state->ysf_txt, 0, sizeof(state->ysf_txt));
        memset(state->dstar_gps, 0, sizeof(state->dstar_gps));
        memset(state->dstar_txt, 0, sizeof(state->dstar_txt));
        {
            uint8_t slot_idx = (slot >= 2) ? 1 : slot;
            state->gi[slot_idx] = -1; //return to an unset value
        }

        //end of voice call alert
        if (opts->call_alert == 1) {
            beeper(opts, state, slot, 40, 86, 3);
        }
    }
}

//similar to above, but constantly testing and checking the most recent event only
//this will hopefully be more useful when dealing with an ongoing event with
//features that update over time with embedded signalling, etc
void
watchdog_event_current(dsd_opts* opts, dsd_state* state, uint8_t slot) {

    //create a pointer to the current slot event history
    Event_History_I* event_struct = &state->event_history_s[slot];

    //ncurses color pairs
    uint8_t color_pair = 4; //default voice color

    //TODO: Flesh out more later on.
    uint32_t source_id = 0;
    uint32_t target_id = 0;
    char src_str[200] = {0};
    char tgt_str[200] = {0};

    //group import items
    char t_name[200] = {0};
    char s_name[200] = {0};
    char t_mode[200] = {0};
    char s_mode[200] = {0};

    uint16_t svc_opts = 0;
    uint8_t subtype = 0;
    uint8_t mfid = 0;
    uint32_t sys_id1 = 0;
    uint32_t sys_id2 = 0;
    uint32_t sys_id3 = 0;
    uint32_t sys_id4 = 0;
    uint32_t sys_id5 = 0;

    uint32_t channel = 0;

    uint8_t enc = 0;
    uint8_t alg_id = 0;
    uint16_t key_id = 0;
    unsigned long long int mi;

    char sysid_string[200];
    memset(sysid_string, 0, sizeof(sysid_string));
    snprintf(sysid_string, sizeof sysid_string, "%s", "");

    if (slot == 0) {
        source_id = state->lastsrc;
        target_id = state->lasttg;

        subtype = state->dmrburstL;
        mfid = state->dmr_fid;

        svc_opts = state->dmr_so;
        enc = (svc_opts >> 6) & 1;
        alg_id = state->payload_algid;
        key_id = (uint16_t)state->payload_keyid;

        mi = state->payload_mi;
    } else {
        source_id = state->lastsrcR;
        target_id = state->lasttgR;

        subtype = state->dmrburstR;
        mfid = state->dmr_fidR;

        svc_opts = state->dmr_soR;
        enc = (svc_opts >> 6) & 1;

        alg_id = state->payload_algidR;
        key_id = (uint16_t)state->payload_keyidR;
        mi = state->payload_miR;
    }

    //if P25 (if not P25, then these will all be zero anyways)
    sys_id1 = state->p2_wacn;
    sys_id2 = state->p2_sysid;
    if (state->nac != 0) {
        sys_id3 = state->nac; //same as state->p2_cc, but zeroes out when no signal or error
    } else {
        sys_id3 = state->p2_cc;
    }
    sys_id4 = state->p2_rfssid;
    sys_id5 = state->p2_siteid;

    if (sys_id1) {
        snprintf(sysid_string, sizeof sysid_string, "P25_%05X%03X%03X_%d_%d", sys_id1, sys_id2, sys_id3, sys_id4,
                 sys_id5);
    } else {
        snprintf(sysid_string, sizeof sysid_string, "P25_%03X", sys_id3);
    }

    if (DSD_SYNC_IS_DMR(state->lastsynctype)) {
        sys_id1 = state->dmr_t3_syscode;
        sys_id2 = state->dmr_color_code;

        if (sys_id1) {
            snprintf(sysid_string, sizeof sysid_string, "DMR_%X_CC_%d", sys_id1, sys_id2);
        } else {
            snprintf(sysid_string, sizeof sysid_string, "DMR_CC_%d", sys_id2);
        }
    }

    if (slot == 0) //BUGFIX: generic catch on FDMA systems so that we don't write duplicate data to slot 2 event history
    {
        //NXDN RID (TODO: Changeover to lastsrc and lasttg later on)
        if (DSD_SYNC_IS_NXDN(state->lastsynctype)) {
            source_id = state->nxdn_last_rid;
            target_id = state->nxdn_last_tg;
            if (state->nxdn_cipher_type != 0) {
                enc = 1;
            }
            alg_id = state->nxdn_cipher_type;
            key_id = state->nxdn_key;

            sys_id1 = state->nxdn_location_site_code;
            sys_id2 = state->nxdn_location_sys_code;
            sys_id3 =
                state
                    ->nxdn_last_ran; //might be an issue on conventional systems that have a different RAN on the tx_rel or idle data bursts

            if (sys_id1) {
                snprintf(sysid_string, sizeof sysid_string, "NXDN_%d_%d_RAN_%d", sys_id2, sys_id1, sys_id3);
            } else {
                snprintf(sysid_string, sizeof sysid_string, "NXDN_RAN_%d", sys_id3);
            }
        }

        if (DSD_SYNC_IS_YSF(state->lastsynctype)) //YSF Fusion
        {
            source_id = 0;
            if (strncmp(state->ysf_src, "          ", 10) != 0) //if this field does not have ten spaces in it
            {
                for (uint8_t i = 0; i < 11; i++) {
                    source_id += state->ysf_src[i]; //convert to sum value to make a distinct enough src value
                }
            }

            //WIP: If Text, compile it here (still having issues with an empty txt string making a line break)
            uint8_t k = 0;
            char ysf_emp[21][21];
            memset(ysf_emp, 0, sizeof(ysf_emp));
            if (memcmp(ysf_emp, state->ysf_txt, sizeof(state->ysf_txt)) != 0) {
                for (uint8_t i = 4; i < 8; i++) {
                    for (uint8_t j = 0; j < 20; j++) {
                        if (state->ysf_txt[i][j] != 0x2A) {
                            event_struct->Event_History_Items[0].text_message[k++] = state->ysf_txt[i][j];
                        } else {
                            event_struct->Event_History_Items[0].text_message[k++] = 0x20; //space
                        }
                    }
                    event_struct->Event_History_Items[0].text_message[k] = 0; //terminate
                }
            } else {
                event_struct->Event_History_Items[0].text_message[0] = '\0';
            }

            snprintf(sysid_string, sizeof sysid_string, "%s", "YSF");

            char temp_str[20];
            memset(temp_str, 0, sizeof(temp_str));

            //set src string as a non-spaced non-garbo char string
            for (uint8_t i = 0; i < 10; i++) {
                if (state->ysf_src[i] == 0) {
                    break; // terminator
                } else if (state->ysf_src[i] > 0x20 && state->ysf_src[i] < 0x7F) {
                    temp_str[i] = state->ysf_src[i];
                } else { // spaces and non-ascii
                    temp_str[i] = 0x5F;
                }
            }
            snprintf(src_str, sizeof src_str, "%s", temp_str);

            //same for tgt str
            memset(temp_str, 0, sizeof(temp_str));
            for (uint8_t i = 0; i < 10; i++) {
                if (state->ysf_tgt[i] == 0) {
                    break; // terminator
                } else if (state->ysf_tgt[i] > 0x20 && state->ysf_tgt[i] < 0x7F) {
                    temp_str[i] = state->ysf_tgt[i];
                } else { // spaces and non-ascii
                    temp_str[i] = 0x5F;
                }
            }
            snprintf(tgt_str, sizeof tgt_str, "%s", temp_str);
        }

        if (state->lastsynctype == DSD_SYNC_M17_STR_POS || state->lastsynctype == DSD_SYNC_M17_STR_NEG
            || state->lastsynctype == DSD_SYNC_M17_LSF_POS || state->lastsynctype == DSD_SYNC_M17_LSF_NEG) //M17 STR
        {
            target_id = (uint32_t)state->m17_dst;
            source_id = (uint32_t)state->m17_src;
            sys_id1 = state->m17_can;
            snprintf(sysid_string, sizeof sysid_string, "M17_CAN_%d", sys_id1);
            snprintf(src_str, sizeof src_str, "%s", state->m17_src_csd);
            snprintf(tgt_str, sizeof tgt_str, "%s", state->m17_dst_csd);
        }

        if (DSD_SYNC_IS_DSTAR(state->lastsynctype)) //DSTAR
        {
            source_id = 0;
            for (uint8_t i = 0; i < 12; i++) {
                source_id += state->dstar_src[i]; //convert to sum value to make a distinct enough src value
            }

            //need a strncmp here for 8 spaces in this field first so we don't blip a blank into the event history
            if (strncmp(state->dstar_src, "        ", 8) == 0) {
                source_id = 0;
            }

            snprintf(sysid_string, sizeof sysid_string, "%s", "DSTAR");

            char temp_str[20];
            memset(temp_str, 0, sizeof(temp_str));

            //set src string as a non-spaced non-garbo char string
            for (uint8_t i = 0; i < 12; i++) {
                if (state->dstar_src[i] == 0) {
                    break; // terminator
                } else if (state->dstar_src[i] > 0x20 && state->dstar_src[i] < 0x7F) {
                    temp_str[i] = state->dstar_src[i];
                } else { // spaces and non-ascii
                    temp_str[i] = 0x5F;
                }
            }
            snprintf(src_str, sizeof src_str, "%s", temp_str);

            //same for tgt str
            memset(temp_str, 0, sizeof(temp_str));
            for (uint8_t i = 0; i < 8; i++) {
                if (state->dstar_dst[i] == 0) {
                    break; // terminator
                } else if (state->dstar_dst[i] > 0x20 && state->dstar_dst[i] < 0x7F) {
                    temp_str[i] = state->dstar_dst[i];
                } else { // spaces and non-ascii
                    temp_str[i] = 0x5F;
                }
            }
            snprintf(tgt_str, sizeof tgt_str, "%s", temp_str);
        }

        if (DSD_SYNC_IS_DPMR(state->lastsynctype)) //dPMR
        {
            source_id = 0;
            for (uint8_t i = 0; i < 20; i++) {
                source_id += state->dpmr_caller_id[i]; //convert to sum value to make a distinct enough src value
            }

            //need a strncmp here for 8 spaces in this field first so we don't blip a blank into the event history
            if (strncmp(state->dpmr_caller_id, "      ", 6) == 0) {
                source_id = 0;
            }

            snprintf(sysid_string, sizeof sysid_string, "DPMR_CC_%d", state->dpmr_color_code);

            snprintf(src_str, sizeof src_str, "%s", state->dpmr_caller_id);
            snprintf(tgt_str, sizeof tgt_str, "%s", state->dpmr_target_id);
        }

        if (DSD_SYNC_IS_EDACS(state->lastsynctype)) //EDACS Calls
        {
            source_id = 0;
            if (opts->p25_is_tuned == 1) {
                source_id = state->lastsrc;
                channel = state->edacs_tuned_lcn;
            }

            sys_id1 = state->edacs_site_id;
            sys_id2 = state->edacs_area_code;
            sys_id3 = state->edacs_sys_id;
            svc_opts = state->edacs_vc_call_type;
            char sup_str[200];
            memset(sup_str, 0, sizeof(sup_str));
            snprintf(sup_str, sizeof sup_str, "%s", "_");
            size_t rem;
            if (svc_opts & 0x02) {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "Digital_", rem);
                }
            } else {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "Analog_", rem);
                }
            }
            if (svc_opts & 0x04) {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "Emergency_", rem);
                }
            }
            if (svc_opts & 0x08) {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "Group_", rem);
                }
            }
            if (svc_opts & 0x10) {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "I_", rem);
                }
            }
            if (svc_opts & 0x20) {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "ALL_", rem);
                }
            }
            if (svc_opts & 0x40) {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "INTER_", rem);
                }
            }
            if (svc_opts & 0x80) {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "TEST_", rem);
                }
            }
            if (svc_opts & 0x100) {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "AGENCY_", rem);
                }
            }
            if (svc_opts & 0x200) {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "FLEET_", rem);
                }
            }
            if (svc_opts & 0x01) {
                rem = sizeof(sup_str) - strlen(sup_str) - 1;
                if (rem > 0) {
                    strncat(sup_str, "Voice_", rem);
                }
            }
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "Call", rem);
            }

            snprintf(sysid_string, sizeof sysid_string, "EDACS_SITE_%03u", (unsigned)sys_id1);
            sysid_string[sizeof sysid_string - 1] = '\0';
            {
                size_t used = strlen(sysid_string);
                if (used >= sizeof(sysid_string)) {
                    used = sizeof(sysid_string) - 1;
                }
                size_t rem2 = sizeof(sysid_string) - used - 1;
                if (rem2 > 0) {
                    size_t sup_len = strlen(sup_str);
                    if (sup_len > rem2) {
                        sup_len = rem2;
                    }
                    memcpy(sysid_string + used, sup_str, sup_len);
                    sysid_string[used + sup_len] = '\0';
                }
            }

            if (state->ea_mode == 0) {
                int afs = state->lasttg;
                snprintf(src_str, sizeof src_str, "%s", "");
                snprintf(tgt_str, sizeof tgt_str, "%s", "");
                int a = (afs >> state->edacs_a_shift) & state->edacs_a_mask;
                int f = (afs >> state->edacs_f_shift) & state->edacs_f_mask;
                int s = afs & state->edacs_s_mask;
                snprintf(tgt_str, sizeof tgt_str, "%03d_AFS_%02d_%02d%01d", afs, a, f, s);
                if (state->lastsrc != 0x800 && state->lastsrc != 0) {
                    snprintf(src_str, sizeof src_str, "LID_%d", state->lastsrc);
                } else {
                    snprintf(src_str, sizeof src_str, "LID_UNK");
                }
            }
        }
    }

    //if we have a group_array import, search and load it here
    //will search and load both target values, and src values if available
    uint8_t t_name_loaded = 0;
    uint8_t s_name_loaded = 0;
    if (target_id != 0) {
        for (unsigned int i = 0; i < state->group_tally; i++) {
            if (state->group_array[i].groupNumber == target_id) {
                sprintf(t_name, "%s", state->group_array[i].groupName);
                sprintf(t_mode, "%s", state->group_array[i].groupMode);
                t_name_loaded = 1;
                break;
            }
        }
    }

    if (source_id != 0) //&& state->gi[slot] == 1
    {
        for (unsigned int i = 0; i < state->group_tally; i++) {
            if (state->group_array[i].groupNumber == source_id) {
                sprintf(s_name, "%s", state->group_array[i].groupName);
                sprintf(s_mode, "%s", state->group_array[i].groupMode);
                s_name_loaded = 1;
                break;
            }
        }
    }

    //system type string (P25, DMR, etc)
    const char* sys_string = dsd_synctype_to_string(state->lastsynctype);

    //date and time strings
    char timestr[9];
    char datestr[11];
    getTimeN_buf(time(NULL), timestr);
    getDateN_buf(time(NULL), datestr);

    if (source_id != 0) {
        event_struct->Event_History_Items[0].write = 0;
        state->event_history_s[slot].Event_History_Items[0].color_pair = color_pair;
        if (state->lastsynctype != DSD_SYNC_NONE) {
            event_struct->Event_History_Items[0].systype = state->lastsynctype;
        } else {
            event_struct->Event_History_Items[0].systype = 39; //generic digital call
        }
        event_struct->Event_History_Items[0].subtype = subtype;    //voice
        event_struct->Event_History_Items[0].gi = state->gi[slot]; //need this add this to link control messages
        event_struct->Event_History_Items[0].sys_id1 = sys_id1;
        event_struct->Event_History_Items[0].sys_id2 = sys_id2;
        event_struct->Event_History_Items[0].sys_id3 = sys_id3;
        event_struct->Event_History_Items[0].sys_id4 = sys_id4;
        event_struct->Event_History_Items[0].sys_id5 = sys_id5;
        event_struct->Event_History_Items[0].enc = enc;
        event_struct->Event_History_Items[0].enc_alg = alg_id;
        event_struct->Event_History_Items[0].enc_key = key_id;
        event_struct->Event_History_Items[0].mi = mi;
        event_struct->Event_History_Items[0].svc = svc_opts;
        event_struct->Event_History_Items[0].source_id = source_id;
        event_struct->Event_History_Items[0].target_id = target_id;
        event_struct->Event_History_Items[0].channel =
            channel;                //need to add this to trunking messages, if tuned from call grant
        if (opts->playfiles == 0) { //if playing back .mbe files with a time in it, don't set this
            event_struct->Event_History_Items[0].event_time = time(NULL);
        }
        snprintf(event_struct->Event_History_Items[0].sysid_string,
                 sizeof event_struct->Event_History_Items[0].sysid_string, "%s", sysid_string);
        snprintf(event_struct->Event_History_Items[0].src_str, sizeof event_struct->Event_History_Items[0].src_str,
                 "%s", src_str);
        snprintf(event_struct->Event_History_Items[0].tgt_str, sizeof event_struct->Event_History_Items[0].tgt_str,
                 "%s", tgt_str);

        snprintf(event_struct->Event_History_Items[0].t_name, sizeof event_struct->Event_History_Items[0].t_name, "%s",
                 t_name);
        snprintf(event_struct->Event_History_Items[0].s_name, sizeof event_struct->Event_History_Items[0].s_name, "%s",
                 s_name);
        snprintf(event_struct->Event_History_Items[0].t_mode, sizeof event_struct->Event_History_Items[0].t_mode, "%s",
                 t_mode);
        snprintf(event_struct->Event_History_Items[0].s_mode, sizeof event_struct->Event_History_Items[0].s_mode, "%s",
                 s_mode);
    }

    //Craft an event string for ncurses event history, and a more complex string for logging
    char event_string[2000];
    memset(event_string, 0, sizeof(event_string));

    //WIP: Seperate Voice Call Event Strings when SRC/TGT values are numerical,
    //and a seperate one for when they are string values (M17, YSF, DSTAR, and dPMR, or use special formatting)
    if (DSD_SYNC_IS_YSF(state->lastsynctype)) //YSF Fusion //TODO: Data calls dumping a lot of events as VOICE
    {
        //TODO: See if we can add some decoded data as well in the future to an event string
        snprintf(event_string, sizeof event_string, "%s %s %s TGT: %s SRC: %s ", datestr, timestr, sys_string,
                 state->ysf_tgt, state->ysf_src);
    } else if (state->lastsynctype == DSD_SYNC_M17_LSF_POS || state->lastsynctype == DSD_SYNC_M17_LSF_NEG) //M17
    {
        //TODO: See if we can add some decoded data as well in the future to an event string
        if (state->m17_dst == 0xFFFFFFFFFFFF) {
            snprintf(event_string, sizeof event_string, "%s %s %s TGT: %s SRC: %s CAN: %02d;", datestr, timestr,
                     sys_string, "BROADCAST", state->m17_src_str, state->m17_can);
        } else {
            snprintf(event_string, sizeof event_string, "%s %s %s TGT: %s SRC: %s CAN: %02d;", datestr, timestr,
                     sys_string, state->m17_dst_str, state->m17_src_str, state->m17_can);
        }
    } else if (DSD_SYNC_IS_DSTAR(state->lastsynctype)) //DSTAR
    {
        //TODO: See if we can add some decoded data as well in the future to an event string
        snprintf(event_string, sizeof event_string, "%s %s %s TGT: %s SRC: %s ", datestr, timestr, sys_string,
                 state->dstar_dst, state->dstar_src);
    } else if (DSD_SYNC_IS_DPMR(state->lastsynctype)) //dPMR
    {
        //TODO: See if we can add some decoded data as well in the future to an event string
        snprintf(event_string, sizeof event_string, "%s %s %s CC: %02d; TGT: %s; SRC: %s; ", datestr, timestr,
                 sys_string, state->dpmr_color_code, state->dpmr_target_id, state->dpmr_caller_id);
        if (state->dPMRVoiceFS2Frame.Version[0] == 3) {
            size_t rem = sizeof(event_string) - strlen(event_string) - 1;
            if (rem > 0) {
                strncat(event_string, "Scrambler Enc; ", rem);
            }
        }
    } else if (DSD_SYNC_IS_EDACS(state->lastsynctype)) //EDACS Calls
    {
        svc_opts = state->edacs_vc_call_type;
        char sup_str[200];
        memset(sup_str, 0, sizeof(sup_str));
        sprintf(sup_str, "%s", "");
        size_t rem;
        if (svc_opts & 0x02) {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "Digital ", rem);
            }
        } else {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "Analog ", rem);
            }
        }
        if (svc_opts & 0x04) {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "Emergency ", rem);
            }
        }
        if (svc_opts & 0x08) {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "Group ", rem);
            }
        }
        if (svc_opts & 0x10) {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "I ", rem);
            }
        }
        if (svc_opts & 0x20) {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "ALL ", rem);
            }
        }
        if (svc_opts & 0x40) {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "INTER ", rem);
            }
        }
        if (svc_opts & 0x80) {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "TEST ", rem);
            }
        }
        if (svc_opts & 0x100) {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "AGENCY ", rem);
            }
        }
        if (svc_opts & 0x200) {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "FLEET ", rem);
            }
        }
        if (svc_opts & 0x01) {
            rem = sizeof(sup_str) - strlen(sup_str) - 1;
            if (rem > 0) {
                strncat(sup_str, "Voice ", rem);
            }
        }
        rem = sizeof(sup_str) - strlen(sup_str) - 1;
        if (rem > 0) {
            strncat(sup_str, "Call", rem);
        }

        if (state->ea_mode == 1) {
            sprintf(event_string, "%s %s %s TGT: %07d; SRC: %07d; LCN: %02d; SITE: %d:%d.%04X; %s;", datestr, timestr,
                    sys_string, target_id, source_id, channel, sys_id1, sys_id2, sys_id3, sup_str);
        } else {
            int afs = state->lasttg;
            int a = (afs >> state->edacs_a_shift) & state->edacs_a_mask;
            int f = (afs >> state->edacs_f_shift) & state->edacs_f_mask;
            int s = afs & state->edacs_s_mask;
            char afs_str[8];
            getAfsString(state, afs_str, a, f, s);
            char lid_str[20];
            memset(lid_str, 0, sizeof(lid_str));
            sprintf(lid_str, "%s", "");
            if (state->lastsrc != 0 && state->lastsrc != 0x800) {
                sprintf(lid_str, "LID: %05d;", state->lastsrc);
            } else {
                sprintf(lid_str, "LID: __UNK;");
            }

            sprintf(event_string, "%s %s %s AFS: %s (%04d); %s LCN: %02d; Site: %d; %s; ", datestr, timestr, sys_string,
                    afs_str, afs, lid_str, channel, sys_id1, sup_str);
        }
    } else if (DSD_SYNC_IS_DMR(state->lastsynctype)) //DMR
    {
        if (sys_id1) {
            sprintf(event_string, "%s %s %s TGT: %08d; SRC: %08d; CC: %02d; SYS: %X; ", datestr, timestr, sys_string,
                    target_id, source_id, sys_id2, sys_id1);
        } else {
            sprintf(event_string, "%s %s %s TGT: %08d; SRC: %08d; CC: %02d; ", datestr, timestr, sys_string, target_id,
                    source_id, sys_id2);
        }
        if (enc) {
            size_t rem = sizeof(event_string) - strlen(event_string) - 1;
            if (rem > 0) {
                strncat(event_string, "ENC; ", rem);
            }
        }
        if (alg_id != 0) {
            char ess_str[30];
            sprintf(ess_str, "ALG: %02X; KID: %02X; ", alg_id, key_id);
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, ess_str, rem);
                }
            }
        }

        //monitor for misc link control that may set a SO without having SO inside of it,
        //those could cause misc issues here, will need to observe and make adjustments
        if (svc_opts & 0x80) {
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, "Emergency; ", rem);
                }
            }
        }

        if (svc_opts & 0x08) {
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, "Broadcast; ", rem);
                }
            }
        }

        if (svc_opts & 0x04) {
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, "OVCM; ", rem);
                }
            }
        }

        if (state->gi[slot] == 0) {
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, "Group; ", rem);
                }
            }
        } else if (state->gi[slot] == 1) {
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, "Private; ", rem);
                }
            }
        }

        if (mfid == 0x10) {
            if (svc_opts & 0x20) {
                {
                    size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                    if (rem > 0) {
                        strncat(event_string, "TXI; ", rem);
                    }
                }
            } else if (svc_opts & 0x10) {
                {
                    size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                    if (rem > 0) {
                        strncat(event_string, "TXI; ", rem);
                    }
                }
            }

            if (svc_opts & 0x03) {
                {
                    size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                    if (rem > 0) {
                        strncat(event_string, "PRIORITY; ", rem);
                    }
                }
            }
        }

    } else if (DSD_SYNC_IS_P25(state->lastsynctype)) {
        if (sys_id1) {
            sprintf(event_string, "%s %s %s TGT: %08d; SRC: %08d; NAC: %03X; NET_STS: %05X:%03X:%d.%d; ", datestr,
                    timestr, sys_string, target_id, source_id, sys_id3, sys_id1, sys_id2, sys_id4, sys_id5);
        } else {
            sprintf(event_string, "%s %s %s TGT: %08d; SRC: %08d; NAC: %03X; ", datestr, timestr, sys_string, target_id,
                    source_id, sys_id3);
        }
        if (alg_id != 0 && alg_id != 0x80) {
            char ess_str[30];
            sprintf(ess_str, "ENC; ALG: %02X; KID: %04X; ", alg_id, key_id);
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, ess_str, rem);
                }
            }
        }
        if (svc_opts & 0x80) {
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, "Emergency; ", rem);
                }
            }
        }
        if (state->gi[slot] == 0) {
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, "Group; ", rem);
                }
            }
        } else if (state->gi[slot] == 1) {
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, "Private; ", rem);
                }
            }
        }
    }

    else if (DSD_SYNC_IS_NXDN(state->lastsynctype)) {
        if (sys_id1) {
            snprintf(event_string, sizeof event_string, "%s %s %s TGT: %08d; SRC: %08d; RAN: %02d; SYS: %d.%d; ",
                     datestr, timestr, sys_string, target_id, source_id, sys_id3, sys_id2, sys_id1);
        } else {
            snprintf(event_string, sizeof event_string, "%s %s %s TGT: %08d; SRC: %08d; RAN: %02d; ", datestr, timestr,
                     sys_string, target_id, source_id, sys_id3);
        }
        if (state->nxdn_grant_chan != 0) {
            char ch_str[96];
            if (state->nxdn_grant_freq != 0) {
                snprintf(ch_str, sizeof ch_str, "CH: %u; FREQ: %.6lf MHz; ", state->nxdn_grant_chan,
                         (double)state->nxdn_grant_freq / 1000000.0);
            } else {
                snprintf(ch_str, sizeof ch_str, "CH: %u; ", state->nxdn_grant_chan);
            }
            size_t rem = sizeof(event_string) - strlen(event_string) - 1;
            if (rem > 0) {
                strncat(event_string, ch_str, rem);
            }
        }
        if (enc) {
            size_t rem = sizeof(event_string) - strlen(event_string) - 1;
            if (rem > 0) {
                strncat(event_string, "ENC; ", rem);
            }
        }
        if (alg_id != 0) {
            char ess_str[30];
            snprintf(ess_str, sizeof ess_str, "ALG: %d; KID: %02X; ", alg_id, key_id);
            {
                size_t rem = sizeof(event_string) - strlen(event_string) - 1;
                if (rem > 0) {
                    strncat(event_string, ess_str, rem);
                }
            }
        }
        if (state->gi[slot] == 0) {
            size_t rem = sizeof(event_string) - strlen(event_string) - 1;
            if (rem > 0) {
                strncat(event_string, "Group; ", rem);
            }
        } else if (state->gi[slot] == 1) {
            size_t rem = sizeof(event_string) - strlen(event_string) - 1;
            if (rem > 0) {
                strncat(event_string, "Private; ", rem);
            }
        }
    }

    if (t_name_loaded) {
        char group[420];
        snprintf(group, sizeof group, "TName: %s; Mode: %s; ", t_name, t_mode);
        {
            size_t rem = sizeof(event_string) - strlen(event_string) - 1;
            if (rem > 0) {
                strncat(event_string, group, rem);
            }
        }
    }
    if (s_name_loaded) {
        char private[420];
        snprintf(private, sizeof private, "SName: %s; Mode: %s; ", s_name, s_mode);
        {
            size_t rem = sizeof(event_string) - strlen(event_string) - 1;
            if (rem > 0) {
                strncat(event_string, private, rem);
            }
        }
    }

    snprintf(event_struct->Event_History_Items[0].event_string,
             sizeof event_struct->Event_History_Items[0].event_string, "%s", event_string);

    /* stack buffers; no free */
}

void
watchdog_event_datacall(dsd_opts* opts, dsd_state* state, uint32_t src, uint32_t dst, char* data_string, uint8_t slot) {
    UNUSED(opts);
    state->event_history_s[slot].Event_History_Items[0].write = 0;
    if (state->event_history_s[slot].Event_History_Items[0].color_pair
        == 4) { //if not set previously by specific decoder //don't touch this one
        state->event_history_s[slot].Event_History_Items[0].color_pair =
            4; //default data color //you can change this one
    }
    state->event_history_s[slot].Event_History_Items[0].systype = state->lastsynctype;
    state->event_history_s[slot].Event_History_Items[0].subtype = 6; //data
    state->event_history_s[slot].Event_History_Items[0].gi = state->gi[slot];
    state->event_history_s[slot].Event_History_Items[0].enc = 0;
    state->event_history_s[slot].Event_History_Items[0].enc_alg = 0;
    state->event_history_s[slot].Event_History_Items[0].enc_key = 0;
    state->event_history_s[slot].Event_History_Items[0].mi = 0;
    state->event_history_s[slot].Event_History_Items[0].svc = 0;
    state->event_history_s[slot].Event_History_Items[0].source_id = src;
    state->event_history_s[slot].Event_History_Items[0].target_id = dst;
    state->event_history_s[slot].Event_History_Items[0].channel = 0;
    state->event_history_s[slot].Event_History_Items[0].event_time = time(NULL);

    //date and time strings //getTimeN(time(NULL)); //getDateN(time(NULL));
    char timestr[9];
    char datestr[11];
    getTimeN_buf(time(NULL), timestr);
    getDateN_buf(time(NULL), datestr);

    char event_string[2000];
    memset(event_string, 0, sizeof(event_string));
    snprintf(event_string, sizeof event_string, "%s %s ", datestr, timestr);
    {
        size_t rem = sizeof(event_string) - strlen(event_string) - 1;
        if (rem > 0) {
            strncat(event_string, data_string, rem);
        }
    }
    snprintf(state->event_history_s[slot].Event_History_Items[0].event_string,
             sizeof state->event_history_s[slot].Event_History_Items[0].event_string, "%s",
             event_string); // could change this to a strncpy to prevent potential overflow

    /* stack buffers; no free */

    //call alert on data calls
    if (opts->call_alert) {
        beeper(opts, state, slot, 80, 20, 3);
    }
}
