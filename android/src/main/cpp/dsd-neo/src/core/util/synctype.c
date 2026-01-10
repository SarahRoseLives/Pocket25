// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Safe synctype string mapping implementation.
 *
 * Provides a safe accessor function for converting synctype values to
 * human-readable strings, replacing direct SyncTypes[] array indexing.
 */

#include <dsd-neo/core/synctype_ids.h>
#include <stddef.h>

/**
 * @brief Legacy synctype string table.
 *
 * This table covers indices 0-43. Extended M17 types (76-77, 86-87, 98-99)
 * are handled separately in dsd_synctype_to_string().
 */
static const char* const SyncTypeStrings[] = {
    "P25p1",        /* 0: +P25p1 */
    "P25p1",        /* 1: -P25p1 */
    "X2TDMA DATA",  /* 2: +X2TDMA data */
    "X2TDMA DATA",  /* 3: -X2TDMA voice */
    "X2TDMA VOICE", /* 4: +X2TDMA voice */
    "X2TDMA VOICE", /* 5: -X2TDMA data */
    "DSTAR",        /* 6: +DSTAR voice */
    "DSTAR",        /* 7: -DSTAR voice */
    "M17",          /* 8: +M17 STR */
    "M17",          /* 9: -M17 STR */
    "DMR",          /* 10: +DMR BS data */
    "DMR",          /* 11: -DMR BS voice */
    "DMR",          /* 12: +DMR BS voice */
    "DMR",          /* 13: -DMR BS data */
    "EDACS/PV",     /* 14: +ProVoice */
    "EDACS/PV",     /* 15: -ProVoice */
    "M17",          /* 16: +M17 LSF */
    "M17",          /* 17: -M17 LSF */
    "DSTAR",        /* 18: +DSTAR header */
    "DSTAR",        /* 19: -DSTAR header */
    "dPMR",         /* 20: +dPMR FS1 */
    "dPMR",         /* 21: +dPMR FS2 */
    "dPMR",         /* 22: +dPMR FS3 */
    "dPMR",         /* 23: +dPMR FS4 */
    "dPMR",         /* 24: -dPMR FS1 */
    "dPMR",         /* 25: -dPMR FS2 */
    "dPMR",         /* 26: -dPMR FS3 */
    "dPMR",         /* 27: -dPMR FS4 */
    "NXDN",         /* 28: +NXDN */
    "NXDN",         /* 29: -NXDN */
    "YSF",          /* 30: +YSF */
    "YSF",          /* 31: -YSF */
    "DMR",          /* 32: DMR MS voice */
    "DMR",          /* 33: DMR MS data */
    "DMR",          /* 34: DMR RC data */
    "P25p2",        /* 35: +P25p2 */
    "P25p2",        /* 36: -P25p2 */
    "EDACS/PV",     /* 37: +EDACS */
    "EDACS/PV",     /* 38: -EDACS */
    "ANALOG",       /* 39: Generic analog */
    "DIGITAL",      /* 40: Generic digital */
    NULL,           /* 41: unused */
    NULL,           /* 42: unused */
    NULL,           /* 43: unused */
};

#define SYNCTYPE_TABLE_SIZE (sizeof(SyncTypeStrings) / sizeof(SyncTypeStrings[0]))

const char*
dsd_synctype_to_string(int synctype) {
    /* Handle special cases for extended M17 types not in the legacy table */
    switch (synctype) {
        case DSD_SYNC_M17_BRT_POS: /* 76 */
        case DSD_SYNC_M17_BRT_NEG: /* 77 */ return "M17 BRT";
        case DSD_SYNC_M17_PKT_POS: /* 86 */
        case DSD_SYNC_M17_PKT_NEG: /* 87 */ return "M17 PKT";
        case DSD_SYNC_M17_PRE_POS: /* 98 */
        case DSD_SYNC_M17_PRE_NEG: /* 99 */ return "M17 PRE";
        case DSD_SYNC_NONE: /* -1 */ return "NONE";
        default: break;
    }

    /* Check bounds for the legacy table */
    if (synctype < 0 || (size_t)synctype >= SYNCTYPE_TABLE_SIZE) {
        return "UNKNOWN";
    }

    /* Return the string if valid, otherwise UNKNOWN */
    const char* str = SyncTypeStrings[synctype];
    return str ? str : "UNKNOWN";
}
