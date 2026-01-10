// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*
 * Copyright (C) 2010 DSD Author
 * GPG Key ID: 0x3F1D7FD0 (74EF 430D F7F2 0A48 FCE6  F630 FAA2 635D 3F1D 7FD0)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/bit_packing.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/keyring.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/crypto/rc4.h>
#include <dsd-neo/protocol/dmr/dmr_const.h>   //for ambe+2 fr
#include <dsd-neo/protocol/p25/p25p1_const.h> //for imbe fr (7200)
#include <dsd-neo/runtime/exitflag.h>
#include <dsd-neo/runtime/log.h>

#include <mbelib.h>
#include <sndfile.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void
saveImbe4400Data(dsd_opts* opts, dsd_state* state, char* imbe_d) {
    int i, j, k;
    unsigned char b;
    unsigned char err;

    err = (unsigned char)state->errs2;
    fputc(err, opts->mbe_out_f);

    k = 0;
    for (i = 0; i < 11; i++) {
        b = 0;

        for (j = 0; j < 8; j++) {
            b = b << 1;
            b = b + imbe_d[k];
            k++;
        }
        fputc(b, opts->mbe_out_f);
    }
}

void
saveAmbe2450Data(dsd_opts* opts, dsd_state* state, char* ambe_d) {
    int i, j, k;
    unsigned char b;
    unsigned char err;

    err = (unsigned char)state->errs2;
    fputc(err, opts->mbe_out_f);

    k = 0;
    for (i = 0; i < 6; i++) {
        b = 0;
        for (j = 0; j < 8; j++) {
            b = b << 1;
            b = b + ambe_d[k];
            k++;
        }
        fputc(b, opts->mbe_out_f);
    }
    b = ambe_d[48];
    fputc(b, opts->mbe_out_f);
}

void
saveAmbe2450DataR(dsd_opts* opts, dsd_state* state, char* ambe_d) {
    int i, j, k;
    unsigned char b;
    unsigned char err;

    err = (unsigned char)state->errs2R;
    fputc(err, opts->mbe_out_fR);

    k = 0;
    for (i = 0; i < 6; i++) {
        b = 0;
        for (j = 0; j < 8; j++) {
            b = b << 1;
            b = b + ambe_d[k];
            k++;
        }
        fputc(b, opts->mbe_out_fR);
    }
    b = ambe_d[48];
    fputc(b, opts->mbe_out_fR);
}

void
PrintIMBEData(dsd_opts* opts, dsd_state* state, char* imbe_d) //for P25P1 and ProVoice
{
    fprintf(stderr, "\n IMBE ");
    uint8_t imbe[88];
    for (int i = 0; i < 11; i++) {
        imbe[i] = convert_bits_into_output((uint8_t*)imbe_d + ((size_t)i * 8u), 8);
        fprintf(stderr, "%02X", imbe[i]);
    }

    fprintf(stderr, " err = [%X] [%X] ", state->errs, state->errs2);
    UNUSED(opts);
}

void
PrintAMBEData(dsd_opts* opts, dsd_state* state, char* ambe_d) {

    //cast as unsigned long long int and not uint64_t
    //to avoid the %lx vs %llx warning on 32 or 64 bit
    unsigned long long int ambe = 0;

    //preceeding line break, if required
    if (opts->dmr_stereo == 0 && opts->dmr_mono == 0) {
        fprintf(stderr, "\n");
    }

    ambe = convert_bits_into_output((uint8_t*)ambe_d, 49);
    ambe = ambe << 7; //shift to final position

    fprintf(stderr, " AMBE %014llX", ambe);

    if (state->currentslot == 0) {
        fprintf(stderr, " err = [%X] [%X] ", state->errs, state->errs2);
    } else {
        fprintf(stderr, " err = [%X] [%X] ", state->errsR, state->errs2R);
    }

    //trailing line break, if required
    if (opts->dmr_stereo == 1 || opts->dmr_mono == 1) {
        fprintf(stderr, "\n");
    }
}

int
readImbe4400Data(dsd_opts* opts, dsd_state* state, char* imbe_d) {

    int i, j, k;
    unsigned char b, x;

    state->errs2 = fgetc(opts->mbe_in_f);
    state->errs = state->errs2;

    k = 0;
    if (opts->payload == 1) {
        fprintf(stderr, "\n IMBE ");
    }
    x = 0;
    for (i = 0; i < 11; i++) {
        b = fgetc(opts->mbe_in_f);
        if (feof(opts->mbe_in_f)) {
            return (1);
        }
        for (j = 0; j < 8; j++) {
            imbe_d[k] = (b & 128) >> 7;

            x = x << 1;
            x |= ((b & 0x80) >> 7);

            b = b << 1;
            b = b & 255;
            k++;
        }

        if (opts->payload == 1) {
            fprintf(stderr, "%02X", x);
        }
    }
    if (opts->payload == 1) {
        fprintf(stderr, " err = [%X] [%X] ", state->errs, state->errs2); //not sure that errs here are legit values
    }
    return (0);
}

int
readAmbe2450Data(dsd_opts* opts, dsd_state* state, char* ambe_d) {

    int i, j, k;
    unsigned char b, x;

    state->errs2 = fgetc(opts->mbe_in_f);
    state->errs = state->errs2;

    k = 0;
    if (opts->payload == 1) {
        fprintf(stderr, "\n AMBE ");
    }

    x = 0;
    for (i = 0; i < 6; i++) //breaks backwards compatablilty with 6 files
    {
        b = fgetc(opts->mbe_in_f);
        if (feof(opts->mbe_in_f)) {
            return (1);
        }
        for (j = 0; j < 8; j++) {
            ambe_d[k] = (b & 128) >> 7;

            x = x << 1;
            x |= ((b & 0x80) >> 7);

            b = b << 1;
            b = b & 255;
            k++;
        }
        if (opts->payload == 1 && i < 6) {
            fprintf(stderr, "%02X", x);
        }
        if (opts->payload == 1 && i == 6) {
            fprintf(stderr, "%02X", x & 0x80);
        }
    }
    if (opts->payload == 1) {
        fprintf(stderr, " err = [%X] [%X] ", state->errs, state->errs2);
    }
    b = fgetc(opts->mbe_in_f);
    ambe_d[48] = (b & 1);

    return (0);
}

void
openMbeInFile(dsd_opts* opts, dsd_state* state) {

    char cookie[5];

    opts->mbe_in_f = fopen(opts->mbe_in_file, "ro");
    if (opts->mbe_in_f == NULL) {
        LOG_ERROR("Error: could not open %s\n", opts->mbe_in_file);
    }

    //this will check the last 4 characters of the opts->mbe_in_file string
    char ext[5];
    memset(ext, 0, sizeof(ext));
    uint16_t str_len = strlen((const char*)opts->mbe_in_file);
    uint16_t ext_ptr = str_len - 4;
    strncpy(ext, opts->mbe_in_file + ext_ptr, 4);

    //debug
    // fprintf (stderr, "EXT: %s;", ext);

    // read cookie
    cookie[0] = fgetc(opts->mbe_in_f);
    cookie[1] = fgetc(opts->mbe_in_f);
    cookie[2] = fgetc(opts->mbe_in_f);
    cookie[3] = fgetc(opts->mbe_in_f);
    cookie[4] = 0;

    //ambe+2
    if (strstr(cookie, ".amb") != NULL) {
        state->mbe_file_type = 1;
    }
    //p1 and pv
    else if (strstr(cookie, ".imb") != NULL) {
        state->mbe_file_type = 0;
    }
    //d-star ambe
    else if (strstr(cookie, ".dmb") != NULL) {
        state->mbe_file_type = 2;
    }
    //sdrtrunk formated mbe json file
    else if (strncmp(".mbe", ext, 4) == 0) {
        state->mbe_file_type = 3;
    } else {
        state->mbe_file_type = -1;
        LOG_ERROR("Error - unrecognized file type\n");

        //try SDRTrunk JSON format as last resort
        state->mbe_file_type = 3;
    }
}

//slot 1
void
closeMbeOutFile(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    if (opts->mbe_out == 1) {
        if (opts->mbe_out_f != NULL) {
            fflush(opts->mbe_out_f);
            fclose(opts->mbe_out_f);
            opts->mbe_out_f = NULL;
            opts->mbe_out = 0;
            LOG_NOTICE("\nClosing MBE out file 1.\n");
        }
    }
}

//slot 2
void
closeMbeOutFileR(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    if (opts->mbe_outR == 1) {
        if (opts->mbe_out_fR != NULL) {
            fflush(opts->mbe_out_fR);
            fclose(opts->mbe_out_fR);
            opts->mbe_out_fR = NULL;
            opts->mbe_outR = 0;
            LOG_NOTICE("\nClosing MBE out file 2.\n");
        }
    }
}

void
openMbeOutFile(dsd_opts* opts, dsd_state* state) {

    int i, j;
    char ext[5];
    char timestr[7]; //stack buffer for time string
    char datestr[9]; //stack buffer for date string

    //random element of filename, so two files won't overwrite one another
    uint16_t random_number = rand() & 0xFFFF;

    getTime_buf(timestr);
    getDate_buf(datestr);

    //phase 1 and provoice
    if (DSD_SYNC_IS_P25P1(state->synctype) || DSD_SYNC_IS_PROVOICE(state->synctype)) {
        snprintf(ext, sizeof ext, "%s", ".imb");
    }
    //d-star
    else if (DSD_SYNC_IS_DSTAR(state->synctype)) {
        snprintf(ext, sizeof ext, "%s", ".dmb"); //new dstar file extension to make it read in and process properly
    }
    //dmr, nxdn, phase 2, x2-tdma
    else {
        snprintf(ext, sizeof ext, "%s", ".amb");
    }

    //reset talkgroup id buffer
    for (i = 0; i < 12; i++) {
        for (j = 0; j < 25; j++) {
            state->tg[j][i] = 0;
        }
    }

    state->tgcount = 0;

    snprintf(opts->mbe_out_file, sizeof opts->mbe_out_file, "%s_%s_%04X_S1%s", datestr, timestr, random_number, ext);

    snprintf(opts->mbe_out_path, sizeof opts->mbe_out_path, "%s%s", opts->mbe_out_dir, opts->mbe_out_file);

    opts->mbe_out_f = fopen(opts->mbe_out_path, "w");
    if (opts->mbe_out_f == NULL) {
        LOG_ERROR("\nError, couldn't open %s for slot 1\n", opts->mbe_out_path);
    } else {
        opts->mbe_out = 1;
        /* Fully buffered output to reduce syscall overhead */
        setvbuf(opts->mbe_out_f, NULL, _IOFBF, (size_t)64u * 1024u);
    }

    fprintf(opts->mbe_out_f, "%s", ext);
    /* header write will be flushed later on close */
    /* stack buffers; no free */
}

void
openMbeOutFileR(dsd_opts* opts, dsd_state* state) {

    int i, j;
    char ext[5];
    char timestr[7]; //stack buffer for time string
    char datestr[9]; //stack buffer for date string

    //random element of filename, so two files won't overwrite one another
    uint16_t random_number = rand() & 0xFFFF;

    getTime_buf(timestr);
    getDate_buf(datestr);

    //phase 1 and provoice
    if (DSD_SYNC_IS_P25P1(state->synctype) || DSD_SYNC_IS_PROVOICE(state->synctype)) {
        snprintf(ext, sizeof ext, "%s", ".imb");
    }
    //d-star
    else if (DSD_SYNC_IS_DSTAR(state->synctype)) {
        snprintf(ext, sizeof ext, "%s", ".dmb"); //new dstar file extension to make it read in and process properly
    }
    //dmr, nxdn, phase 2, x2-tdma
    else {
        snprintf(ext, sizeof ext, "%s", ".amb");
    }

    //reset talkgroup id buffer
    for (i = 0; i < 12; i++) {
        for (j = 0; j < 25; j++) {
            state->tg[j][i] = 0;
        }
    }

    state->tgcount = 0;

    snprintf(opts->mbe_out_fileR, sizeof opts->mbe_out_fileR, "%s_%s_%04X_S2%s", datestr, timestr, random_number, ext);

    snprintf(opts->mbe_out_path, sizeof opts->mbe_out_path, "%s%s", opts->mbe_out_dir, opts->mbe_out_fileR);

    opts->mbe_out_fR = fopen(opts->mbe_out_path, "w");
    if (opts->mbe_out_fR == NULL) {
        LOG_ERROR("\nError, couldn't open %s for slot 2\n", opts->mbe_out_path);
    } else {
        opts->mbe_outR = 1;
        /* Fully buffered output to reduce syscall overhead */
        setvbuf(opts->mbe_out_fR, NULL, _IOFBF, (size_t)64u * 1024u);
    }

    fprintf(opts->mbe_out_fR, "%s", ext);
    /* header write will be flushed later on close */
    /* stack buffers; no free */
}

//temp filename should not have the .wav extension, will be renamed with one after event is closed
SNDFILE*
open_wav_file(char* dir, char* temp_filename, uint16_t sample_rate, uint8_t ext) {
    uint16_t random_number = rand();
    char datestr[9];
    char timestr[7];
    getDate_buf(datestr);
    getTime_buf(timestr);

    if (ext == 0) {
        sprintf(temp_filename, "%s/TEMP_%s_%s_%04X", dir, datestr, timestr, random_number);
    } else {
        sprintf(temp_filename, "%s/TEMP_%s_%s_%04X.wav", dir, datestr, timestr, random_number);
    }

    /* stack buffers; no free */

    SNDFILE* wav;
    SF_INFO info;
    info.samplerate = sample_rate;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
    wav = sf_open(temp_filename, SFM_RDWR, &info); //RDWR will append to file instead of overwrite file

    if (wav == NULL) {
        LOG_ERROR("Error - could not open wav output file %s\n", temp_filename);
        return NULL;
    }

    return wav;
}

SNDFILE*
close_wav_file(SNDFILE* wav_file) {
    sf_close(wav_file);
    wav_file = NULL;
    return wav_file;
}

SNDFILE*
close_and_rename_wav_file(SNDFILE* wav_file, char* wav_out_filename, char* dir, Event_History_I* event_struct) {
    if (wav_file != NULL) {
        sf_close(wav_file);
    }

    if (wav_out_filename == NULL || wav_out_filename[0] == '\0') {
        return NULL;
    }

    time_t event_time = event_struct->Event_History_Items[0].event_time;
    char datestr[9];
    char timestr[7];
    getDateF_buf(event_time, datestr);
    getTimeF_buf(event_time, timestr);
    uint16_t random_number = rand();

    uint32_t source_id = event_struct->Event_History_Items[0].source_id;
    uint32_t target_id = event_struct->Event_History_Items[0].target_id;
    int8_t gi = event_struct->Event_History_Items[0].gi;

    char sys_str[200];
    memset(sys_str, 0, sizeof(sys_str));
    char src_str[200];
    memset(src_str, 0, sizeof(src_str));
    char tgt_str[200];
    memset(tgt_str, 0, sizeof(tgt_str));
    char gi_str[10];
    memset(gi_str, 0, sizeof(gi_str));

    snprintf(sys_str, sizeof(sys_str), "%s", event_struct->Event_History_Items[0].sysid_string);
    snprintf(src_str, sizeof(src_str), "%s", event_struct->Event_History_Items[0].src_str);
    snprintf(tgt_str, sizeof(tgt_str), "%s", event_struct->Event_History_Items[0].tgt_str);

    snprintf(gi_str, sizeof(gi_str), "%s", "");
    if (gi == 0) {
        snprintf(gi_str, sizeof(gi_str), "%s", "GROUP");
    } else if (gi == 1) {
        snprintf(gi_str, sizeof(gi_str), "%s", "PRIVATE");
    }

    uint8_t is_string = (src_str[0] != '\0') ? 1 : 0;

    // Prepare final filename now, but only rename if file is not empty header-only (44 bytes)
    char new_filename[2000];
    memset(new_filename, 0, sizeof(new_filename));

    if (is_string == 1) {
        snprintf(new_filename, sizeof(new_filename), "%s/%s_%s_%05d_%s_%s_TGT_%s_SRC_%s.wav", dir, datestr, timestr,
                 random_number, sys_str, gi_str, tgt_str, src_str);
    } else { //is a numerical value
        snprintf(new_filename, sizeof(new_filename), "%s/%s_%s_%05d_%s_%s_TGT_%d_SRC_%d.wav", dir, datestr, timestr,
                 random_number, sys_str, gi_str, target_id, source_id);
    }

    /* stack buffers; no free */

    // Check size of temp file before renaming; delete if only header (44 bytes)
    FILE* file = fopen(wav_out_filename, "r");
    if (file != NULL) {
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET); // Rewind to beginning
        fclose(file);

        if (size == 44) {
            // Remove the temp file and do not expose a .wav to watchers
            remove(wav_out_filename);
            wav_file = NULL;
            return wav_file;
        }
    }

    // Safe to rename now
    rename(wav_out_filename, new_filename);

    // Optional: recheck final file size and remove if header-only, though we already checked
    file = fopen(new_filename, "r");
    if (file != NULL) {
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        fclose(file);
        if (size == 44) {
            remove(new_filename);
        }
    }

    wav_file = NULL;
    return wav_file;
}

SNDFILE*
close_and_delete_wav_file(SNDFILE* wav_file, char* wav_out_filename) {
    sf_close(wav_file);
    wav_file = NULL;
    remove(wav_out_filename);
    return wav_file;
}

void
openWavOutFile(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    SF_INFO info;
    info.samplerate = 8000; //8000
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
    opts->wav_out_f = sf_open(opts->wav_out_file, SFM_RDWR, &info); //RDWR will append to file instead of overwrite file

    if (opts->wav_out_f == NULL) {
        LOG_ERROR("Error - could not open wav output file %s\n", opts->wav_out_file);
        return;
    }
}

void
openWavOutFileL(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    SF_INFO info;
    info.samplerate = 8000;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
    opts->wav_out_f = sf_open(opts->wav_out_file, SFM_RDWR, &info); //RDWR will append to file instead of overwrite file

    if (opts->wav_out_f == NULL) {
        LOG_ERROR("Error - could not open wav output file %s\n", opts->wav_out_file);
        return;
    }
}

void
openWavOutFileR(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    SF_INFO info;
    info.samplerate = 8000; //8000
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
    opts->wav_out_fR =
        sf_open(opts->wav_out_fileR, SFM_RDWR, &info); //RDWR will append to file instead of overwrite file

    if (opts->wav_out_f == NULL) {
        LOG_ERROR("Error - could not open wav output file %s\n", opts->wav_out_fileR);
        return;
    }
}

void
openWavOutFileLR(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    SF_INFO info;
    info.samplerate = 8000; //8000
    info.channels = 2;      //2 channel for stereo output
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
    opts->wav_out_f = sf_open(opts->wav_out_file, SFM_RDWR, &info); //RDWR will append to file instead of overwrite file

    if (opts->wav_out_f == NULL) {
        LOG_ERROR("Error - could not open wav output file %s\n", opts->wav_out_file);
        return;
    }
}

void
openWavOutFileRaw(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    SF_INFO info;
    info.samplerate = 48000; //8000
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
    opts->wav_out_raw = sf_open(opts->wav_out_file_raw, SFM_WRITE, &info);
    if (opts->wav_out_raw == NULL) {
        LOG_ERROR("Error - could not open raw wav output file %s\n", opts->wav_out_file_raw);
        return;
    }
}

void
closeWavOutFile(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    sf_close(opts->wav_out_f);
}

void
closeWavOutFileL(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    sf_close(opts->wav_out_f);
}

void
closeWavOutFileR(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    sf_close(opts->wav_out_fR);
}

void
closeWavOutFileRaw(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    sf_close(opts->wav_out_raw);
}

void
openSymbolOutFile(dsd_opts* opts, dsd_state* state) {
    closeSymbolOutFile(opts, state);
    opts->symbol_out_f = fopen(opts->symbol_out_file, "w");
}

void
closeSymbolOutFile(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    if (opts->symbol_out_f) {
        fclose(opts->symbol_out_f);
        opts->symbol_out_f = NULL;
    }
}

void
rotate_symbol_out_file(dsd_opts* opts, dsd_state* state) {
    if (opts->symbol_out_f && opts->symbol_out_file_is_auto == 1) {
        if ((time(NULL) - opts->symbol_out_file_creation_time) >= 3600) //3600 is one hour in seconds
        {
            //basically just lift the close and open from ncurses handler for 'r' and then 'R'
            // closeSymbolOutFile (opts, state); //open also does this, so don't need to do it twice
            char timestr[7];
            char datestr[9];
            getTime_buf(timestr);
            getDate_buf(datestr);
            sprintf(opts->symbol_out_file, "%s_%s_dibit_capture.bin", datestr, timestr);
            openSymbolOutFile(opts, state);

            //add a system event to echo in the event history
            state->event_history_s[0].Event_History_Items[0].color_pair = 4;
            char event_str[2000];
            memset(event_str, 0, sizeof(event_str));
            sprintf(event_str, "DSD-neo Dibit Capture File Rotated: %s;", opts->symbol_out_file);
            watchdog_event_datacall(opts, state, 0xFFFFFF, 0xFFFFFF, event_str, 0);
            state->lastsrc =
                0; //this could wipe a call, but usually on TDMA cc's, slot 1 is the control channel, so may never be set when this is run
            watchdog_event_history(opts, state, 0);
            watchdog_event_current(opts, state, 0);

            // stack buffers; no free needed
            opts->symbol_out_file_creation_time = time(NULL);
            // opts->symbol_out_file_is_auto = 1;
        }
    }
}

//input bit array, return output as up to a 64-bit value
uint64_t
convert_bits_into_output(uint8_t* input, int len) {
    int i;
    uint64_t output = 0;
    for (i = 0; i < len; i++) {
        output <<= 1;
        output |= (uint64_t)(input[i] & 1);
    }
    return output;
}

/* Note: ConvertBitIntoBytes() is provided in DMR utils (dmr_utils.c).
 * Do not duplicate here to avoid multiple definition at link time. */

void
pack_bit_array_into_byte_array(uint8_t* input, uint8_t* output, int len) {
    int i;
    for (i = 0; i < len; i++) {
        output[i] = (uint8_t)convert_bits_into_output(&input[(size_t)i * 8u], 8);
    }
}

//take len amount of bits and pack into x amount of bytes (asymmetrical)
void
pack_bit_array_into_byte_array_asym(uint8_t* input, uint8_t* output, int len) {
    int i = 0;
    int k = len % 8;
    for (i = 0; i < len; i++) {
        output[i / 8] <<= 1;
        output[i / 8] |= input[i];
    }
    //if any leftover bits that don't flush the last byte fully packed, shift them over left
    if (k) {
        output[i / 8] <<= 8 - k;
    }
}

//take len amount of bytes and unpack back into a bit array
void
unpack_byte_array_into_bit_array(uint8_t* input, uint8_t* output, int len) {
    int i = 0, k = 0;
    for (i = 0; i < len; i++) {
        output[k++] = (input[i] >> 7) & 1;
        output[k++] = (input[i] >> 6) & 1;
        output[k++] = (input[i] >> 5) & 1;
        output[k++] = (input[i] >> 4) & 1;
        output[k++] = (input[i] >> 3) & 1;
        output[k++] = (input[i] >> 2) & 1;
        output[k++] = (input[i] >> 1) & 1;
        output[k++] = (input[i] >> 0) & 1;
    }
}

//take len amount of bits and pack into x amount of bytes (asymmetrical)
void
pack_ambe(char* input, uint8_t* output, int len) {
    int i = 0;
    int k = len % 8;
    for (i = 0; i < len; i++) {
        output[i / 8] <<= 1;
        output[i / 8] |= (uint8_t)input[i];
    }
    //if any leftover bits that don't flush the last byte fully packed, shift them over left
    if (k) {
        output[i / 8] <<= 8 - k;
    }
}

//unpack byte array with ambe data into a 49-bit bitwise array
void
unpack_ambe(uint8_t* input, char* ambe) {
    int i = 0, k = 0;
    for (i = 0; i < 6; i++) {
        ambe[k++] = (input[i] >> 7) & 1;
        ambe[k++] = (input[i] >> 6) & 1;
        ambe[k++] = (input[i] >> 5) & 1;
        ambe[k++] = (input[i] >> 4) & 1;
        ambe[k++] = (input[i] >> 3) & 1;
        ambe[k++] = (input[i] >> 2) & 1;
        ambe[k++] = (input[i] >> 1) & 1;
        ambe[k++] = (input[i] >> 0) & 1;
    }
    ambe[48] = input[6] >> 7;
}

//recover previous IV for SDRTrunk .mbe files when P25p1
uint64_t
reverse_lfsr_64_to_len(dsd_opts* opts, uint8_t* iv, int16_t len) {

    uint64_t lfsr = 0, bit1 = 0, bit2 = 0;

    lfsr = ((uint64_t)iv[0] << 56ULL) + ((uint64_t)iv[1] << 48ULL) + ((uint64_t)iv[2] << 40ULL)
           + ((uint64_t)iv[3] << 32ULL) + ((uint64_t)iv[4] << 24ULL) + ((uint64_t)iv[5] << 16ULL)
           + ((uint64_t)iv[6] << 8ULL) + ((uint64_t)iv[7] << 0ULL);

    memset(iv, 0, 8 * sizeof(uint8_t));

    for (int16_t cnt = 0; cnt < len; cnt++) {
        //63,61,45,37,27,14
        // Polynomial is C(x) = x^64 + x^62 + x^46 + x^38 + x^27 + x^15 + 1

        //basically, just get the taps at the +1 position on all but MSB, then check the LSB and configure bit as required
        bit1 = ((lfsr >> 62) ^ (lfsr >> 46) ^ (lfsr >> 38) ^ (lfsr >> 27) ^ (lfsr >> 15)) & 0x1;
        bit2 = lfsr & 1;
        if (bit1 == bit2) {
            bit2 = 0;
        } else {
            bit2 = 1;
        }

        //just run this in reverse of normal LFSR
        lfsr = (lfsr >> 1) | (bit2 << 63);
    }

    for (int16_t i = 0; i < 8; i++) {
        iv[i] = (lfsr >> (56 - (i * 8))) & 0xFF;
    }

    if (opts->payload == 1) {
        fprintf(stderr, " RV LFSR(%02d): ", len);
        for (int16_t i = 0; i < 8; i++) {
            fprintf(stderr, "%02X", iv[i]);
        }
        fprintf(stderr, ";");
    }

    return bit2;
}

//convert a user string into a uint8_t array
uint16_t
parse_raw_user_string(char* input, uint8_t* output, size_t out_cap) {
    if (!input || !output || out_cap == 0) {
        return 0;
    }

    // Since we want this as octets, get strlen value, then divide by two
    size_t in_len = strlen((const char*)input);
    if (in_len == 0) {
        return 0;
    }

    // If odd number of nibbles, we will logically pad one nibble (shift last)
    int shift = 0;
    if (in_len & 1U) {
        shift = 1;
        in_len++; // treat as if one extra nibble was provided
    }

    // Number of octets we intend to produce from input
    size_t want_octets = in_len / 2U;
    // Respect the capacity of the output buffer
    size_t max_octets = out_cap;
    if (want_octets > max_octets) {
        want_octets = max_octets;
        // If truncating, do not attempt to left-shift any octet beyond bounds
        if (shift && want_octets == 0) {
            shift = 0;
        }
    }

    char octet_char[3];
    octet_char[2] = '\0';
    size_t k = 0; // input nibble offset

    for (size_t i = 0; i < want_octets; i++) {
        // Copy up to two hex chars (nibbles); guard against reading past input
        octet_char[0] = '\0';
        octet_char[1] = '\0';
        for (int c = 0; c < 2; c++) {
            size_t pos = k + (size_t)c;
            if (pos < strlen(input)) {
                octet_char[c] = input[pos];
            } else {
                // logical pad with zero if input ended (only possible on odd nibble)
                octet_char[c] = '0';
            }
        }
        // Parse two nibbles into a byte
        unsigned int val = 0;
        (void)sscanf(octet_char, "%2X", &val);
        output[i] = (uint8_t)val;
        k += 2;
    }

    // If we had an odd input nibble count, left shift the last written octet
    if (shift && want_octets > 0) {
        output[want_octets - 1] <<= 4;
    }

    return (uint16_t)want_octets;
}

uint16_t
ambe2_str_to_decode(dsd_opts* opts, dsd_state* state, char* ambe_str, uint8_t* ks, uint16_t ks_idx, uint8_t dmra,
                    uint8_t is_enc, uint8_t ks_available) {
    UNUSED(opts);

    char ambe_fr[4][24];
    memset(ambe_fr, 0, sizeof(ambe_fr));
    uint8_t dibit_pair = 0;
    uint8_t dibit1 = 0, dibit2 = 0;
    const int *w, *x, *y, *z;
    w = rW;
    x = rX;
    y = rY;
    z = rZ;
    for (size_t i = 0; i < 18; i++) {

        char octet_char[2];
        octet_char[1] = 0;

        strncpy(octet_char, ambe_str + i, 1);
        sscanf(octet_char, "%hhX", &dibit_pair);

        dibit1 = (dibit_pair >> 2) & 0x3;
        dibit2 = (dibit_pair >> 0) & 0x3;

        //debug
        // fprintf (stderr, "\n dibit_pair: %X = %d, %d;", dibit_pair, dibit1, dibit2);

        //load into ambe_fr
        ambe_fr[*w][*x] = (1 & (dibit1 >> 1)); // bit 1
        ambe_fr[*y][*z] = (1 & (dibit1 >> 0)); // bit 0

        w++;
        x++;
        y++;
        z++;

        ambe_fr[*w][*x] = (1 & (dibit2 >> 1)); // bit 1
        ambe_fr[*y][*z] = (1 & (dibit2 >> 0)); // bit 0

        w++;
        x++;
        y++;
        z++;

        //working now!
    }

    char ambe_d[49];
    memset(ambe_d, 0, sizeof(ambe_d));
    state->errs = mbe_eccAmbe3600x2450C0(ambe_fr);
    state->errs2 = state->errs;
    mbe_demodulateAmbe3600x2450Data(ambe_fr);
    state->errs2 += mbe_eccAmbe3600x2450Data(ambe_fr, ambe_d);
    state->debug_audio_errors += state->errs2;

    //keystream application
    for (uint8_t i = 0; i < 49; i++) {
        ambe_d[i] ^= ks[(ks_idx++) % 3000];
    }

    //DMRA or P25 KS, skip the left over 7 bits from a byte
    if (dmra == 1) {
        ks_idx += 7;
    }

    mbe_processAmbe2450Dataf(state->audio_out_temp_buf, &state->errs, &state->errs2, state->err_str, ambe_d,
                             state->cur_mp, state->prev_mp, state->prev_mp_enhanced, opts->uvquality);

    if (opts->payload == 1) {
        PrintAMBEData(opts, state, ambe_d);
    }

    if (is_enc == 0 || ks_available == 1) {

        //convert and save to .amb file if desired
        if (opts->mbe_out_f != NULL) {
            saveAmbe2450Data(opts, state, ambe_d);
        }

        //audio out stack
        if (opts->floating_point == 0) {
            processAudio(opts, state);
        }

        //per call wav files
        if (opts->wav_out_f != NULL && opts->dmr_stereo_wav == 1) {
            writeSynthesizedVoice(opts, state);
        }

        //static wav file
        if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
            writeSynthesizedVoiceMS(opts, state);
        }

        //to make the static wav file work, I had to write a work around
        //to either play audio from left only when writing wav files,
        //or to play from both speakers if not doing either per-call or static wav
        if (opts->audio_out == 1 && opts->floating_point == 0) {
            if (opts->static_wav_file == 1 || opts->dmr_stereo_wav == 1) {
                playSynthesizedVoice(opts, state);
            } else {
                playSynthesizedVoiceMS(opts, state);
            }
        }

        if (opts->audio_out == 1 && opts->floating_point == 1) {
            memcpy(state->f_l, state->audio_out_temp_buf, sizeof(state->f_l));
            playSynthesizedVoiceFM(opts, state);
        }
        //else if not floating point audio or audio out, then purge the audio buffers before they overflow and segfault
        else if (opts->audio_out == 0) {
            if (state->audio_out_idx2 >= 800000) {
                state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
                state->audio_out_buf_p = state->audio_out_buf + 100;
                memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
                memset(state->audio_out_buf, 0, 100 * sizeof(short));
                state->audio_out_idx2 = 0;
            }
        }
    }

    return ks_idx; //return current ks_idx
}

uint16_t
imbe_str_to_decode(dsd_opts* opts, dsd_state* state, char* imbe_str, uint8_t* ks, uint16_t ks_idx, uint8_t is_enc,
                   uint8_t ks_available) {
    UNUSED(opts);

    char imbe_fr[8][23];
    memset(imbe_fr, 0, sizeof(imbe_fr));
    uint8_t dibit_pair = 0;
    uint8_t dibit1 = 0, dibit2 = 0;
    const int *w, *x, *y, *z;
    w = iW;
    x = iX;
    y = iY;
    z = iZ;
    for (size_t i = 0; i < 36; i++) {

        char octet_char[2];
        octet_char[1] = 0;

        strncpy(octet_char, imbe_str + i, 1);
        sscanf(octet_char, "%hhX", &dibit_pair);

        dibit1 = (dibit_pair >> 2) & 0x3;
        dibit2 = (dibit_pair >> 0) & 0x3;

        //debug
        // fprintf (stderr, "\n dibit_pair: %X = %d, %d;", dibit_pair, dibit1, dibit2);

        //load into imbe_fr
        imbe_fr[*w][*x] = (1 & (dibit1 >> 1)); // bit 1
        imbe_fr[*y][*z] = (1 & (dibit1 >> 0)); // bit 0

        w++;
        x++;
        y++;
        z++;

        imbe_fr[*w][*x] = (1 & (dibit2 >> 1)); // bit 1
        imbe_fr[*y][*z] = (1 & (dibit2 >> 0)); // bit 0

        w++;
        x++;
        y++;
        z++;

        //working now!
    }

    char imbe_d[88];
    memset(imbe_d, 0, sizeof(imbe_d));
    state->errs = mbe_eccImbe7200x4400C0(imbe_fr);
    state->errs2 = state->errs;
    mbe_demodulateImbe7200x4400Data(imbe_fr);
    state->errs2 += mbe_eccImbe7200x4400Data(imbe_fr, imbe_d);
    state->debug_audio_errors += state->errs2;

    //keystream application
    for (uint8_t i = 0; i < 88; i++) {
        imbe_d[i] ^= ks[(ks_idx++) % 3000];
    }

    mbe_processImbe4400Dataf(state->audio_out_temp_buf, &state->errs, &state->errs2, state->err_str, imbe_d,
                             state->cur_mp, state->prev_mp, state->prev_mp_enhanced, opts->uvquality);

    if (opts->payload == 1) {
        PrintIMBEData(opts, state, imbe_d);
    }

    if (is_enc == 0 || ks_available == 1) {

        //convert and save to .imb file if desired
        if (opts->mbe_out_f != NULL) {
            saveImbe4400Data(opts, state, imbe_d);
        }

        //audio out stack
        if (opts->floating_point == 0) {
            processAudio(opts, state);
        }

        //per call wav files
        if (opts->wav_out_f != NULL && opts->dmr_stereo_wav == 1) {
            writeSynthesizedVoice(opts, state);
        }

        //static wav file
        if (opts->wav_out_f != NULL && opts->static_wav_file == 1) {
            writeSynthesizedVoiceMS(opts, state);
        }

        //to make the static wav file work, I had to write a work around
        //to either play audio from left only when writing wav files,
        //or to play from both speakers if not doing either per-call or static wav
        if (opts->audio_out == 1 && opts->floating_point == 0) {
            if (opts->static_wav_file == 1 || opts->dmr_stereo_wav == 1) {
                playSynthesizedVoice(opts, state);
            } else {
                playSynthesizedVoiceMS(opts, state);
            }
        }

        if (opts->audio_out == 1 && opts->floating_point == 1) {
            memcpy(state->f_l, state->audio_out_temp_buf, sizeof(state->f_l));
            playSynthesizedVoiceFM(opts, state);
        }
        //else if not floating point audio or audio out, then purge the audio buffers before they overflow and segfault
        else if (opts->audio_out == 0) {
            if (state->audio_out_idx2 >= 800000) {
                state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
                state->audio_out_buf_p = state->audio_out_buf + 100;
                memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
                memset(state->audio_out_buf, 0, 100 * sizeof(short));
                state->audio_out_idx2 = 0;
            }
        }
    }

    return ks_idx; //return current ks_idx
}

void
read_sdrtrunk_json_format(dsd_opts* opts, dsd_state* state) {

    /* Allocate raw buffer + 1 for NUL terminator; no need to zero-fill */
    char* source_str = (char*)malloc(0x100000 + 1);
    if (source_str == NULL) {
        LOG_ERROR("Failed to allocate memory for MBE file buffer\n");
        return;
    }
    size_t source_size;

    int8_t protocol = -1;
    uint16_t version = 1; //any .mbe file that does not have a version field should be considered version 1
    uint32_t source = 0;
    uint32_t target = 0;
    int8_t gi = -1;
    uint8_t is_enc = 0;
    uint8_t ks_available = 0; //if encrypted, this signals if a keystream was created for enc muting / unmuting
    uint8_t is_dmra = 1;      //Denny, we need an MFID in the JSON file plz
    uint8_t show_time = 1;    //if this has already ran once, don't keep showing the time
    uint8_t alg_id = 0;
    uint16_t key_id = 0;
    unsigned long long int iv_hex = 0;
    int rc4_db = 256;
    int rc4_mod = 13;

    time_t event_time = 0; // set after parsing; 0 indicates unset

    //for event history items
    state->dmr_color_code = 0;
    state->lastsrc = 0;
    state->lasttg = 0;
    state->gi[0] = -1;
    state->synctype = DSD_SYNC_NONE;
    state->lastsynctype = DSD_SYNC_NONE;

    //watchdog for event history
    watchdog_event_history(opts, state, 0);
    watchdog_event_current(opts, state, 0);

    uint8_t ks[3000];
    memset(ks, 0, sizeof(ks));
    uint16_t ks_idx = 0; //keystream index value

    //P25p1 IMBE / IV out of order execution on .mbe files (fixed in version 2 of .mbe file)
    uint8_t ks_i[3000];
    memset(ks_i, 0, sizeof(ks_i));
    uint16_t ks_idx_i = 808; //keystream index value IMBE (start at 808 for out of order ESS)
    int imbe_counter = 0;    //count IMBE frames for when to skip 2 bytes of ks and juggle keystreams

    source_size = fread(source_str, 1, 0x100000, opts->mbe_in_f);
    source_str[source_size] = '\0'; /* ensure C-string for strtok/strncmp */

    //debug
    // fprintf (stderr, " Source Size: %d.\n", source_size);
    // fprintf (stderr, "\n");

    char* str_buffer = strtok(source_str, "{ \""); //value after initial { open bracket

    //debug print current str_buffer
    // fprintf (stderr, "%s", str_buffer);

    for (size_t i = 0; i < source_size; i++) {

        //debug print current str_buffer
        // fprintf (stderr, "%s", str_buffer);

        if (strncmp("version", str_buffer, 7) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            version = strtol(str_buffer, NULL, 10);

            //debug set value
            if (opts->payload == 1) {
                fprintf(stderr, "\n Version: %d;", version);
            }

            //debug print current str_buffer
            // fprintf (stderr, "\n Version: %s", str_buffer);
        }

        //compare and set items accordingly
        if (strncmp("protocol", str_buffer, 8) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            //debug print current str_buffer
            fprintf(stderr, "\n Protocol: %s", str_buffer);

            //compare protocol and set to proper codec etc
            if (strncmp("APCO25-PHASE1", str_buffer, 13) == 0) {
                //set IMBE protocol here
                protocol = 1;

                //rc4 dropbyte and key len mod
                rc4_db = 267;
                rc4_mod = 13;

                state->synctype = DSD_SYNC_P25P1_POS;
                state->lastsynctype = DSD_SYNC_P25P1_POS;
            }

            if (strncmp("APCO25-PHASE2", str_buffer, 13) == 0) {
                //set AMBE+2 protocol here
                protocol = 2;

                //rc4 dropbyte and key len mod
                rc4_db = 256;
                rc4_mod = 13;

                state->synctype = DSD_SYNC_P25P2_POS;
                state->lastsynctype = DSD_SYNC_P25P2_POS;
            }

            if (strncmp("DMR", str_buffer, 3) == 0) {
                //set AMBE+2 protocol here
                protocol = 2;

                //rc4 dropbyte and key len mod
                rc4_db = 256;
                rc4_mod = 9;

                state->synctype = DSD_SYNC_DMR_BS_DATA_POS;
                state->lastsynctype = DSD_SYNC_DMR_BS_DATA_POS;
            }

            //open .imb or .amb file, if desired, but only after setting a synctype
            if (state->synctype != DSD_SYNC_NONE) {
                //if converting to .amb or .imb, open that file format as well
                if ((opts->mbe_out_dir[0] != 0) && (opts->mbe_out_f == NULL)) {
                    openMbeOutFile(opts, state);
                }
            }
        }

        if (strncmp("call_type", str_buffer, 9) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            //set gi value based on this
            if (strncmp("GROUP", str_buffer, 5) == 0) {
                gi = 0;
            } else {
                gi = 1;
            }

            state->gi[0] = gi;

            //debug set value
            // fprintf (stderr, " GI: %d;", gi);

            //debug print current str_buffer
            fprintf(stderr, "\n Call Type: %s", str_buffer);
        }

        if (strncmp("encrypted", str_buffer, 9) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            //set enc value based on this
            if (strncmp("true", str_buffer, 4) == 0) {
                is_enc = 1;
            } else {
                is_enc = 0;
            }

            //reset other enc variables (filled in later on if available)
            alg_id = 0;
            key_id = 0;

            //debug set value
            // fprintf (stderr, " ENC: %d;", is_enc);

            //debug print current str_buffer
            fprintf(stderr, "\n Encryption: %s", str_buffer);
        }

        if (strncmp("to", str_buffer, 2) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            target = strtol(str_buffer, NULL, 10);

            state->lasttg = target;

            //debug set value
            // fprintf (stderr, " Target: %d;", target);

            //debug print current str_buffer
            fprintf(stderr, "\n To: %s", str_buffer);
        }

        if (strncmp("from", str_buffer, 4) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            source = strtol(str_buffer, NULL, 10);

            state->lastsrc = source;

            //debug set value
            // fprintf (stderr, " Source: %d;", source);

            //debug print current str_buffer
            fprintf(stderr, "\n From: %s", str_buffer);
        }

        if (strncmp("encryption_algorithm", str_buffer, 20) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            alg_id = strtol(str_buffer, NULL, 10);

            //debug set value
            if (opts->payload == 1) {
                fprintf(stderr, "\n Alg ID: %02X;", alg_id);
            }

            //set just in case needed
            is_enc = 1;

            //debug print current str_buffer
            // fprintf (stderr, "\n Encryption Alg: %s", str_buffer);
        }

        if (strncmp("encryption_key_id", str_buffer, 17) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            key_id = strtol(str_buffer, NULL, 10);

            //debug set value
            if (opts->payload == 1) {
                fprintf(stderr, "\n Key ID: %04X;", key_id);
            }

            //set just in case needed
            is_enc = 1;

            //debug print current str_buffer
            // fprintf (stderr, "\n Encryption KID: %s", str_buffer);
        }

        if (strncmp("encryption_mi", str_buffer, 13) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            uint16_t iv_len = strlen((const char*)str_buffer);
            char iv_str[20];
            memset(iv_str, 0, sizeof(iv_str));

            //debug this str_buffer len
            // fprintf (stderr, " IV STR LEN: %d;", iv_len);

            if (iv_len == 18) { //P25 MI has an extra zero byte (two zeroes) appended to MI, remove those
                iv_len = 16;
            }
            strncpy(iv_str, str_buffer, iv_len); //copy out final IV value from the MI

            iv_hex = strtoull(iv_str, NULL, 16); //Note: The 16 here is for base 16 (hex), not 16 chars

            //debug set value
            if (opts->payload == 1) {
                fprintf(stderr, "\n IV: %016llX;", iv_hex); //not really needed if loaded into array
            }

            //debug print current str_buffer
            // fprintf (stderr, "\n Encryption MI/IV: %s", str_buffer);

            //WIP: This is the last field of enc, so we create a new keystream here, if needed
            state->currentslot = 0;
            state->payload_algid = alg_id;
            state->payload_mi = iv_hex;
            state->payload_keyid = key_id;
            if (state->keyloader == 1) {
                keyring(opts, state);
            }

            //TODO: Handle multi keystream creation with a new function
            uint8_t ks_bytes[375];
            memset(ks_bytes, 0, sizeof(ks_bytes));
            uint8_t kiv[15];
            memset(kiv, 0, sizeof(kiv));

            //Test: Setup a simple RC4 for now (working)
            if ((alg_id == 0xAA || alg_id == 0x21) && state->R != 0) {

                //load key into key portion of kiv
                kiv[0] = ((state->R & 0xFF00000000) >> 32);
                kiv[1] = ((state->R & 0xFF000000) >> 24);
                kiv[2] = ((state->R & 0xFF0000) >> 16);
                kiv[3] = ((state->R & 0xFF00) >> 8);
                kiv[4] = ((state->R & 0xFF) >> 0);

                //load the str_buffer into the IV portion of kiv
                // KIV is 15 bytes; IV occupies bytes [5..14] → capacity 10
                parse_raw_user_string(str_buffer, kiv + 5, sizeof(kiv) - 5);

                rc4_block_output(rc4_db, rc4_mod, 200, kiv, ks_bytes);

                unpack_byte_array_into_bit_array(ks_bytes, ks, 200);

                //reverse lfsr on IV and create keystream with that as well
                //due to out of order execution on P25p1 ESS sync.
                if (protocol == 1 && version == 1) {
                    reverse_lfsr_64_to_len(opts, kiv + 5, 64);

                    memset(ks_bytes, 0, sizeof(ks_bytes));

                    rc4_block_output(rc4_db, rc4_mod, 200, kiv, ks_bytes);

                    unpack_byte_array_into_bit_array(ks_bytes, ks_i, 200);
                }

                ks_available = 1;

            } //end test

            //NOTE: Regarding SDRTrunk .mbe format, the ESS Encryption Sync
            //is in the correct location on P25p2, but for P25p1, the ESS
            //information preceeds LDU2, but should be AFTER the LDU2 IMBE frames,
            //so, for now, we utilize a reverse lfsr function (Crypthings coming in handy)
            //and recover the previous LFSR and make two keystreams in order
            //to properly decrypt the initial frame, and then juggle the
            //keystreams in code to provide a smooth decryption session of P25p1.

            //Update: There is a pull request available now, and using a version value,
            //we will be able to do either format (ESS out of order vs ESS in correct order)

            //reset ks_idx to 0
            ks_idx = 0;

            //reset frame counter
            imbe_counter = 0;

            //set just in case needed
            is_enc = 1;
        }

        if (strncmp("hex", str_buffer, 3) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            if (protocol == 1) //P25p1 IMBE
            {
                //debug print current str_buffer
                // fprintf (stderr, "\n IMBE HEX: %s", str_buffer);

                imbe_counter++;

                //36 hex characters on 'hex' which is the IMBE interleaved C codewords
                if (version == 1) {
                    ks_idx_i = imbe_str_to_decode(opts, state, str_buffer, ks_i, ks_idx_i, is_enc, ks_available);
                } else {
                    ks_idx = imbe_str_to_decode(opts, state, str_buffer, ks, ks_idx, is_enc, ks_available);
                }

                //skip LSD bits in-between these two IMBE voice frames
                if (imbe_counter == 8 || imbe_counter == 17) {
                    ks_idx_i += 16;
                }

                //juggle keystreams and reset the I counter (if version 1)
                if (imbe_counter == 9 && version == 1) {
                    memcpy(ks_i, ks, sizeof(ks_i));
                    ks_idx_i = 0;

                    //debug
                    // fprintf (stderr, " LDU2;");
                }
                //reset keystream idx after frame 18
                else if (imbe_counter == 18 && version == 2) {
                    ks_idx = 0;

                    //debug
                    // fprintf (stderr, " LDU2;");
                }

                //debug
                // fprintf (stderr, " # %02d; KS_IDX_I: %04d;", imbe_counter, ks_idx_i);

                //debug
                // if (is_enc == 1 && ks_available == 0)
                //   fprintf (stderr, " Enc Mute;");
                // else if (is_enc == 1 && ks_available == 1)
                //   fprintf (stderr, " Enc Play;");

            } else if (protocol == 2) //P25p2 AMBE
            {
                //debug print current str_buffer
                // fprintf (stderr, "\n AMBE HEX: %s", str_buffer);

                //18 hex characters on 'hex' which is the AMBE interleaved C codewords
                ks_idx = ambe2_str_to_decode(opts, state, str_buffer, ks, ks_idx, is_dmra, is_enc, ks_available);

                //debug
                // if (is_enc == 1 && ks_available == 0)
                //   fprintf (stderr, " Enc Mute;");
                // else if (is_enc == 1 && ks_available == 1)
                //   fprintf (stderr, " Enc Play;");
            }
        }

        if (strncmp("time", str_buffer, 4) == 0) {
            str_buffer = strtok(NULL, " : \""); //next value after any : "" string

            char time_str[20];
            memset(time_str, 0, sizeof(time_str));
            strncpy(time_str, str_buffer, 10); //full string is 13, but not copying milliseconds to match time(NULL)

            event_time = strtol(time_str, NULL, 10);

            //working now with tweak
            state->event_history_s[0].Event_History_Items[0].event_time = event_time;

            //debug set value
            // fprintf (stderr, " Time: %ld;", event_time);

            //what is actual time_t for time(NULL);
            // fprintf (stderr, " Time(NULL): %ld;", time(NULL));

            //convert to legible time and date format
            char timestr[9];
            char datestr[11];
            getTimeN_buf(event_time, timestr);
            getDateN_buf(event_time, datestr);

            //user legible time
            if (show_time == 1) {
                fprintf(stderr, " Date: %s Time: %s", datestr, timestr);
            }

            /* stack buffers; no free */

            show_time = 0;

            //debug print current str_buffer
            // fprintf (stderr, "\n Time: %s", str_buffer);
        }

        //reset ks_idx if this isn't encrypted (ambe only)
        if (is_enc == 0) {
            ks_idx = 0;
        }

        str_buffer = strtok(NULL, " : \""); //next value after any : "" string

        if (str_buffer == NULL) {
            break;
        }

        //exit loop if signal
        if (exitflag == 1) {
            break;
        }
    }

    //free allocated memory from the source string
    if (source_str != NULL) {
        free(source_str);
        source_str = NULL;
    }

    //watchdog for event history
    watchdog_event_history(opts, state, 0);
    watchdog_event_current(opts, state, 0);

    //if .imb or .amb file open, close it now
    if (opts->mbe_out_f != NULL) {
        closeMbeOutFile(opts, state);
    }

    //end line break
    fprintf(stderr, "\n");
}
