// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Mean power and dB conversion helpers.
 *
 * These helpers are used by the RTL squelch/VOX paths and UI displays.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

double raw_rms(const short* samples, int len, int step);
double raw_pwr(const short* samples, int len, int step);
double raw_pwr_f(const float* samples, int len, int step);
double pwr_to_dB(double mean_power);
double dB_to_pwr(double dB);

#ifdef __cplusplus
}
#endif
