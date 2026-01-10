// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Audio-domain filter helpers.
 *
 * Declares analog monitor and audio utility filters implemented in core/util.
 */

#pragma once

#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_audio_filters(dsd_state* state, int sample_rate_hz);
void lpf(dsd_state* state, short* input, int len);
void lpf_f(dsd_state* state, float* input, int len);
void hpf(dsd_state* state, short* input, int len);
void hpf_f(dsd_state* state, float* input, int len);
void hpf_dL(dsd_state* state, short* input, int len);
void hpf_dR(dsd_state* state, short* input, int len);
void pbf(dsd_state* state, short* input, int len);
void pbf_f(dsd_state* state, float* input, int len);

#ifdef __cplusplus
}
#endif
