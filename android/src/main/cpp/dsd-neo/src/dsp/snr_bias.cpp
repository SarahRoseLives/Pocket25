// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/dsp/snr_bias.h>

#include <dsd-neo/dsp/demod_state.h>

#include <math.h>

/* SNR estimator bias constants: these are the "pure" estimator biases for the
 * variance-ratio method, independent of noise bandwidth. They correct for the
 * statistical bias of the quartile/median-based clustering approach.
 *
 * The total SNR correction is:
 *   bias_total = bias_estimator + 10*log10(Bn / Rs)
 * where Bn = noise equivalent bandwidth of channel LPF, Rs = symbol rate.
 *
 * These estimator biases were derived by subtracting the nominal bandwidth
 * term from the original empirical calibration values:
 *   C4FM: 7.95 dB - 10*log10(8000/4800) = 7.95 - 2.22 = 5.73 dB
 *   GFSK/QPSK: 2.43 dB - 10*log10(5400/4800) = 2.43 - 0.51 = 1.92 dB
 */
static const double kC4fmEstimatorBiasDb = 5.73;
static const double kEvmEstimatorBiasDb = 1.92;

/* Noise equivalent bandwidth (Hz) for each channel LPF profile.
 * Computed as Bn = (Fs/2) * Σh² / (Σh)² for the 24 kHz reference designs and
 * used as fixed approximations because channel_lpf_* now holds the absolute
 * cutoff constant (8k/3.5k/5.1k/6.25k/5.2k/7.25k) even when Fs changes. */
static const double kNoiseBwWideHz = 8200.0;     /* Wide/analog profile (~8 kHz cutoff) */
static const double kNoiseBw6K25Hz = 3800.0;     /* 6.25 kHz modes (3500 Hz cutoff) */
static const double kNoiseBw12K5Hz = 5500.0;     /* 12.5 kHz modes (5100 Hz cutoff) */
static const double kNoiseBwProvoiceHz = 6500.0; /* ProVoice (6250 Hz cutoff) */
static const double kNoiseBwP25C4fmHz = 5600.0;  /* P25 C4FM (5200 Hz cutoff) */
static const double kNoiseBwP25CqpskHz = 7500.0; /* P25 CQPSK/LSM (7250 Hz cutoff) */

static double
dsd_snr_noise_bandwidth_hz(int lpf_profile) {
    switch (lpf_profile) {
        case DSD_CH_LPF_PROFILE_6K25: return kNoiseBw6K25Hz;
        case DSD_CH_LPF_PROFILE_12K5: return kNoiseBw12K5Hz;
        case DSD_CH_LPF_PROFILE_PROVOICE: return kNoiseBwProvoiceHz;
        case DSD_CH_LPF_PROFILE_P25_C4FM: return kNoiseBwP25C4fmHz;
        case DSD_CH_LPF_PROFILE_P25_CQPSK: return kNoiseBwP25CqpskHz;
        default: return kNoiseBwWideHz;
    }
}

double
dsd_snr_bias_c4fm_db(int rate_out, int ted_sps, int lpf_profile) {
    if (rate_out <= 0 || ted_sps <= 0) {
        return kC4fmEstimatorBiasDb + 2.2; /* fallback to original ~7.95 dB */
    }
    double symbol_rate = (double)rate_out / (double)ted_sps;
    double noise_bw = dsd_snr_noise_bandwidth_hz(lpf_profile);
    /* Total bias = estimator bias + bandwidth penalty */
    return kC4fmEstimatorBiasDb + 10.0 * log10(noise_bw / symbol_rate);
}

double
dsd_snr_bias_evm_db(int rate_out, int ted_sps, int lpf_profile) {
    if (rate_out <= 0 || ted_sps <= 0) {
        return kEvmEstimatorBiasDb + 0.5; /* fallback to original ~2.43 dB */
    }
    double symbol_rate = (double)rate_out / (double)ted_sps;
    double noise_bw = dsd_snr_noise_bandwidth_hz(lpf_profile);
    /* Total bias = estimator bias + bandwidth penalty */
    return kEvmEstimatorBiasDb + 10.0 * log10(noise_bw / symbol_rate);
}
