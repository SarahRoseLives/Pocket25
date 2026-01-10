// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010 mbelib Author
 * GPG Key ID: 0xEA5EFE2C (9E7A 5527 9CDC EBF7 BF1B  D772 4F98 E863 EA5E FE2C)
 *
 * Portions were originally under the ISC license; this mbelib-neo
 * distribution is provided under GPL-2.0-or-later. See LICENSE for details.
 */
/**
 * @file
 * @brief Internal generator matrices and tables for Golay/Hamming ECC.
 */

#ifndef MBEINT_ECC_CONST_H
#define MBEINT_ECC_CONST_H

/* Declarations only; definitions live in src/ecc/ecc_const.c. */
extern const int hammingGenerator[4];
extern const int imbe7100x4400hammingGenerator[4];
extern const int golayGenerator[12];
extern const int golayMatrix[2048];

#endif /* MBEINT_ECC_CONST_H */
