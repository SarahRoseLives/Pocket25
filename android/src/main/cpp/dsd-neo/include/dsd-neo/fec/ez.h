// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief ezpwd-style Reed-Solomon helpers used by P25 Phase 2.
 *
 * Declares the RS(63,35) and ISCH lookup helpers implemented in `src/fec/ez.cpp`
 * so callers can use them directly.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ez_rs28_ess(int payload[96], int parity[168]);
int ez_rs28_facch(int payload[156], int parity[114]);
int ez_rs28_sacch(int payload[180], int parity[132]);

int ez_rs28_facch_soft(int payload[156], int parity[114], const int* erasures, int n_erasures);
int ez_rs28_sacch_soft(int payload[180], int parity[132], const int* erasures, int n_erasures);
int ez_rs28_ess_soft(int payload[96], int parity[168], const int* erasures, int n_erasures);

int isch_lookup(uint64_t isch);

#ifdef __cplusplus
}
#endif
