// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Core file I/O helpers used by CLI/UI orchestration.
 *
 * Declares file-related helpers implemented in core.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>
#include <dsd-neo/platform/sndfile_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void saveImbe4400Data(dsd_opts* opts, dsd_state* state, char* imbe_d);
void saveAmbe2450Data(dsd_opts* opts, dsd_state* state, char* ambe_d);
void saveAmbe2450DataR(dsd_opts* opts, dsd_state* state, char* ambe_d);
void PrintAMBEData(dsd_opts* opts, dsd_state* state, char* ambe_d);
void PrintIMBEData(dsd_opts* opts, dsd_state* state, char* imbe_d);
int readImbe4400Data(dsd_opts* opts, dsd_state* state, char* imbe_d);
int readAmbe2450Data(dsd_opts* opts, dsd_state* state, char* ambe_d);
void read_sdrtrunk_json_format(dsd_opts* opts, dsd_state* state);
void openMbeInFile(dsd_opts* opts, dsd_state* state);
void openMbeOutFile(dsd_opts* opts, dsd_state* state);
void openMbeOutFileR(dsd_opts* opts, dsd_state* state);
void openWavOutFile(dsd_opts* opts, dsd_state* state);
void openWavOutFileL(dsd_opts* opts, dsd_state* state);
void openWavOutFileR(dsd_opts* opts, dsd_state* state);
void openWavOutFileLR(dsd_opts* opts, dsd_state* state);
SNDFILE* open_wav_file(char* dir, char* temp_filename, uint16_t sample_rate, uint8_t ext);
void openWavOutFileRaw(dsd_opts* opts, dsd_state* state);
void openSymbolOutFile(dsd_opts* opts, dsd_state* state);
SNDFILE* close_wav_file(SNDFILE* wav_file);
SNDFILE* close_and_rename_wav_file(SNDFILE* wav_file, char* wav_out_filename, char* dir, Event_History_I* event_struct);
SNDFILE* close_and_delete_wav_file(SNDFILE* wav_file, char* wav_out_filename);
void closeMbeOutFile(dsd_opts* opts, dsd_state* state);
void closeMbeOutFileR(dsd_opts* opts, dsd_state* state);
void closeSymbolOutFile(dsd_opts* opts, dsd_state* state);
void rotate_symbol_out_file(dsd_opts* opts, dsd_state* state);
void closeWavOutFile(dsd_opts* opts, dsd_state* state);
void closeWavOutFileL(dsd_opts* opts, dsd_state* state);
void closeWavOutFileR(dsd_opts* opts, dsd_state* state);
void closeWavOutFileRaw(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
