// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Frame sync pattern constants and related protocol flags.
 *
 * These are legacy dibit-string patterns (ASCII '0'-'3') used by sync
 * detection and some protocol handlers.
 */

#pragma once

/* M17 Sync Patterns */
#define M17_LSF                        "11113313"
#define M17_STR                        "33331131"
/* Alternating with last symbol opposite of first symbol of LSF */
#define M17_PRE                        "31313131"
#define M17_PIV                        "13131313"
#define M17_PRE_LSF                    "3131313133331131" /* Preamble + LSF */
#define M17_PIV_LSF                    "1313131311113313" /* Preamble + LSF */
#define M17_BRT                        "31331111"
#define M17_PKT                        "13113333"

#define FUSION_SYNC                    "31111311313113131131"
#define INV_FUSION_SYNC                "13333133131331313313"

#define INV_P25P1_SYNC                 "333331331133111131311111"
#define P25P1_SYNC                     "111113113311333313133333"

#define P25P2_SYNC                     "11131131111333133333"
#define INV_P25P2_SYNC                 "33313313333111311111"

#define X2TDMA_BS_VOICE_SYNC           "113131333331313331113311"
#define X2TDMA_BS_DATA_SYNC            "331313111113131113331133"
#define X2TDMA_MS_DATA_SYNC            "313113333111111133333313"
#define X2TDMA_MS_VOICE_SYNC           "131331111333333311111131"

#define DSTAR_HD                       "131313131333133113131111"
#define INV_DSTAR_HD                   "313131313111311331313333"
#define DSTAR_SYNC                     "313131313133131113313111"
#define INV_DSTAR_SYNC                 "131313131311313331131333"

#define NXDN_MS_DATA_SYNC              "313133113131111333"
#define INV_NXDN_MS_DATA_SYNC          "131311331313333111"
#define INV_NXDN_BS_DATA_SYNC          "131311331313333131"
#define NXDN_BS_DATA_SYNC              "313133113131111313"
#define NXDN_MS_VOICE_SYNC             "313133113131113133"
#define INV_NXDN_MS_VOICE_SYNC         "131311331313331311"
#define INV_NXDN_BS_VOICE_SYNC         "131311331313331331"
#define NXDN_BS_VOICE_SYNC             "313133113131113113"

#define DMR_BS_DATA_SYNC               "313333111331131131331131"
#define DMR_BS_VOICE_SYNC              "131111333113313313113313"
#define DMR_MS_DATA_SYNC               "311131133313133331131113"
#define DMR_MS_VOICE_SYNC              "133313311131311113313331"

/* Part 1-A CAI 4.4.4 (FSW only - Late Entry - Marginal Signal) */
#define NXDN_FSW                       "3131331131"
#define INV_NXDN_FSW                   "1313113313"
/* Part 1-A CAI 4.4.3 Preamble last 9 plus FSW (start of RDCH) */
#define NXDN_PANDFSW                   "3131133313131331131" /* 19 symbols */
#define INV_NXDN_PANDFSW               "1313311131313113313" /* 19 symbols */

#define DMR_RESERVED_SYNC              "131331111133133133311313"

#define DMR_DIRECT_MODE_TS1_DATA_SYNC  "331333313111313133311111"
#define DMR_DIRECT_MODE_TS1_VOICE_SYNC "113111131333131311133333"
#define DMR_DIRECT_MODE_TS2_DATA_SYNC  "311311111333113333133311"
#define DMR_DIRECT_MODE_TS2_VOICE_SYNC "133133333111331111311133"

#define INV_PROVOICE_SYNC              "31313111333133133311331133113311"
#define PROVOICE_SYNC                  "13131333111311311133113311331133"
#define INV_PROVOICE_EA_SYNC           "13313133113113333311313133133311"
#define PROVOICE_EA_SYNC               "31131311331331111133131311311133"

/* EDACS/PV EOT dotting sequence */
#define DOTTING_SEQUENCE_A             "131313131313131313131313131313131313131313131313" /* 0xAAAA... */
#define DOTTING_SEQUENCE_B             "313131313131313131313131313131313131313131313131" /* 0x5555... */

/* ProVoice conventional string pattern:
 * default 85/85 if not enabled; else mute to avoid double sync in frame_sync. */
#ifdef PVCONVENTIONAL
#define PROVOICE_CONV                                                                                                  \
    "00000000000000000000000000000000" /* all zeroes should be unobtainable string in frame_sync synctests */
#define INV_PROVOICE_CONV                                                                                              \
    "00000000000000000000000000000000" /* all zeroes should be unobtainable string in frame_sync synctests */
#else
#define PROVOICE_CONV     "13131333111311311313131313131313" /* TX 85 RX 85 (default programming value) */
#define INV_PROVOICE_CONV "31313111333133133131313131313131" /* TX 85 RX 85 (default programming value) */
#endif
/* Short sync used when PVCONVENTIONAL is enabled by CMake. */
#define PROVOICE_CONV_SHORT     "1313133311131131" /* 16-bit short pattern, last 16 bits vary with TX/RX */
#define INV_PROVOICE_CONV_SHORT "3131311133313313"

/* EDACS */
#define EDACS_SYNC              "313131313131313131313111333133133131313131313131"
#define INV_EDACS_SYNC          "131313131313131313131333111311311313131313131313"

/* Flags for EDACS call type */
#define EDACS_IS_VOICE          0x01
#define EDACS_IS_DIGITAL        0x02
#define EDACS_IS_EMERGENCY      0x04
#define EDACS_IS_GROUP          0x08
#define EDACS_IS_INDIVIDUAL     0x10
#define EDACS_IS_ALL_CALL       0x20
#define EDACS_IS_INTERCONNECT   0x40
#define EDACS_IS_TEST_CALL      0x80
#define EDACS_IS_AGENCY_CALL    0x100
#define EDACS_IS_FLEET_CALL     0x200

/* dPMR frame sync patterns */
#define DPMR_FRAME_SYNC_1       "111333331133131131111313"
#define DPMR_FRAME_SYNC_2       "113333131331"
#define DPMR_FRAME_SYNC_3       "133131333311"
#define DPMR_FRAME_SYNC_4       "333111113311313313333131"

/* dPMR frame sync 1 to 4 - inverted */
#define INV_DPMR_FRAME_SYNC_1   "333111113311313313333131"
#define INV_DPMR_FRAME_SYNC_2   "331111313113"
#define INV_DPMR_FRAME_SYNC_3   "311313111133"
#define INV_DPMR_FRAME_SYNC_4   "111333331133131131111313"
