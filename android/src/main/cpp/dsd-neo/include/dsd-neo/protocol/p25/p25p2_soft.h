// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * P25P2 soft-decision RS erasure marking API.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compute reliability for a hexbit (6 bits = 3 dibits).
 *
 * @param bit_offsets Six p2bit/p2xbit indices (relative to TS start).
 * @param ts_counter  Current timeslot counter (0-3).
 * @param reliab      Per-dibit reliability array (p2reliab or p2xreliab).
 * @return Minimum reliability of constituent dibits [0..255]; 0 on OOB.
 */
uint8_t p25p2_hexbit_reliability(const uint16_t bit_offsets[6], int ts_counter, const uint8_t* reliab);

/**
 * Build soft-decision erasure list for FACCH.
 *
 * @param ts_counter  Current timeslot counter (0-3).
 * @param scrambled   1 for p2xreliab, 0 for p2reliab.
 * @param erasures    Erasure array (in/out). Must have space for n_fixed + max_add.
 * @param n_fixed     Number of fixed erasures already present (typically 18 for FACCH).
 * @param max_add     Maximum dynamic erasures to add (recommend <=10 for FACCH).
 * @return Total erasure count.
 */
int p25p2_facch_soft_erasures(int ts_counter, int scrambled, int* erasures, int n_fixed, int max_add);

/**
 * Build soft-decision erasure list for SACCH.
 *
 * @param ts_counter  Current timeslot counter (0-3).
 * @param scrambled   1 for p2xreliab, 0 for p2reliab.
 * @param erasures    Erasure array (in/out). Must have space for n_fixed + max_add.
 * @param n_fixed     Number of fixed erasures already present (typically 11 for SACCH).
 * @param max_add     Maximum dynamic erasures to add (recommend <=16 for SACCH).
 * @return Total erasure count.
 */
int p25p2_sacch_soft_erasures(int ts_counter, int scrambled, int* erasures, int n_fixed, int max_add);

/**
 * Build soft-decision erasure list for ESS.
 *
 * ESS uses RS(44,16,29) with:
 *   - 16 payload hexbits (positions 0-15 in RS space)
 *   - 28 parity hexbits (positions 16-43 in RS space)
 *
 * @param ts_counter  Current timeslot counter (0-3).
 * @param is_4v       1 for ESS_B (4V mode, 96 bits across 4 frames), 0 for ESS_A (2V mode).
 * @param erasures    Erasure array (in/out). Must have space for n_fixed + max_add.
 * @param n_fixed     Number of fixed erasures already present.
 * @param max_add     Maximum dynamic erasures to add.
 * @return Total erasure count.
 */
int p25p2_ess_soft_erasures(int ts_counter, int is_4v, int* erasures, int n_fixed, int max_add);

#ifdef __cplusplus
}
#endif
