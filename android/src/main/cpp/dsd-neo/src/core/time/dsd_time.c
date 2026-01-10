// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
 * dsd_time.c
 * Time and Date Functions
 *
 * LWVMOBILE
 * 2024-04 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

// Note: Callers must free the returned strings from alloc variants.
// All helpers zero-fill on localtime() failure; non-allocating variants are provided.

#include <dsd-neo/core/time_format.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

//get HHmmss timestamp no colon (file operations)
char*
getTime(void) {
    char* curr = calloc(7, sizeof(char));
    time_t t = time(NULL);
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(curr, 7, "000000");
    } else {
        snprintf(curr, 7, "%02d%02d%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    }
    return curr;
}

//non-allocating variant
void
getTime_buf(char out[7]) {
    time_t t = time(NULL);
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(out, 7, "000000");
    } else {
        snprintf(out, 7, "%02d%02d%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    }
}

//get HH:mm:ss timestamp with colon (Sync/Console Display)
char*
getTimeC(void) {
    char* curr = calloc(9, sizeof(char));
    time_t t = time(NULL);
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(curr, 9, "00:00:00");
    } else {
        snprintf(curr, 9, "%02d:%02d:%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    }
    return curr;
}

void
getTimeC_buf(char out[9]) {
    time_t t = time(NULL);
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(out, 9, "00:00:00");
    } else {
        snprintf(out, 9, "%02d:%02d:%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    }
}

//get HH:mm:ss timestamp with colon (Ncurses Call History)
char*
getTimeN(time_t t) {
    char* curr = calloc(9, sizeof(char));
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(curr, 9, "00:00:00");
    } else {
        snprintf(curr, 9, "%02d:%02d:%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    }
    return curr;
}

void
getTimeN_buf(time_t t, char out[9]) {
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(out, 9, "00:00:00");
    } else {
        snprintf(out, 9, "%02d:%02d:%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    }
}

//get YYYYMMDD without hyphen (file operations)
char*
getDate(void) {
    char* curr = calloc(9, sizeof(char));
    time_t t = time(NULL);
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(curr, 9, "00000000");
    } else {
        snprintf(curr, 9, "%04u%02u%02u", (unsigned)(ptm->tm_year + 1900), (unsigned)(ptm->tm_mon + 1),
                 (unsigned)ptm->tm_mday);
    }
    return curr;
}

void
getDate_buf(char out[9]) {
    time_t t = time(NULL);
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(out, 9, "00000000");
    } else {
        snprintf(out, 9, "%04u%02u%02u", (unsigned)(ptm->tm_year + 1900), (unsigned)(ptm->tm_mon + 1),
                 (unsigned)ptm->tm_mday);
    }
}

//get YYYY-MM-DD with hyphen (Sync/Console Display)
char*
getDateH(void) {
    char* curr = calloc(11, sizeof(char));
    time_t t = time(NULL);
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(curr, 11, "0000-00-00");
    } else {
        snprintf(curr, 11, "%04u-%02u-%02u", (unsigned)(ptm->tm_year + 1900), (unsigned)(ptm->tm_mon + 1),
                 (unsigned)ptm->tm_mday);
    }
    return curr;
}

void
getDateH_buf(char out[11]) {
    time_t t = time(NULL);
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(out, 11, "0000-00-00");
    } else {
        snprintf(out, 11, "%04u-%02u-%02u", (unsigned)(ptm->tm_year + 1900), (unsigned)(ptm->tm_mon + 1),
                 (unsigned)ptm->tm_mday);
    }
}

//get YYYY/MM/DD with forward slash (LRRP files)
char*
getDateS(void) {
    char* curr = calloc(11, sizeof(char));
    time_t t = time(NULL);
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(curr, 11, "0000/00/00");
    } else {
        snprintf(curr, 11, "%04u/%02u/%02u", (unsigned)(ptm->tm_year + 1900), (unsigned)(ptm->tm_mon + 1),
                 (unsigned)ptm->tm_mday);
    }
    return curr;
}

void
getDateS_buf(char out[11]) {
    time_t t = time(NULL);
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(out, 11, "0000/00/00");
    } else {
        snprintf(out, 11, "%04u/%02u/%02u", (unsigned)(ptm->tm_year + 1900), (unsigned)(ptm->tm_mon + 1),
                 (unsigned)ptm->tm_mday);
    }
}

//get YYYY-MM-DD with hyphen (Ncurses Call History)
char*
getDateN(time_t t) {
    char* curr = calloc(11, sizeof(char));
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(curr, 11, "0000-00-00");
    } else {
        snprintf(curr, 11, "%04u-%02u-%02u", (unsigned)(ptm->tm_year + 1900), (unsigned)(ptm->tm_mon + 1),
                 (unsigned)ptm->tm_mday);
    }
    return curr;
}

void
getDateN_buf(time_t t, char out[11]) {
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(out, 11, "0000-00-00");
    } else {
        snprintf(out, 11, "%04u-%02u-%02u", (unsigned)(ptm->tm_year + 1900), (unsigned)(ptm->tm_mon + 1),
                 (unsigned)ptm->tm_mday);
    }
}

//get HHmmss timestamp no colon (file operations)
char*
getTimeF(time_t t) {
    char* curr = calloc(7, sizeof(char));
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(curr, 7, "000000");
    } else {
        snprintf(curr, 7, "%02d%02d%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    }
    return curr;
}

void
getTimeF_buf(time_t t, char out[7]) {
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(out, 7, "000000");
    } else {
        snprintf(out, 7, "%02d%02d%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    }
}

//get YYYYMMDD without hyphen (file operations)
char*
getDateF(time_t t) {
    char* curr = calloc(9, sizeof(char));
    struct tm* ptm = localtime(&t);
    if (ptm == NULL) {
        snprintf(curr, 9, "00000000");
    } else {
        snprintf(curr, 9, "%04u%02u%02u", (unsigned)(ptm->tm_year + 1900), (unsigned)(ptm->tm_mon + 1),
                 (unsigned)ptm->tm_mday);
    }
    return curr;
}

void
getDateF_buf(time_t t, char out[9]) {
    struct tm* ptm = localtime(&t);
    snprintf(out, 9, "%04u%02u%02u", (unsigned)(ptm->tm_year + 1900), (unsigned)(ptm->tm_mon + 1),
             (unsigned)ptm->tm_mday);
}
