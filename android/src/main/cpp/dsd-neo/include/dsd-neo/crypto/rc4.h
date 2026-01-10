// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief RC4 keystream helpers.
 *
 * Declares minimal RC4 utilities implemented in `src/crypto/crypt-rc4.c`.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rc4_block_output(int drop, int keylen, int meslen, uint8_t* key, uint8_t* output_blocks);
void rc4_voice_decrypt(int drop, uint8_t keylength, uint8_t messagelength, uint8_t key[], uint8_t cipher[],
                       uint8_t plain[]);
void hytera_enhanced_rc4_setup(dsd_opts* opts, dsd_state* state, unsigned long long int key_value,
                               unsigned long long int mi_value);

#ifdef __cplusplus
}
#endif
