// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 *
 * Internal header for adaptive smoothing algorithms.
 * Implements JMBE Algorithms #111-116 for error-based audio smoothing.
 */

#ifndef MBEINT_MBE_ADAPTIVE_H
#define MBEINT_MBE_ADAPTIVE_H

#include "mbelib-neo/mbelib.h"

/* Algorithm constants from JMBE specification */

/** Default local energy value (Algorithm #111). */
#define MBE_DEFAULT_LOCAL_ENERGY        75000.0f

/** Minimum local energy threshold (Algorithm #111). */
#define MBE_MIN_LOCAL_ENERGY            10000.0f

/** Energy smoothing coefficient alpha (Algorithm #111). */
#define MBE_ENERGY_SMOOTH_ALPHA         0.95f

/** Energy smoothing coefficient beta (Algorithm #111). */
#define MBE_ENERGY_SMOOTH_BETA          0.05f

/** Default amplitude threshold (Algorithm #115). */
#define MBE_DEFAULT_AMPLITUDE_THRESHOLD 20480

/** Error rate threshold for smoothing entry (Algorithm #112). */
#define MBE_ERROR_THRESHOLD_ENTRY       0.0125f

/** Low error rate threshold (Algorithm #112). */
#define MBE_ERROR_THRESHOLD_LOW         0.005f

/** Adaptive gain constant (Algorithm #112). */
#define MBE_ADAPTIVE_GAIN               45.255f

/** Adaptive exponent constant (Algorithm #112). */
#define MBE_ADAPTIVE_EXPONENT           277.26f

/** Alternative adaptive multiplier (Algorithm #112). */
#define MBE_ADAPTIVE_ALT                1.414f

/** Amplitude penalty per error (Algorithm #115). */
#define MBE_AMPLITUDE_PENALTY_PER_ERROR 300

/** Amplitude base constant (Algorithm #115). */
#define MBE_AMPLITUDE_BASE              6000

#endif /* MBEINT_MBE_ADAPTIVE_H */
