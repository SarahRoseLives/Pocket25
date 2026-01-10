// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief RTL-SDR metrics, spectrum diagnostics, and auto-PPM helpers.
 *
 * Houses spectrum/SNR-based auto-PPM supervision state, spectrum and
 * carrier diagnostics, and the public query/toggle helpers used by
 * the UI and protocol code.
 */

#include <dsd-neo/io/rtl_metrics.h>

#include <atomic>
#include <cmath>
#include <dsd-neo/dsp/costas.h>
#include <dsd-neo/dsp/demod_state.h>
#include <string.h>

#include <pffft.h>

/* Spectrum capture and carrier diagnostics shared with RTL orchestrator. */
static const int kSpecMaxN = 1024; /* Max FFT size (power of two) */
float g_spec_db[kSpecMaxN];
std::atomic<int> g_spec_rate_hz{0};
std::atomic<int> g_spec_ready{0};
std::atomic<int> g_spec_N{256}; /* default N */
/* Carrier diagnostics (updated alongside spectrum) */
static std::atomic<double> g_cfo_nco_hz{0.0};
static std::atomic<double> g_resid_cfo_spec_hz{0.0};
static std::atomic<int> g_carrier_lock{0};
static std::atomic<int> g_nco_q15{0};
static std::atomic<int> g_demod_rate_hz{0};
static std::atomic<int> g_costas_err_avg_q14{0};
static std::atomic<double> g_fll_band_edge_freq_rad{0.0}; /* FLL band-edge NCO freq (rad/sample) */

/* Demodulator state (defined in rtl_sdr_fm.cpp) used for CFO/Costas metrics. */
extern demod_state demod;

/* SNR estimates from demod thread (defined in rtl_sdr_fm.cpp). */
extern std::atomic<double> g_snr_c4fm_db;
extern std::atomic<double> g_snr_qpsk_db;
extern std::atomic<double> g_snr_gfsk_db;

/* Supervisory tuner autogain gate (0/1), controlled via env/UI. */
std::atomic<int> g_tuner_autogain_on{0};

/* Auto-PPM status (spectrum-based). */
std::atomic<int> g_auto_ppm_enabled{0};
/* User override for auto-PPM: -1 = follow env/opts; 0 = force off; 1 = force on. */
std::atomic<int> g_auto_ppm_user_en{-1};
std::atomic<int> g_auto_ppm_locked{0};
std::atomic<int> g_auto_ppm_training{0};
std::atomic<int> g_auto_ppm_lock_ppm{0};
std::atomic<double> g_auto_ppm_lock_snr_db{-100.0};
std::atomic<double> g_auto_ppm_lock_df_hz{0.0};
std::atomic<double> g_auto_ppm_snr_db{-100.0};
std::atomic<double> g_auto_ppm_df_hz{0.0};
std::atomic<double> g_auto_ppm_est_ppm{0.0};
std::atomic<int> g_auto_ppm_last_dir{0};
std::atomic<int> g_auto_ppm_cooldown{0};

static inline PFFFT_Setup*
pffft_get_cached_setup(int N) {
    static PFFFT_Setup* setup = nullptr;
    static int setup_N = 0;
    if (!setup || setup_N != N) {
        if (setup) {
            pffft_destroy_setup(setup);
            setup = nullptr;
            setup_N = 0;
        }
        setup = pffft_new_setup(N, PFFFT_COMPLEX);
        setup_N = N;
    }
    return setup;
}

/**
 * @brief Update spectrum, CFO, and SNR exports from an interleaved I/Q block.
 *
 * Copies the most recent samples into a windowed buffer, performs an FFT,
 * smooths the power spectrum, and updates residual CFO and Costas/FLL exports.
 * Also nudges the CQPSK outer loop when appropriate.
 *
 * @param iq_interleaved Interleaved int16_t I/Q samples.
 * @param len_interleaved Number of int16_t elements in `iq_interleaved`.
 * @param out_rate_hz Output sample rate used for CFO scaling.
 */
void
rtl_metrics_update_spectrum_from_iq(const float* iq_interleaved, int len_interleaved, int out_rate_hz) {
    if (!iq_interleaved || len_interleaved < 2) {
        return;
    }
    const int pairs = len_interleaved >> 1;
    /* Use current FFT size; clamp to bounds */
    int N = g_spec_N.load(std::memory_order_relaxed);
    if (N < 64) {
        N = 64;
    }
    if (N > kSpecMaxN) {
        N = kSpecMaxN;
    }
    /* Prepare last N complex samples (I/Q) with DC removal and Hann window */
    alignas(16) static float z[2 * kSpecMaxN];
    int take = (pairs >= N) ? N : pairs;
    int start = (pairs - take);
    double sumI = 0.0;
    double sumQ = 0.0;
    for (int n = 0; n < take; n++) {
        int idx = start + n;
        float I = iq_interleaved[(size_t)(idx << 1) + 0];
        float Q = iq_interleaved[(size_t)(idx << 1) + 1];
        sumI += static_cast<double>(I);
        sumQ += static_cast<double>(Q);
    }
    float meanI = (take > 0) ? static_cast<float>(sumI / static_cast<double>(take)) : 0.0f;
    float meanQ = (take > 0) ? static_cast<float>(sumQ / static_cast<double>(take)) : 0.0f;
    for (int n = 0; n < (N << 1); n++) {
        z[n] = 0.0f;
    }
    for (int n = 0; n < take; n++) {
        float w =
            0.5f * (1.0f - cosf(2.0f * static_cast<float>(M_PI) * static_cast<float>(n) / static_cast<float>(N - 1)));
        int idx = start + n;
        float I = iq_interleaved[(size_t)(idx << 1) + 0];
        float Q = iq_interleaved[(size_t)(idx << 1) + 1];
        z[(n << 1) + 0] = w * (static_cast<float>(I) - meanI);
        z[(n << 1) + 1] = w * (static_cast<float>(Q) - meanQ);
    }
    auto update_spectrum = [&](auto&& re_at, auto&& im_at) {
        const float eps = 1e-12f;
        const bool first = (g_spec_ready.load(std::memory_order_relaxed) == 0);
        for (int k = 0; k < N; k++) {
            int kk = k + (N >> 1);
            if (kk >= N) {
                kk -= N;
            }
            float re = re_at(kk);
            float im = im_at(kk);
            float mag2 = re * re + im * im;
            float db = 10.0f * log10f(mag2 + eps);
            float prev = g_spec_db[k];
            if (first) {
                g_spec_db[k] = db;
            } else {
                g_spec_db[k] = 0.8f * prev + 0.2f * db;
            }
        }
        g_spec_rate_hz.store(out_rate_hz, std::memory_order_relaxed);
        g_spec_ready.store(1, std::memory_order_release);
    };

    PFFFT_Setup* setup = pffft_get_cached_setup(N);
    if (setup) {
        pffft_transform_ordered(setup, z, z, nullptr, PFFFT_FORWARD);
        update_spectrum([&](int idx) { return z[(idx << 1) + 0]; }, [&](int idx) { return z[(idx << 1) + 1]; });
    }
    /* Compute residual CFO from spectrum peak around DC using quadratic interp. */
    int i_max = 0;
    float p_max = -1e30f;
    for (int k = 0; k < N; k++) {
        float v = g_spec_db[k];
        if (v > p_max) {
            p_max = v;
            i_max = k;
        }
    }
    double df_spec_hz = 0.0;
    if (N >= 3 && i_max > 0 && i_max + 1 < N) {
        double p1 = g_spec_db[i_max - 1];
        double p2 = g_spec_db[i_max + 0];
        double p3 = g_spec_db[i_max + 1];
        double denom = (p1 - 2.0 * p2 + p3);
        double delta = 0.0;
        if (fabs(denom) > 1e-9) {
            delta = 0.5 * (p1 - p3) / denom;
            if (delta < -0.5) {
                delta = -0.5;
            }
            if (delta > +0.5) {
                delta = +0.5;
            }
        }
        double center = static_cast<double>(N) / 2.0;
        double k_off = (static_cast<double>(i_max) + delta) - center;
        df_spec_hz = (out_rate_hz > 0) ? (k_off * static_cast<double>(out_rate_hz) / static_cast<double>(N)) : 0.0;
    }
    g_resid_cfo_spec_hz.store(df_spec_hz, std::memory_order_relaxed);

    /* NCO CFO from Costas/FLL (native float freq in rad/sample, scaled by Fs/(2π))
     *
     * For CQPSK (OP25-compatible flow), the total CFO is the sum of:
     *   1. FLL band-edge frequency (coarse, at sample rate) - fll_band_edge_state.freq
     *   2. Costas frequency (fine, at symbol rate) - costas_state.freq
     *
     * The Costas operates at symbol rate (Fs/sps), so its frequency must be scaled
     * by (1/sps) to convert to sample-rate equivalent before adding to FLL freq.
     *
     * For non-CQPSK modes, we use the legacy fll_freq field.
     */
    double cfo_hz = 0.0;
    float total_freq_rad = 0.0f; /* Total NCO freq in rad/sample for legacy Q15 metric */
    if (out_rate_hz > 0) {
        if (demod.cqpsk_enable) {
            /* CQPSK: Combine FLL band-edge and Costas frequencies.
             * FLL freq is at sample rate, Costas freq is at symbol rate.
             * Costas freq (rad/symbol) * (1/sps) = rad/sample equivalent.
             */
            float fll_freq = demod.fll_band_edge_state.freq; /* rad/sample */
            float costas_freq = demod.costas_state.freq;     /* rad/symbol */
            int sps = demod.ted_sps > 0 ? demod.ted_sps : 5;

            /* Convert Costas freq from rad/symbol to rad/sample */
            float costas_freq_sample_rate = costas_freq / static_cast<float>(sps);

            total_freq_rad = fll_freq + costas_freq_sample_rate;
            cfo_hz = static_cast<double>(total_freq_rad) * static_cast<double>(out_rate_hz) / (2.0 * M_PI);

            /* Also update legacy fll_freq/fll_phase for any code that still reads them */
            demod.fll_freq = total_freq_rad;
            demod.fll_phase = demod.fll_band_edge_state.phase + demod.costas_state.phase;
        } else {
            /* Non-CQPSK: Use legacy FLL state */
            total_freq_rad = demod.fll_freq;
            cfo_hz = static_cast<double>(demod.fll_freq) * static_cast<double>(out_rate_hz) / (2.0 * M_PI);
        }
    }
    g_cfo_nco_hz.store(cfo_hz, std::memory_order_relaxed);
    /* Store native float freq as legacy Q15 for backwards-compatible metrics */
    int fll_freq_q15_compat = static_cast<int>(lrint(total_freq_rad * (32768.0 / (2.0 * M_PI))));
    g_nco_q15.store(fll_freq_q15_compat, std::memory_order_relaxed);
    g_demod_rate_hz.store(out_rate_hz, std::memory_order_relaxed);
    g_costas_err_avg_q14.store(demod.costas_err_avg_q14, std::memory_order_relaxed);
    /* Store FLL band-edge freq for UI access (avoid data race on demod state) */
    g_fll_band_edge_freq_rad.store(static_cast<double>(demod.fll_band_edge_state.freq), std::memory_order_relaxed);

    /* Spectrum-assisted CFO correction for CQPSK:
     * When CQPSK path and FLL are enabled, and we see a reasonably strong
     * QPSK signal, use the residual CFO estimate from the spectrum to gently
     * nudge the FLL NCO toward zero residual. This acts as a slow outer loop
     * around the symbol-domain Costas, improving pull-in when residual
     * CFO is outside its comfort zone.
     *
     * To avoid loop fighting and NCO oscillation when the inner Costas
     * is already close to lock, we:
     *   - Ignore very small residuals near DC.
     *   - Use a conservative outer-loop gain.
     *
     * For the OP25-compatible flow, we nudge fll_band_edge_state.freq (the coarse
     * frequency at sample rate). The legacy fll_freq is updated as a side effect
     * for backwards compatibility with any code that reads it directly.
     */
    if (demod.cqpsk_enable && demod.fll_enabled && out_rate_hz > 0) {
        double snr_qpsk = g_snr_qpsk_db.load(std::memory_order_relaxed);
        double abs_df = fabs(df_spec_hz);
        /* Gate: require reasonable SNR and ignore wildly off/tiny residuals. */
        const double k_df_min = 150.0; /* Hz: ignore residuals below ~150 Hz to reduce jitter */
        const double k_df_max = 2500.0;
        if (snr_qpsk > -3.0 && abs_df > k_df_min && abs_df < k_df_max) {
            /* Outer-loop gain: fraction of residual per update. */
            const double k_outer = 0.05;
            /* Residual is after NCO; increase NCO CFO toward signal CFO.
             * Convert residual Hz to rad/sample: delta_rad = df_hz * 2π / Fs */
            double delta_rad = k_outer * df_spec_hz * 2.0 * M_PI / static_cast<double>(out_rate_hz);
            if (fabs(delta_rad) > 1e-9) {
                const float F_CLAMP = 0.25f; /* ~±0.25 rad/sample max CFO */

                /* For OP25 flow, nudge the FLL band-edge state directly */
                float f_old = demod.fll_band_edge_state.freq;
                float f_new = f_old + static_cast<float>(delta_rad);
                if (f_new > F_CLAMP) {
                    f_new = F_CLAMP;
                }
                if (f_new < -F_CLAMP) {
                    f_new = -F_CLAMP;
                }
                demod.fll_band_edge_state.freq = f_new;

                /* Also update legacy fll_freq for backwards compatibility.
                 * Combine FLL band-edge + Costas (scaled to sample rate). */
                int sps = demod.ted_sps > 0 ? demod.ted_sps : 5;
                float costas_freq_sample_rate = demod.costas_state.freq / static_cast<float>(sps);
                total_freq_rad = f_new + costas_freq_sample_rate;
                demod.fll_freq = total_freq_rad;

                /* Also nudge legacy fll_state for non-OP25 paths that may read it */
                float delta_applied = f_new - f_old;
                float i_old = demod.fll_state.integrator;
                float i_new = i_old + delta_applied;
                if (i_new > F_CLAMP) {
                    i_new = F_CLAMP;
                }
                if (i_new < -F_CLAMP) {
                    i_new = -F_CLAMP;
                }
                demod.fll_state.integrator = i_new;

                /* Store native float freq as legacy Q15 for backwards-compatible metrics */
                int fll_q15_compat = static_cast<int>(lrint(total_freq_rad * (32768.0 / (2.0 * M_PI))));
                g_nco_q15.store(fll_q15_compat, std::memory_order_relaxed);
                /* Recompute NCO CFO export after adjustment */
                cfo_hz = static_cast<double>(total_freq_rad) * static_cast<double>(out_rate_hz) / (2.0 * M_PI);
                g_cfo_nco_hz.store(cfo_hz, std::memory_order_relaxed);
            }
        }
    }

    /* Lock heuristic for CQPSK: based on SNR (Costas loop doesn't expose a direct lock flag).
     * The spectrum-based df_spec_hz is not meaningful for CQPSK since the Costas NCO
     * corrects carrier offset internally. Use SNR threshold instead. */
    int locked = 0;
    if (demod.cqpsk_enable) {
        double snr = g_snr_qpsk_db.load(std::memory_order_relaxed);
        /* Lock when SNR indicates good signal quality */
        if (snr > 6.0) {
            locked = 1;
        }
    }
    g_carrier_lock.store(locked, std::memory_order_relaxed);
}

/* Spectrum and carrier diagnostics query helpers. */
/**
 * @brief Copy the smoothed spectrum into the caller's buffer.
 *
 * Returns the number of bins copied (clamped to both the configured FFT size
 * and `max_bins`). Optionally returns the current sample rate used for the FFT.
 *
 * @param out_db   [out] Destination buffer for power values in dB.
 * @param max_bins Maximum bins to copy.
 * @param out_rate [out] Optional sample rate in Hz.
 * @return Number of bins copied (0 if not ready or on invalid input).
 */
extern "C" int
dsd_rtl_stream_spectrum_get(float* out_db, int max_bins, int* out_rate) {
    if (!out_db || max_bins <= 0) {
        return 0;
    }
    if (g_spec_ready.load(std::memory_order_acquire) == 0) {
        return 0;
    }
    int N = g_spec_N.load(std::memory_order_relaxed);
    if (N < 64) {
        N = 64;
    }
    if (N > kSpecMaxN) {
        N = kSpecMaxN;
    }
    int n = (max_bins < N) ? max_bins : N;
    for (int i = 0; i < n; i++) {
        out_db[i] = g_spec_db[i];
    }
    if (out_rate) {
        *out_rate = g_spec_rate_hz.load(std::memory_order_relaxed);
    }
    return n;
}

/**
 * @brief Configure the FFT size used for spectrum exports.
 *
 * The size is clamped to [64, kSpecMaxN] and rounded up to the next power of
 * two for the pffft complex transform.
 *
 * @param n Requested FFT length (bins).
 * @return Actual FFT length selected.
 */
extern "C" int
dsd_rtl_stream_spectrum_set_size(int n) {
    if (n < 64) {
        n = 64;
    }
    if (n > kSpecMaxN) {
        n = kSpecMaxN;
    }
    int p = 64;
    while (p < n) {
        p <<= 1;
    }
    if (p > kSpecMaxN) {
        p = kSpecMaxN;
    }
    g_spec_N.store(p, std::memory_order_relaxed);
    return p;
}

/**
 * @brief Get the current FFT size used for spectrum exports.
 * @return FFT length in bins.
 */
extern "C" int
dsd_rtl_stream_spectrum_get_size(void) {
    int N = g_spec_N.load(std::memory_order_relaxed);
    if (N < 64) {
        N = 64;
    }
    if (N > kSpecMaxN) {
        N = kSpecMaxN;
    }
    return N;
}

/** @brief Return the current NCO CFO estimate in Hz derived from Costas/FLL. */
extern "C" double
dsd_rtl_stream_get_cfo_hz(void) {
    return g_cfo_nco_hz.load(std::memory_order_relaxed);
}

/** @brief Return the residual CFO estimate in Hz derived from the spectrum peak. */
extern "C" double
dsd_rtl_stream_get_residual_cfo_hz(void) {
    return g_resid_cfo_spec_hz.load(std::memory_order_relaxed);
}

/** @brief Return 1 when the CQPSK carrier lock heuristic is satisfied. */
extern "C" int
dsd_rtl_stream_get_carrier_lock(void) {
    return g_carrier_lock.load(std::memory_order_relaxed) ? 1 : 0;
}

/** @brief Get the current FLL/Costas NCO frequency in Q15 cycles/sample. */
extern "C" int
dsd_rtl_stream_get_nco_q15(void) {
    return g_nco_q15.load(std::memory_order_relaxed);
}

/** @brief Get the demodulator sample rate (Hz) used for CFO scaling. */
extern "C" int
dsd_rtl_stream_get_demod_rate_hz(void) {
    return g_demod_rate_hz.load(std::memory_order_relaxed);
}

/** @brief Get the smoothed Costas error term (Q14). */
extern "C" int
dsd_rtl_stream_get_costas_err_q14(void) {
    return g_costas_err_avg_q14.load(std::memory_order_relaxed);
}

/** @brief Return the FLL band-edge frequency estimate in Hz. */
extern "C" double
dsd_rtl_stream_get_fll_band_edge_freq_hz(void) {
    int Fs = g_demod_rate_hz.load(std::memory_order_relaxed);
    if (Fs <= 0) {
        return 0.0;
    }
    /* FLL band-edge freq is in rad/sample; convert to Hz: f_hz = freq * Fs / (2π).
     * Read from atomic to avoid data race with demod thread. */
    double freq_rad = g_fll_band_edge_freq_rad.load(std::memory_order_relaxed);
    return freq_rad * static_cast<double>(Fs) / (2.0 * M_PI);
}

/**
 * @brief Reset Costas loop state for fresh carrier acquisition on retune.
 *
 * Clears the Costas phase and error estimates, but PRESERVES the frequency
 * estimate. The carrier frequency offset is primarily a property of the RTL-SDR
 * local oscillator, not the channel. Preserving freq allows immediate tracking
 * on channel changes rather than slewing from 0 Hz.
 *
 * Also resets the differential phasor history to (1,0) so the first sample's
 * diff output equals raw input.
 */
extern "C" void
dsd_rtl_stream_reset_costas(void) {
    /* Reset phase and error, but preserve frequency estimate */
    demod.costas_state.phase = 0.0f;
    demod.costas_state.error = 0.0f;
    /* Note: deliberately NOT zeroing costas_state.freq - preserve it! */

    /* Reset differential decode history to (1,0) not (0,0).
     * When prev is (0,0), the first diff decode produces zero output,
     * which corrupts the Costas phase error and causes the loop to hunt.
     * Using (1,0) means the first sample's diff output equals raw input. */
    demod.cqpsk_diff_prev_r = 1.0f;
    demod.cqpsk_diff_prev_j = 0.0f;
}

/* Smoothed SNR exports (for UI and protocol code). */
/** @brief Get the smoothed C4FM SNR estimate in dB (negative when unavailable). */
extern "C" double
rtl_stream_get_snr_c4fm(void) {
    return g_snr_c4fm_db.load(std::memory_order_relaxed);
}

/** @brief Get the smoothed CQPSK SNR estimate in dB (negative when unavailable). */
extern "C" double
rtl_stream_get_snr_cqpsk(void) {
    return g_snr_qpsk_db.load(std::memory_order_relaxed);
}

/** @brief Get the smoothed GFSK SNR estimate in dB (negative when unavailable). */
extern "C" double
rtl_stream_get_snr_gfsk(void) {
    return g_snr_gfsk_db.load(std::memory_order_relaxed);
}

/* Supervisory tuner autogain runtime control */
/** @brief Return the supervisory tuner auto-gain flag (1=enabled). */
extern "C" int
dsd_rtl_stream_get_tuner_autogain(void) {
    return g_tuner_autogain_on.load(std::memory_order_relaxed) ? 1 : 0;
}

/** @brief Enable or disable supervisory tuner auto-gain (atomic flag). */
extern "C" void
dsd_rtl_stream_set_tuner_autogain(int onoff) {
    g_tuner_autogain_on.store(onoff ? 1 : 0, std::memory_order_relaxed);
}

/**
 * @brief Snapshot auto-PPM supervision status.
 *
 * Fills out the optional output parameters with the latest SNR, frequency
 * offset, estimate, direction, cooldown, and lock status.
 *
 * @param enabled  [out] Whether auto-PPM is currently enabled.
 * @param snr_db   [out] Current SNR estimate in dB.
 * @param df_hz    [out] Current frequency offset estimate in Hz.
 * @param est_ppm  [out] Current PPM estimate applied.
 * @param last_dir [out] Most recent adjustment direction (-1/0/1).
 * @param cooldown [out] Remaining cooldown ticks before next adjustment.
 * @param locked   [out] Whether a stable PPM lock is latched.
 * @return 0 always (snapshot only).
 */
int
dsd_rtl_stream_auto_ppm_get_status(int* enabled, double* snr_db, double* df_hz, double* est_ppm, int* last_dir,
                                   int* cooldown, int* locked) {
    if (enabled) {
        *enabled = g_auto_ppm_enabled.load(std::memory_order_relaxed);
    }
    if (snr_db) {
        *snr_db = g_auto_ppm_snr_db.load(std::memory_order_relaxed);
    }
    if (df_hz) {
        *df_hz = g_auto_ppm_df_hz.load(std::memory_order_relaxed);
    }
    if (est_ppm) {
        *est_ppm = g_auto_ppm_est_ppm.load(std::memory_order_relaxed);
    }
    if (last_dir) {
        *last_dir = g_auto_ppm_last_dir.load(std::memory_order_relaxed);
    }
    if (cooldown) {
        *cooldown = g_auto_ppm_cooldown.load(std::memory_order_relaxed);
    }
    if (locked) {
        *locked = g_auto_ppm_locked.load(std::memory_order_relaxed);
    }
    return 0;
}

/** @brief Return 1 if the spectrum-based auto-PPM training window is active. */
int
dsd_rtl_stream_auto_ppm_training_active(void) {
    return g_auto_ppm_training.load(std::memory_order_relaxed) ? 1 : 0;
}

/**
 * @brief Retrieve the last locked auto-PPM correction and supporting metrics.
 *
 * @param ppm    [out] Locked PPM value.
 * @param snr_db [out] SNR at lock time in dB.
 * @param df_hz  [out] Residual frequency offset at lock time in Hz.
 * @return 0 always (snapshot only).
 */
int
dsd_rtl_stream_auto_ppm_get_lock(int* ppm, double* snr_db, double* df_hz) {
    if (ppm) {
        *ppm = g_auto_ppm_lock_ppm.load(std::memory_order_relaxed);
    }
    if (snr_db) {
        *snr_db = g_auto_ppm_lock_snr_db.load(std::memory_order_relaxed);
    }
    if (df_hz) {
        *df_hz = g_auto_ppm_lock_df_hz.load(std::memory_order_relaxed);
    }
    return 0;
}

/**
 * @brief Force-enable or disable auto-PPM (user override).
 * @param onoff Non-zero to enable; zero to disable.
 */
void
dsd_rtl_stream_set_auto_ppm(int onoff) {
    g_auto_ppm_user_en.store(onoff ? 1 : 0, std::memory_order_relaxed);
}

/**
 * @brief Get the effective auto-PPM enable flag after user overrides.
 * @return 1 when enabled, 0 when disabled.
 */
int
dsd_rtl_stream_get_auto_ppm(void) {
    int u = g_auto_ppm_user_en.load(std::memory_order_relaxed);
    if (u == 0) {
        return 0;
    }
    if (u == 1) {
        return 1;
    }
    return g_auto_ppm_enabled.load(std::memory_order_relaxed) ? 1 : 0;
}
