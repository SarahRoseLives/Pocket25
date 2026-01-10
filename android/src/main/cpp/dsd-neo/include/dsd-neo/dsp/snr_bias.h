// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute total SNR bias for C4FM (4-level FSK) given DSP parameters.
 *
 * @param rate_out Output sample rate in Hz.
 * @param ted_sps Samples per symbol.
 * @param lpf_profile Channel LPF profile (DSD_CH_LPF_PROFILE_*).
 * @return Total bias in dB to subtract from raw SNR estimate.
 */
double dsd_snr_bias_c4fm_db(int rate_out, int ted_sps, int lpf_profile);

/**
 * @brief Compute total SNR bias for EVM/GFSK/QPSK given DSP parameters.
 *
 * @param rate_out Output sample rate in Hz.
 * @param ted_sps Samples per symbol.
 * @param lpf_profile Channel LPF profile (DSD_CH_LPF_PROFILE_*).
 * @return Total bias in dB to subtract from raw SNR estimate.
 */
double dsd_snr_bias_evm_db(int rate_out, int ted_sps, int lpf_profile);

#ifdef __cplusplus
}
#endif
