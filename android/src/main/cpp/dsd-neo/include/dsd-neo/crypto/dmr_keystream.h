// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief DMR privacy/keystream helper entrypoints.
 *
 * Declares optional key/keystream generation helpers used by CLI/UI controls.
 */

#pragma once

#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

void tyt_ep_aes_keystream_creation(dsd_state* state, char* input);
void tyt_ap_pc4_keystream_creation(dsd_state* state, char* input);
void retevis_rc2_keystream_creation(dsd_state* state, char* input);
void ken_dmr_scrambler_keystream_creation(dsd_state* state, char* input);
void anytone_bp_keystream_creation(dsd_state* state, char* input);
void straight_mod_xor_keystream_creation(dsd_state* state, char* input);
void tyt16_ambe2_codeword_keystream(dsd_state* state, char ambe_fr[4][24], int fnum);

#ifdef __cplusplus
}
#endif
