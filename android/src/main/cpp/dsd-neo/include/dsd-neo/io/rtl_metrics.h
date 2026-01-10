// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief RTL-SDR metrics and auto-PPM helpers.
 *
 * Exposes auto-PPM supervision state and a helper used by the RTL stream
 * read path to perform spectrum/SNR-based PPM adjustments.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Get auto-PPM status snapshot; returns 0 on success. */
int dsd_rtl_stream_auto_ppm_get_status(int* enabled, double* snr_db, double* df_hz, double* est_ppm, int* last_dir,
                                       int* cooldown, int* locked);

/** @brief Return 1 if auto-PPM training window is active. */
int dsd_rtl_stream_auto_ppm_training_active(void);

/** @brief Get locked auto-PPM value and lock-time snapshot; returns 0 on success. */
int dsd_rtl_stream_auto_ppm_get_lock(int* ppm, double* snr_db, double* df_hz);

/** @brief Enable/disable auto-PPM runtime helper. */
void dsd_rtl_stream_set_auto_ppm(int onoff);

/** @brief Return current auto-PPM enable flag. */
int dsd_rtl_stream_get_auto_ppm(void);

/**
 * @brief Update spectrum/SNR metrics from a block of interleaved I/Q.
 *
 * Used by the demod thread to feed the auto-PPM estimator.
 */
void rtl_metrics_update_spectrum_from_iq(const float* iq_interleaved, int len_interleaved, int out_rate_hz);

#ifdef __cplusplus
}
#endif
