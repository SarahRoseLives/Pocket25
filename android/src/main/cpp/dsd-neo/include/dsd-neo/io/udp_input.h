// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief UDP PCM16LE input backend.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start UDP input on the given bind address/port.
 *
 * Binds a UDP socket, spawns the input thread, and begins queuing PCM16LE
 * samples into the input ring.
 *
 * @param opts Decoder options (receives UDP context).
 * @param bindaddr Bind address (e.g., "0.0.0.0" or "::").
 * @param port UDP port number.
 * @param samplerate Expected sample rate in Hz.
 * @return 0 on success; negative on error.
 */
int udp_input_start(dsd_opts* opts, const char* bindaddr, int port, int samplerate);

/** @brief Stop UDP input and free resources. */
void udp_input_stop(dsd_opts* opts);

/**
 * @brief Blocking read of one PCM16 sample from UDP ring.
 *
 * @param opts Decoder options containing UDP context.
 * @param out [out] Receives one sample.
 * @return 1 on success, 0 on shutdown.
 */
int udp_input_read_sample(dsd_opts* opts, int16_t* out);

#ifdef __cplusplus
}
#endif
