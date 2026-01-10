// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Named sync type IDs for self-documenting code.
 *
 * This header defines named constants for synctype/lastsynctype values used
 * throughout the decoder. Use these instead of magic numbers to make code
 * branches self-documenting and safer to modify.
 *
 * IMPORTANT: These numeric values must match the existing synctype assignments
 * in dsd_frame_sync.c. Do NOT change the numeric values - that would be a
 * behavioral change requiring extensive testing.
 *
 * Mapping source: src/dsp/dsd_frame_sync.c getFrameSync() comment block
 */
#ifndef DSD_SYNCTYPE_IDS_H
#define DSD_SYNCTYPE_IDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * P25 Phase 1 (FDMA C4FM)
 * ============================================================================ */
#define DSD_SYNC_P25P1_POS        0 /**< +P25 Phase 1 (non-inverted) */
#define DSD_SYNC_P25P1_NEG        1 /**< -P25 Phase 1 (inverted) */

/* ============================================================================
 * X2-TDMA
 * ============================================================================ */
#define DSD_SYNC_X2TDMA_DATA_POS  2 /**< +X2-TDMA data frame (non-inverted) */
#define DSD_SYNC_X2TDMA_VOICE_NEG 3 /**< -X2-TDMA voice frame (inverted) */
#define DSD_SYNC_X2TDMA_VOICE_POS 4 /**< +X2-TDMA voice frame (non-inverted) */
#define DSD_SYNC_X2TDMA_DATA_NEG  5 /**< -X2-TDMA data frame (inverted) */

/* ============================================================================
 * D-STAR
 * ============================================================================ */
#define DSD_SYNC_DSTAR_VOICE_POS  6  /**< +D-STAR voice */
#define DSD_SYNC_DSTAR_VOICE_NEG  7  /**< -D-STAR voice */
#define DSD_SYNC_DSTAR_HD_POS     18 /**< +D-STAR header */
#define DSD_SYNC_DSTAR_HD_NEG     19 /**< -D-STAR header */

/* ============================================================================
 * M17
 * ============================================================================ */
#define DSD_SYNC_M17_STR_POS      8  /**< +M17 stream frame (non-inverted) */
#define DSD_SYNC_M17_STR_NEG      9  /**< -M17 stream frame (inverted) */
#define DSD_SYNC_M17_LSF_POS      16 /**< +M17 link setup frame (non-inverted) */
#define DSD_SYNC_M17_LSF_NEG      17 /**< -M17 link setup frame (inverted) */
#define DSD_SYNC_M17_BRT_POS      76 /**< +M17 BERT frame (non-inverted) - reserved */
#define DSD_SYNC_M17_BRT_NEG      77 /**< -M17 BERT frame (inverted) - reserved */
#define DSD_SYNC_M17_PKT_POS      86 /**< +M17 packet frame (non-inverted) */
#define DSD_SYNC_M17_PKT_NEG      87 /**< -M17 packet frame (inverted) */
#define DSD_SYNC_M17_PRE_POS      98 /**< +M17 preamble (non-inverted) */
#define DSD_SYNC_M17_PRE_NEG      99 /**< -M17 preamble (inverted) */

/* ============================================================================
 * DMR (Base Station)
 * ============================================================================ */
#define DSD_SYNC_DMR_BS_DATA_POS  10 /**< +DMR BS data frame (non-inverted) */
#define DSD_SYNC_DMR_BS_VOICE_NEG 11 /**< -DMR BS voice frame (inverted) */
#define DSD_SYNC_DMR_BS_VOICE_POS 12 /**< +DMR BS voice frame (non-inverted) */
#define DSD_SYNC_DMR_BS_DATA_NEG  13 /**< -DMR BS data frame (inverted) */

/* ============================================================================
 * DMR (Mobile Station / Repeater Control)
 * ============================================================================ */
#define DSD_SYNC_DMR_MS_VOICE     32 /**< DMR MS voice */
#define DSD_SYNC_DMR_MS_DATA      33 /**< DMR MS data */
#define DSD_SYNC_DMR_RC_DATA      34 /**< DMR RC (repeater control) data */

/* ============================================================================
 * ProVoice / EDACS
 * ============================================================================ */
#define DSD_SYNC_PROVOICE_POS     14 /**< +ProVoice (EDACS) */
#define DSD_SYNC_PROVOICE_NEG     15 /**< -ProVoice (EDACS) */
#define DSD_SYNC_EDACS_POS        37 /**< +EDACS */
#define DSD_SYNC_EDACS_NEG        38 /**< -EDACS */

/* ============================================================================
 * dPMR
 * ============================================================================ */
#define DSD_SYNC_DPMR_FS1_POS     20 /**< +dPMR frame sync 1 */
#define DSD_SYNC_DPMR_FS2_POS     21 /**< +dPMR frame sync 2 */
#define DSD_SYNC_DPMR_FS3_POS     22 /**< +dPMR frame sync 3 */
#define DSD_SYNC_DPMR_FS4_POS     23 /**< +dPMR frame sync 4 */
#define DSD_SYNC_DPMR_FS1_NEG     24 /**< -dPMR frame sync 1 */
#define DSD_SYNC_DPMR_FS2_NEG     25 /**< -dPMR frame sync 2 */
#define DSD_SYNC_DPMR_FS3_NEG     26 /**< -dPMR frame sync 3 */
#define DSD_SYNC_DPMR_FS4_NEG     27 /**< -dPMR frame sync 4 */

/* ============================================================================
 * NXDN
 * ============================================================================ */
#define DSD_SYNC_NXDN_POS         28 /**< +NXDN (sync only) */
#define DSD_SYNC_NXDN_NEG         29 /**< -NXDN (sync only) */

/* ============================================================================
 * YSF (Yaesu System Fusion)
 * ============================================================================ */
#define DSD_SYNC_YSF_POS          30 /**< +YSF */
#define DSD_SYNC_YSF_NEG          31 /**< -YSF */

/* ============================================================================
 * P25 Phase 2 (TDMA CQPSK/H-DQPSK)
 * ============================================================================ */
#define DSD_SYNC_P25P2_POS        35 /**< +P25 Phase 2 (non-inverted) */
#define DSD_SYNC_P25P2_NEG        36 /**< -P25 Phase 2 (inverted) */

/* ============================================================================
 * Generic / Special
 * ============================================================================ */
#define DSD_SYNC_ANALOG           39   /**< Generic analog signal */
#define DSD_SYNC_DIGITAL          40   /**< Generic digital signal */
#define DSD_SYNC_NONE             (-1) /**< No sync / uninitialized */

/* ============================================================================
 * Protocol Classification Helper Macros
 *
 * These macros check whether a synctype belongs to a particular protocol
 * family. They are designed to replace scattered magic number comparisons
 * throughout the codebase.
 * ============================================================================ */

/** Check if synctype is P25 Phase 1 */
#define DSD_SYNC_IS_P25P1(s)      ((s) == DSD_SYNC_P25P1_POS || (s) == DSD_SYNC_P25P1_NEG)

/** Check if synctype is P25 Phase 2 */
#define DSD_SYNC_IS_P25P2(s)      ((s) == DSD_SYNC_P25P2_POS || (s) == DSD_SYNC_P25P2_NEG)

/** Check if synctype is any P25 (Phase 1 or Phase 2) */
#define DSD_SYNC_IS_P25(s)        (DSD_SYNC_IS_P25P1(s) || DSD_SYNC_IS_P25P2(s))

/** Check if synctype is X2-TDMA */
#define DSD_SYNC_IS_X2TDMA(s)     ((s) >= DSD_SYNC_X2TDMA_DATA_POS && (s) <= DSD_SYNC_X2TDMA_DATA_NEG)

/** Check if synctype is DMR Base Station */
#define DSD_SYNC_IS_DMR_BS(s)                                                                                          \
    ((s) == DSD_SYNC_DMR_BS_DATA_POS || (s) == DSD_SYNC_DMR_BS_VOICE_NEG || (s) == DSD_SYNC_DMR_BS_VOICE_POS           \
     || (s) == DSD_SYNC_DMR_BS_DATA_NEG)

/** Check if synctype is DMR Mobile Station or Repeater Control */
#define DSD_SYNC_IS_DMR_MS(s)                                                                                          \
    ((s) == DSD_SYNC_DMR_MS_VOICE || (s) == DSD_SYNC_DMR_MS_DATA || (s) == DSD_SYNC_DMR_RC_DATA)

/** Check if synctype is any DMR */
#define DSD_SYNC_IS_DMR(s) (DSD_SYNC_IS_DMR_BS(s) || DSD_SYNC_IS_DMR_MS(s))

/** Check if synctype is D-STAR (voice or header) */
#define DSD_SYNC_IS_DSTAR(s)                                                                                           \
    ((s) == DSD_SYNC_DSTAR_VOICE_POS || (s) == DSD_SYNC_DSTAR_VOICE_NEG || (s) == DSD_SYNC_DSTAR_HD_POS                \
     || (s) == DSD_SYNC_DSTAR_HD_NEG)

/** Check if synctype is any M17 */
#define DSD_SYNC_IS_M17(s)                                                                                             \
    ((s) == DSD_SYNC_M17_STR_POS || (s) == DSD_SYNC_M17_STR_NEG || (s) == DSD_SYNC_M17_LSF_POS                         \
     || (s) == DSD_SYNC_M17_LSF_NEG || (s) == DSD_SYNC_M17_BRT_POS || (s) == DSD_SYNC_M17_BRT_NEG                      \
     || (s) == DSD_SYNC_M17_PKT_POS || (s) == DSD_SYNC_M17_PKT_NEG || (s) == DSD_SYNC_M17_PRE_POS                      \
     || (s) == DSD_SYNC_M17_PRE_NEG)

/** Check if synctype is NXDN */
#define DSD_SYNC_IS_NXDN(s)       ((s) == DSD_SYNC_NXDN_POS || (s) == DSD_SYNC_NXDN_NEG)

/** Check if synctype is YSF */
#define DSD_SYNC_IS_YSF(s)        ((s) == DSD_SYNC_YSF_POS || (s) == DSD_SYNC_YSF_NEG)

/** Check if synctype is dPMR */
#define DSD_SYNC_IS_DPMR(s)       ((s) >= DSD_SYNC_DPMR_FS1_POS && (s) <= DSD_SYNC_DPMR_FS4_NEG)

/** Check if synctype is ProVoice */
#define DSD_SYNC_IS_PROVOICE(s)   ((s) == DSD_SYNC_PROVOICE_POS || (s) == DSD_SYNC_PROVOICE_NEG)

/** Check if synctype is EDACS */
#define DSD_SYNC_IS_EDACS_ONLY(s) ((s) == DSD_SYNC_EDACS_POS || (s) == DSD_SYNC_EDACS_NEG)

/** Check if synctype is EDACS/ProVoice */
#define DSD_SYNC_IS_EDACS(s)      (DSD_SYNC_IS_PROVOICE(s) || DSD_SYNC_IS_EDACS_ONLY(s))

/** Check if synctype is inverted (negative polarity) */
#define DSD_SYNC_IS_INVERTED(s)                                                                                        \
    ((s) == DSD_SYNC_P25P1_NEG || (s) == DSD_SYNC_X2TDMA_VOICE_NEG || (s) == DSD_SYNC_X2TDMA_DATA_NEG                  \
     || (s) == DSD_SYNC_DSTAR_VOICE_NEG || (s) == DSD_SYNC_DSTAR_HD_NEG || (s) == DSD_SYNC_M17_STR_NEG                 \
     || (s) == DSD_SYNC_M17_LSF_NEG || (s) == DSD_SYNC_M17_BRT_NEG || (s) == DSD_SYNC_M17_PKT_NEG                      \
     || (s) == DSD_SYNC_M17_PRE_NEG || (s) == DSD_SYNC_DMR_BS_VOICE_NEG || (s) == DSD_SYNC_DMR_BS_DATA_NEG             \
     || (s) == DSD_SYNC_PROVOICE_NEG || (s) == DSD_SYNC_EDACS_NEG || (s) == DSD_SYNC_NXDN_NEG                          \
     || (s) == DSD_SYNC_YSF_NEG || (s) == DSD_SYNC_P25P2_NEG || (s) == DSD_SYNC_DPMR_FS1_NEG                           \
     || (s) == DSD_SYNC_DPMR_FS2_NEG || (s) == DSD_SYNC_DPMR_FS3_NEG || (s) == DSD_SYNC_DPMR_FS4_NEG)

/* ============================================================================
 * Safe String Mapping Function
 *
 * Use this instead of direct SyncTypes[] array indexing to avoid out-of-bounds
 * access and NULL pointer dereferences.
 * ============================================================================ */

/**
 * @brief Convert a synctype value to a human-readable string.
 *
 * This function safely maps synctype values to descriptive strings, handling
 * both the standard range (0-43) covered by the legacy SyncTypes[] array and
 * extended M17 types (76-77, 86-87, 98-99).
 *
 * @param synctype The sync type value to convert.
 * @return A constant string describing the sync type, or "UNKNOWN" if the
 *         value is out of range or unrecognized.
 */
const char* dsd_synctype_to_string(int synctype);

#ifdef __cplusplus
}
#endif

#endif /* DSD_SYNCTYPE_IDS_H */
