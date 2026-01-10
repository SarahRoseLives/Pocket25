// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Time/date formatting helpers.
 *
 * Provides small helpers used for console/UI timestamps and log filenames.
 */

#pragma once

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

char* getTime(void);
char* getTimeC(void);
char* getTimeN(time_t t);
char* getTimeF(time_t t);

char* getDate(void);
char* getDateH(void);
char* getDateS(void);
char* getDateN(time_t t);
char* getDateF(time_t t);

void getTime_buf(char out[7]);  /* HHmmss */
void getTimeC_buf(char out[9]); /* HH:MM:SS */
void getTimeN_buf(time_t t, char out[9]);
void getTimeF_buf(time_t t, char out[7]);

void getDate_buf(char out[9]);   /* YYYYMMDD */
void getDateH_buf(char out[11]); /* YYYY-MM-DD */
void getDateS_buf(char out[11]); /* YYYY/MM/DD */
void getDateN_buf(time_t t, char out[11]);
void getDateF_buf(time_t t, char out[9]);

#ifdef __cplusplus
}
#endif
