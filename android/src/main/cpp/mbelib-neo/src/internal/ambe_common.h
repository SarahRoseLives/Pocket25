// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Internal helpers for AMBE 3600x{2400,2450} ECC and demodulation.
 *
 * Declares common routines used by both AMBE 3600x2400 and 3600x2450
 * implementations to correct C0 with Golay(23,12), demodulate C1, and
 * extract the 49-bit parameter vector.
 */

#ifndef MBELIB_NEO_INTERNAL_AMBE_COMMON_H
#define MBELIB_NEO_INTERNAL_AMBE_COMMON_H

/**
 * @brief Correct C0 for AMBE 3600x{2400,2450} with Golay(23,12).
 *
 * Applies Golay decoding to `fr[0][1..23]` in-place to correct errors
 * in the protected portion of C0.
 *
 * @param fr AMBE frame as 4x24 bitplanes (modified).
 * @return Number of corrected bit errors in C0.
 */
int mbe_eccAmbe3600C0_common(char fr[4][24]);

/**
 * @brief Demodulate AMBE 3600x{2400,2450} C1 in-place.
 *
 * Uses a pseudo-random sequence derived from the C0 payload to remove
 * the interleaving/modulation applied to C1.
 *
 * @param fr AMBE frame as 4x24 bitplanes (modified).
 */
void mbe_demodulateAmbe3600Data_common(char fr[4][24]);

/**
 * @brief Extract 49 parameter bits from C0..C3 with ECC.
 *
 * Copies C0, demodulates C1 and applies Golay(23,12), and copies C2/C3
 * into the 49-bit output parameter vector.
 *
 * @param fr     AMBE frame as 4x24 bitplanes (modified by demodulation).
 * @param out49  Output parameter bits (49 entries).
 * @return Number of corrected bit errors in protected fields.
 */
int mbe_eccAmbe3600Data_common(char fr[4][24], char* out49);

#endif /* MBELIB_NEO_INTERNAL_AMBE_COMMON_H */
