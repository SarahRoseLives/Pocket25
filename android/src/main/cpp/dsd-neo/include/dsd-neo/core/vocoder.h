// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Voice/vocoder decode entrypoints.
 *
 * Declares the MBE decode functions implemented in `src/core/vocoder/`.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

void processMbeFrame(dsd_opts* opts, dsd_state* state, char imbe_fr[8][23], char ambe_fr[4][24],
                     char imbe7100_fr[7][24]);
void soft_mbe(dsd_opts* opts, dsd_state* state, char imbe_fr[8][23], char ambe_fr[4][24], char imbe7100_fr[7][24]);
void playMbeFiles(dsd_opts* opts, dsd_state* state, int argc, char** argv);

#ifdef __cplusplus
}
#endif
