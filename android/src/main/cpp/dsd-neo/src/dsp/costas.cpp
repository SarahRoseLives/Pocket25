// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Gardner symbol recovery block for GR - Copyright 2010, 2011, 2012, 2013, 2014, 2015 KA1RBI
 * Costas loop for carrier recovery - Copyright 2006,2010-2012 Free Software Foundation, Inc.
 * Lock detector based on Yair Linn's research - Copyright 2022 gnorbury@bondcar.com
 * Port to dsd-neo - Copyright (C) 2026 by arancormonk
 *
 * This is a direct port of OP25's CQPSK signal chain:
 *   AGC -> Gardner (timing only) -> diff_phasor -> Costas (at symbol rate)
 *
 * The key insight from OP25 (p25_demodulator_dev.py line 486):
 *   self.connect(self.if_out, self.agc, self.fll, self.clock, self.diffdec, self.costas, ...)
 *
 * Where:
 *   - clock = op25_repeater.gardner_cc (timing recovery only, NO carrier tracking)
 *   - diffdec = digital.diff_phasor_cc (differential decoding at symbol rate)
 *   - costas = op25_repeater.costas_loop_cc (carrier tracking at symbol rate)
 */

#include <dsd-neo/dsp/costas.h>
#include <dsd-neo/dsp/demod_state.h>
#include <dsd-neo/runtime/config.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kPi = 3.14159265358979323846f;

static inline bool
debug_cqpsk_enabled(void) {
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (!cfg) {
        dsd_neo_config_init(NULL);
        cfg = dsd_neo_get_config();
    }
    return cfg && cfg->debug_cqpsk_enable;
}

/* MMSE interpolator parameters - match OP25/GNU Radio */
#define MMSE_NTAPS  8
#define MMSE_NSTEPS 16

/* GNU Radio MMSE 8-tap polyphase interpolator coefficients (subset at 1/16 steps) */
static const float mmse_taps[MMSE_NSTEPS + 1][MMSE_NTAPS] = {
    /* Row 0/128 (mu=0): output = sample[4] */
    {0.00000e+00f, 0.00000e+00f, 0.00000e+00f, 0.00000e+00f, 1.00000e+00f, 0.00000e+00f, 0.00000e+00f, 0.00000e+00f},
    /* Row 8/128 (mu=0.0625) */
    {-1.23337e-03f, 6.84261e-03f, -2.24178e-02f, 6.57852e-02f, 9.83392e-01f, -4.04519e-02f, 9.56876e-03f,
     -1.54221e-03f},
    /* Row 16/128 (mu=0.125) */
    {-2.43121e-03f, 1.35716e-02f, -4.49929e-02f, 1.36968e-01f, 9.55956e-01f, -7.43154e-02f, 1.80759e-02f,
     -2.94361e-03f},
    /* Row 24/128 (mu=0.1875) */
    {-3.55283e-03f, 1.99599e-02f, -6.70018e-02f, 2.12443e-01f, 9.18329e-01f, -1.01501e-01f, 2.53295e-02f,
     -4.16581e-03f},
    /* Row 32/128 (mu=0.25) */
    {-4.55932e-03f, 2.57844e-02f, -8.77011e-02f, 2.91006e-01f, 8.71305e-01f, -1.22047e-01f, 3.11866e-02f,
     -5.17776e-03f},
    /* Row 40/128 (mu=0.3125) */
    {-5.41467e-03f, 3.08323e-02f, -1.06342e-01f, 3.71376e-01f, 8.15826e-01f, -1.36111e-01f, 3.55525e-02f,
     -5.95620e-03f},
    /* Row 48/128 (mu=0.375) */
    {-6.08674e-03f, 3.49066e-02f, -1.22185e-01f, 4.52218e-01f, 7.52958e-01f, -1.43968e-01f, 3.83800e-02f,
     -6.48585e-03f},
    /* Row 56/128 (mu=0.4375) */
    {-6.54823e-03f, 3.78315e-02f, -1.34515e-01f, 5.32164e-01f, 6.83875e-01f, -1.45993e-01f, 3.96678e-02f,
     -6.75943e-03f},
    /* Row 64/128 (mu=0.5): midpoint */
    {-6.77751e-03f, 3.94578e-02f, -1.42658e-01f, 6.09836e-01f, 6.09836e-01f, -1.42658e-01f, 3.94578e-02f,
     -6.77751e-03f},
    /* Row 72/128 (mu=0.5625) */
    {-6.73929e-03f, 3.95900e-02f, -1.46043e-01f, 6.92808e-01f, 5.22267e-01f, -1.33190e-01f, 3.75341e-02f,
     -6.50285e-03f},
    /* Row 80/128 (mu=0.625) */
    {-6.48585e-03f, 3.83800e-02f, -1.43968e-01f, 7.52958e-01f, 4.52218e-01f, -1.22185e-01f, 3.49066e-02f,
     -6.08674e-03f},
    /* Row 88/128 (mu=0.6875) */
    {-5.95620e-03f, 3.55525e-02f, -1.36111e-01f, 8.15826e-01f, 3.71376e-01f, -1.06342e-01f, 3.08323e-02f,
     -5.41467e-03f},
    /* Row 96/128 (mu=0.75) */
    {-5.17776e-03f, 3.11866e-02f, -1.22047e-01f, 8.71305e-01f, 2.91006e-01f, -8.77011e-02f, 2.57844e-02f,
     -4.55932e-03f},
    /* Row 104/128 (mu=0.8125) */
    {-4.16581e-03f, 2.53295e-02f, -1.01501e-01f, 9.18329e-01f, 2.12443e-01f, -6.70018e-02f, 1.99599e-02f,
     -3.55283e-03f},
    /* Row 112/128 (mu=0.875) */
    {-2.94361e-03f, 1.80759e-02f, -7.43154e-02f, 9.55956e-01f, 1.36968e-01f, -4.49929e-02f, 1.35716e-02f,
     -2.43121e-03f},
    /* Row 120/128 (mu=0.9375) */
    {-1.54221e-03f, 9.56876e-03f, -4.04519e-02f, 9.83392e-01f, 6.57852e-02f, -2.24178e-02f, 6.84261e-03f,
     -1.23337e-03f},
    /* Row 128/128 (mu=1.0): output = sample[3] */
    {0.00000e+00f, 0.00000e+00f, 0.00000e+00f, 1.00000e+00f, 0.00000e+00f, 0.00000e+00f, 0.00000e+00f, 0.00000e+00f},
};

/* 8-tap MMSE FIR interpolation */
static inline float
mmse_interp_8tap(const float* s, float mu) {
    float idx_f = mu * (float)MMSE_NSTEPS;
    int idx_lo = (int)idx_f;
    float frac = idx_f - (float)idx_lo;

    if (idx_lo < 0) {
        idx_lo = 0;
        frac = 0.0f;
    }
    if (idx_lo >= MMSE_NSTEPS) {
        idx_lo = MMSE_NSTEPS - 1;
        frac = 1.0f;
    }
    int idx_hi = idx_lo + 1;

    const float* taps_lo = mmse_taps[idx_lo];
    const float* taps_hi = mmse_taps[idx_hi];
    float one_minus_frac = 1.0f - frac;

    float acc = 0.0f;
    for (int k = 0; k < MMSE_NTAPS; k++) {
        float tap = one_minus_frac * taps_lo[k] + frac * taps_hi[k];
        acc += tap * s[k];
    }
    return acc;
}

/* Complex MMSE interpolation */
static inline void
mmse_interp_cc(const float* dl, float mu, float* out_r, float* out_j) {
    float sr[MMSE_NTAPS];
    float sj[MMSE_NTAPS];

    for (int k = 0; k < MMSE_NTAPS; k++) {
        const size_t kk = (size_t)k;
        sr[k] = dl[kk * 2];
        sj[k] = dl[kk * 2 + 1];
    }

    *out_r = mmse_interp_8tap(sr, mu);
    *out_j = mmse_interp_8tap(sj, mu);
}

/* Branchless clip (matches GNU Radio) */
static inline float
branchless_clip(float x, float limit) {
    float a = x + limit;
    float b = x - limit;
    if (a < 0.0f) {
        a = 0.0f;
    }
    if (b > 0.0f) {
        b = 0.0f;
    }
    return 0.5f * (a + b);
}

/*
 * OP25 QPSK phase error detector.
 *
 * From costas_loop_cc_impl.cc phase_detector_4():
 *   return ((sample.real() > 0 ? 1.0 : -1.0) * sample.imag() -
 *           (sample.imag() > 0 ? 1.0 : -1.0) * sample.real());
 *
 * This expects symbols at axis-aligned positions (0°, 90°, 180°, 270°).
 */
static inline float
phase_detector_4(float real, float imag) {
    return ((real > 0.0f ? 1.0f : -1.0f) * imag - (imag > 0.0f ? 1.0f : -1.0f) * real);
}

/* NaN check helper */
#define IS_NAN(x) ((x) != (x))

} // namespace

/*
 * Reset Costas loop state for fresh carrier acquisition on retune.
 *
 * Per OP25's costas_reset() in p25_demodulator_dev.py:574-576:
 *   self.costas.set_frequency(0)
 *   self.costas.set_phase(0)
 *
 * Unlike the old combined block, we now reset BOTH phase and frequency
 * because the Costas loop operates at symbol rate after diff_phasor,
 * and there's no shared state with Gardner.
 */
extern "C" void
dsd_costas_reset(dsd_costas_loop_state_t* c) {
    if (!c) {
        return;
    }
    c->phase = 0.0f;
    c->freq = 0.0f;
    c->error = 0.0f;
    c->initialized = 0;
}

/*
 * OP25-compatible Gardner timing recovery block.
 *
 * Direct port of OP25's gardner_cc_impl::general_work() from:
 *   op25/gr-op25_repeater/lib/gardner_cc_impl.cc
 *
 * This is PURE timing recovery - NO carrier tracking, NO NCO rotation.
 * The carrier is tracked separately by the downstream Costas loop.
 *
 * Signal flow:
 *   Input: AGC'd complex samples at sample rate
 *   Processing:
 *     1. Push samples to circular delay line
 *     2. When mu accumulates past 1.0, interpolate symbol and mid-symbol
 *     3. Compute Gardner error: (last - current) * mid
 *     4. Update omega and mu
 *     5. Update lock detector (Yair Linn method)
 *   Output: Symbol-rate complex samples (timing corrected, NOT carrier corrected)
 *
 * Key differences from old combined block:
 *   - NO NCO rotation applied to input samples
 *   - NO phase error computation or Costas tracking
 *   - Output is raw symbols, not carrier-corrected
 */
extern "C" void
op25_gardner_cc(struct demod_state* d) {
    if (!d || !d->lowpassed || d->lp_len < 2) {
        return;
    }
    if (!d->cqpsk_enable) {
        return;
    }

    ted_state_t* ted = &d->ted_state;

    const int buf_len = d->lp_len;
    const int nc = buf_len >> 1; /* input complex samples */
    if (nc < 4) {
        return;
    }

    /* Get TED parameters */
    const int sps = d->ted_sps > 0 ? d->ted_sps : 5;
    float omega = ted->omega;

    /*
     * Reinitialize TED state when SPS changes (e.g., P25P1 CC 5 sps -> P25P2 VC 4 sps).
     *
     * This mirrors OP25's set_omega() behavior.
     */
    int need_reinit = (ted->omega_mid == 0.0f || ted->twice_sps < 2);
    if (!need_reinit && ted->sps > 0 && ted->sps != sps) {
        need_reinit = 1;
    }

    int is_first_init = (ted->omega_mid == 0.0f || ted->twice_sps < 2);

    if (need_reinit) {
        /* Debug: log TED SPS change when DSD_NEO_DEBUG_CQPSK=1 */
        if (debug_cqpsk_enabled()) {
            fprintf(stderr, "[GARDNER] TED %s: sps=%d->%d old_omega=%.3f old_mu=%.3f (mu=%d for warmup)\n",
                    is_first_init ? "init" : "sps_change", ted->sps, sps, ted->omega, ted->mu, sps);
        }

        /* Reset mu on any reinitialization (first init OR SPS change).
         *
         * OP25's set_omega() preserves d_mu because OP25 uses freq_xlat for
         * channel selection within a fixed wideband capture - the timing phase
         * relationship is maintained across channels since the samples are
         * continuous.
         *
         * dsd-neo uses hardware retuning (RTL-SDR center frequency changes),
         * which means we're receiving completely new samples from a different
         * RF frequency. Preserving mu across retunes is incorrect because:
         *
         * 1. The old timing phase has no relationship to the new signal
         * 2. When switching from 5 SPS (omega=5) to 4 SPS (omega=4), a preserved
         *    mu=3.992 puts sampling at 99.8% through the symbol (edge) instead
         *    of near the center, causing poor constellation quality
         *
         * Initialize mu to sps (not 0) so the TED consumes fresh samples before
         * outputting the first symbol. OP25 relies on GNU Radio's set_history()
         * to pre-fill the delay line, but dsd-neo doesn't have that mechanism.
         * With mu=0, the inner loop "while (mu > 1.0)" doesn't run, causing
         * the first symbol to be interpolated from stale/zeroed delay line data.
         * With mu=sps, the TED first fills the delay line with ~sps fresh samples
         * before computing and outputting the first valid symbol. */
        ted->mu = (float)sps;

        omega = (float)sps;
        ted->omega = omega;

        /* OP25 uses d_omega_rel = 0.002 (±0.2%) - from gardner_cc_impl.cc line 73.
         * Store omega_rel in state for use in clipping formula. */
        ted->omega_rel = 0.002f;
        ted->omega_mid = omega;
        ted->omega_min = omega * (1.0f - ted->omega_rel);
        ted->omega_max = omega * (1.0f + ted->omega_rel);

        int twice_sps_op25 = 2 * (int)ceilf(ted->omega_max);
        int twice_sps_mmse = (int)ceilf(ted->omega_max / 2.0f) + MMSE_NTAPS + 1;
        int twice_sps_required = (twice_sps_op25 > twice_sps_mmse) ? twice_sps_op25 : twice_sps_mmse;

        if (twice_sps_required > TED_DL_SIZE) {
            d->lp_len = 0;
            return;
        }
        ted->twice_sps = twice_sps_required;
        ted->dl_index = 0;
        ted->sps = sps;

        /* OP25's set_omega() only clears the FIRST element of the delay line:
         *   *d_dl = gr_complex(0,0);  // NOT memset for entire buffer!
         *
         * This preserves existing samples in the delay line, allowing immediate
         * symbol output without warmup delay. The old samples may be from a
         * different channel, but they provide valid timing phase continuity.
         *
         * Clearing the entire buffer forces the TED to wait for the delay line
         * to fill before producing valid symbols, wasting the first ~sps samples
         * after each channel change. */
        ted->dl[0] = 0.0f;
        ted->dl[1] = 0.0f;

        /* OP25's set_omega() does NOT reset last_sample or lock accumulator.
         * Preserve these for phase continuity across SPS changes. */
    }

    /* OP25 gains: gain_mu=0.025, gain_omega=0.1*gain_mu^2
     * From p25_demodulator_dev.py lines 58, 398 */
    float gain_mu = d->ted_gain > 0.0f ? d->ted_gain : 0.025f;
    float gain_omega = 0.1f * gain_mu * gain_mu;

    float* iq_in = d->lowpassed;
    float* iq_out = d->timing_buf;

    float mu = ted->mu;
    int dl_index = ted->dl_index;
    int twice_sps = ted->twice_sps;
    float* dl = ted->dl;

    /* Last sample for Gardner (OP25: d_last_sample) */
    float last_r = ted->last_r;
    float last_j = ted->last_j;

    /* Lock detector accumulator */
    float lock_accum = ted->lock_accum;
    int lock_count = ted->lock_count;

    int i = 0; /* input index */
    int o = 0; /* output index (interleaved floats) */

    /*
     * Main loop: OP25's gardner_cc_impl::general_work() structure
     */
    while (o < buf_len && i < nc) {
        /*
         * Inner loop: consume samples while mu > 1.0, filling delay line
         * OP25 lines 145-152: push to delay line, no rotation
         */
        while (mu > 1.0f && i < nc) {
            mu -= 1.0f;

            /* Get input sample - NO NCO rotation */
            const size_t ii = (size_t)i;
            float in_r = iq_in[ii * 2];
            float in_j = iq_in[ii * 2 + 1];

            /* NaN check (matches OP25) */
            if (IS_NAN(in_r)) {
                in_r = 0.0f;
            }
            if (IS_NAN(in_j)) {
                in_j = 0.0f;
            }

            /* Push to delay line at both dl_index and dl_index + twice_sps
             * This is OP25's circular buffer trick for wrap-free interpolation */
            const size_t dl_i = (size_t)dl_index;
            const size_t dl_i2 = (size_t)dl_index + (size_t)twice_sps;
            dl[dl_i * 2] = in_r;
            dl[dl_i * 2 + 1] = in_j;
            dl[dl_i2 * 2] = in_r;
            dl[dl_i2 * 2 + 1] = in_j;

            dl_index++;
            if (dl_index >= twice_sps) {
                dl_index = 0;
            }

            i++;
        }

        if (i >= nc) {
            break;
        }

        /*
         * Symbol output: OP25 lines 154-194
         * Interpolate, compute Gardner error, output
         */

        /* Compute half-omega parameters (OP25 style) */
        float half_omega = omega / 2.0f;
        int half_sps = (int)floorf(half_omega);
        float half_mu = mu + half_omega - (float)half_sps;
        if (half_mu > 1.0f) {
            half_mu -= 1.0f;
            half_sps += 1;
        }
        if (half_sps < 0) {
            half_sps = 0;
        }

        /* Bounds check for MMSE reads */
        int max_mid_idx = dl_index + MMSE_NTAPS - 1;
        int max_sym_idx = dl_index + half_sps + MMSE_NTAPS - 1;
        if (max_mid_idx >= 2 * twice_sps || max_sym_idx >= 2 * twice_sps) {
            mu += omega;
            continue;
        }

        /* OP25: interp_samp_mid at dl_index (mid-symbol point) */
        float mid_r, mid_j;
        mmse_interp_cc(dl + (size_t)dl_index * 2, mu, &mid_r, &mid_j);

        /* OP25: interp_samp at dl_index + half_sps (symbol point) */
        float sym_r, sym_j;
        mmse_interp_cc(dl + (size_t)(dl_index + half_sps) * 2, half_mu, &sym_r, &sym_j);

        /* OP25 Gardner error: (last - current) * mid
         * From gardner_cc_impl.cc lines 169-172 */
        float error_real = (last_r - sym_r) * mid_r;
        float error_imag = (last_j - sym_j) * mid_j;
        float symbol_error = error_real + error_imag;

        if (IS_NAN(symbol_error)) {
            symbol_error = 0.0f;
        }
        if (symbol_error < -1.0f) {
            symbol_error = -1.0f;
        }
        if (symbol_error > 1.0f) {
            symbol_error = 1.0f;
        }

        /*
         * OP25 Lock detector (Yair Linn method)
         * From gardner_cc_impl.cc lines 177-185
         *
         * IEEE Transactions on Wireless Communications Vol 5, No 2, Feb 2006
         */
        float ie2 = sym_r * sym_r;
        float io2 = mid_r * mid_r;
        float qe2 = sym_j * sym_j;
        float qo2 = mid_j * mid_j;
        float yi = ((ie2 + io2) != 0.0f) ? (ie2 - io2) / (ie2 + io2) : 0.0f;
        float yq = ((qe2 + qo2) != 0.0f) ? (qe2 - qo2) / (qe2 + qo2) : 0.0f;
        lock_accum += yi + yq;
        lock_count++;

        /* OP25 omega update: d_omega += d_gain_omega * symbol_error * abs(interp_samp)
         * From gardner_cc_impl.cc line 187 */
        float sym_mag = sqrtf(sym_r * sym_r + sym_j * sym_j);
        omega = omega + gain_omega * symbol_error * sym_mag;

        /* Clip omega to valid range using branchless_clip
         * From gardner_cc_impl.cc line 188:
         *   d_omega = d_omega_mid + gr::branchless_clip(d_omega-d_omega_mid, d_omega_rel);
         * OP25 passes d_omega_rel directly, but the intended range is [omega_min, omega_max]
         * which equals omega_mid ± (omega_mid * omega_rel). We must scale by omega_mid
         * to get the correct ±0.2% tolerance (e.g., ±0.01 samples at 5 sps). */
        omega = ted->omega_mid + branchless_clip(omega - ted->omega_mid, ted->omega_mid * ted->omega_rel);

        /* Save current symbol as last for next iteration */
        last_r = sym_r;
        last_j = sym_j;

        /* OP25 mu update: d_mu += d_omega + d_gain_mu * symbol_error
         * From gardner_cc_impl.cc line 190 */
        mu += omega + gain_mu * symbol_error;

        /* Output interpolated sample - NO carrier correction, just timing */
        iq_out[o++] = sym_r;
        iq_out[o++] = sym_j;
    }

    /* Save state */
    ted->mu = mu;
    ted->omega = omega;
    ted->dl_index = dl_index;
    ted->last_r = last_r;
    ted->last_j = last_j;
    ted->lock_accum = lock_accum;
    ted->lock_count = lock_count;

    /* Copy output back to lowpassed buffer and update length */
    if (o >= 2) {
        for (int j = 0; j < o; j++) {
            d->lowpassed[j] = iq_out[j];
        }
        d->lp_len = o;
    } else {
        d->lp_len = 0;
    }
}

/*
 * External differential phasor (matches GNU Radio digital.diff_phasor_cc).
 *
 * y[n] = x[n] * conj(x[n-1])
 *
 * From OP25's p25_demodulator_dev.py line 408:
 *   self.diffdec = digital.diff_phasor_cc()
 *
 * This is applied AFTER Gardner timing recovery, producing differential
 * phase symbols for the Costas loop.
 */
extern "C" void
op25_diff_phasor_cc(struct demod_state* d) {
    if (!d || !d->lowpassed || d->lp_len < 2) {
        return;
    }

    const int pairs = d->lp_len >> 1;
    float* iq = d->lowpassed;

    float prev_r = d->cqpsk_diff_prev_r;
    float prev_j = d->cqpsk_diff_prev_j;

    for (int n = 0; n < pairs; n++) {
        const size_t nn = (size_t)n;
        float cur_r = iq[nn * 2];
        float cur_j = iq[nn * 2 + 1];

        /* y = x * conj(prev) = (cur_r + j*cur_j) * (prev_r - j*prev_j) */
        float out_r = cur_r * prev_r + cur_j * prev_j;
        float out_j = cur_j * prev_r - cur_r * prev_j;

        iq[nn * 2] = out_r;
        iq[nn * 2 + 1] = out_j;

        prev_r = cur_r;
        prev_j = cur_j;
    }

    d->cqpsk_diff_prev_r = prev_r;
    d->cqpsk_diff_prev_j = prev_j;
}

/*
 * OP25-compatible Costas loop at symbol rate.
 *
 * Direct port of OP25's costas_loop_cc_impl::work() from:
 *   op25/gr-op25_repeater/lib/costas_loop_cc_impl.cc
 *
 * This operates on DIFFERENTIALLY DECODED symbols (after diff_phasor_cc).
 * The phase detector expects symbols at axis-aligned positions.
 *
 * Signal flow:
 *   Input: Symbol-rate differential phasors from diff_phasor_cc
 *   Processing:
 *     1. NCO rotation: out = in * exp(-j*phase)
 *     2. Phase error detection (QPSK detector)
 *     3. Loop filter update (PI controller)
 *     4. Phase limiting to ±π/2
 *   Output: Carrier-corrected differential phasors
 *
 * Loop parameters from OP25:
 *   loop_bw = 0.008 (from p25_demodulator_dev.py line 59: _def_costas_alpha)
 *   damping = sqrt(2)/2 (critically damped)
 *   max_phase = π/2 (from p25_demodulator_dev.py line 405: TWO_PI/4)
 *
 * The update_gains() formula from costas_loop_cc_impl.cc:162-167:
 *   denom = 1.0 + 2.0*damping*loop_bw + loop_bw*loop_bw
 *   alpha = (4*damping*loop_bw) / denom
 *   beta = (4*loop_bw*loop_bw) / denom
 */
extern "C" void
op25_costas_loop_cc(struct demod_state* d) {
    if (!d || !d->lowpassed || d->lp_len < 2) {
        return;
    }
    if (!d->cqpsk_enable) {
        return;
    }

    dsd_costas_loop_state_t* c = &d->costas_state;

    /* Initialize Costas loop parameters if not already set.
     *
     * OP25 parameters from p25_demodulator_dev.py:
     *   costas_alpha = 0.008 (this is loop_bw, NOT alpha!)
     *   costas = op25_repeater.costas_loop_cc(costas_alpha, 4, TWO_PI/4)
     *
     * The third argument (TWO_PI/4 = π/2) is max_phase.
     *
     * In costas_loop_cc_impl constructor:
     *   set_loop_bandwidth(loop_bw) calls update_gains():
     *     denom = 1.0 + 2.0*damping*loop_bw + loop_bw^2
     *     alpha = (4*damping*loop_bw) / denom
     *     beta = (4*loop_bw^2) / denom
     *
     * With loop_bw=0.008, damping=sqrt(2)/2=0.7071:
     *   denom = 1.0 + 2.0*0.7071*0.008 + 0.008^2 = 1.01137
     *   alpha = (4*0.7071*0.008) / 1.01137 = 0.0223
     *   beta = (4*0.008^2) / 1.01137 = 0.000253
     */
    if (!c->initialized) {
        float loop_bw = 0.008f;
        float damping = 0.70710678118654752440f; /* sqrt(2)/2 */
        float denom = 1.0f + 2.0f * damping * loop_bw + loop_bw * loop_bw;
        c->alpha = (4.0f * damping * loop_bw) / denom;
        c->beta = (4.0f * loop_bw * loop_bw) / denom;
        c->max_freq = 1.0f; /* OP25 default */
        c->min_freq = -1.0f;
        c->damping = damping;
        c->loop_bw = loop_bw;
        /* Phase/freq already reset by dsd_costas_reset or zero-init */
        c->initialized = 1;
    }

    const int pairs = d->lp_len >> 1;
    float* iq = d->lowpassed;

    float phase = c->phase;
    float freq = c->freq;
    const float alpha = c->alpha;
    const float beta = c->beta;
    const float max_freq = c->max_freq;
    const float min_freq = c->min_freq;

    /* OP25 max_phase = TWO_PI/4 = π/2 */
    const float max_phase = kPi / 2.0f;
    const float min_phase = -max_phase;

    for (int n = 0; n < pairs; n++) {
        const size_t nn = (size_t)n;
        float in_r = iq[nn * 2];
        float in_j = iq[nn * 2 + 1];

        /* OP25: nco_out = gr_expj(-d_phase)
         * From costas_loop_cc_impl.cc line 146 */
        float nco_r = cosf(-phase);
        float nco_j = sinf(-phase);

        /* OP25: optr[i] = iptr[i] * nco_out
         * Complex multiply: out = in * nco */
        float out_r = in_r * nco_r - in_j * nco_j;
        float out_j = in_r * nco_j + in_j * nco_r;

        /* OP25 phase error detector for QPSK (order=4)
         * From costas_loop_cc_impl.cc line 150:
         *   d_error = (*this.*d_phase_detector)(optr[i]);
         *
         * Note: OP25 does NOT apply PT_45 rotation here (line 149 is commented out).
         * The phase detector expects axis-aligned symbols. */
        float error = phase_detector_4(out_r, out_j);
        error = branchless_clip(error, 1.0f);

        /* OP25 advance_loop (PI controller)
         * From costas_loop_cc_impl.cc lines 169-173:
         *   d_freq = d_freq + d_beta * error
         *   d_phase = d_phase + d_freq + d_alpha * error */
        freq = freq + beta * error;
        phase = phase + freq + alpha * error;

        /* OP25 phase_limit (clamp to ±max_phase, NOT wrap)
         * From costas_loop_cc_impl.cc lines 183-188 */
        if (phase > max_phase) {
            phase = max_phase;
        } else if (phase < min_phase) {
            phase = min_phase;
        }

        /* OP25 frequency_limit
         * From costas_loop_cc_impl.cc lines 191-196 */
        if (freq > max_freq) {
            freq = max_freq;
        } else if (freq < min_freq) {
            freq = min_freq;
        }

        /* Write carrier-corrected output */
        iq[nn * 2] = out_r;
        iq[nn * 2 + 1] = out_j;
    }

    /* Save state */
    c->phase = phase;
    c->freq = freq;
    c->error = 0.0f; /* Last error not particularly useful at symbol rate */
}

/*
 * Reset FLL band-edge state for fresh frequency acquisition.
 */
extern "C" void
dsd_fll_band_edge_reset(dsd_fll_band_edge_state_t* f) {
    if (!f) {
        return;
    }
    f->phase = 0.0f;
    f->freq = 0.0f;
    /* Clear delay line */
    for (int i = 0; i < FLL_BAND_EDGE_MAX_TAPS; i++) {
        f->delay_r[i] = 0.0f;
        f->delay_i[i] = 0.0f;
    }
    f->delay_idx = 0;
    /* Don't clear initialized or taps - they can be reused */
}

/*
 * Design band-edge filters for FLL.
 *
 * Direct port of GNU Radio's fll_band_edge_cc_impl::design_filter().
 *
 * The band-edge filters are the derivatives of the pulse shaping filter
 * at the band edges. For raised-cosine, this is a sine wave at the rolloff
 * transition, extended to a half-wave which is equivalent to the sum of
 * two sinc functions in time.
 *
 * Parameters:
 *   sps: samples per symbol
 *   rolloff: excess bandwidth (0.0 to 1.0, typically 0.2 for P25)
 *   n_taps: number of filter taps (OP25 uses 2*sps+1)
 */
static void
fll_band_edge_design_filter(dsd_fll_band_edge_state_t* f, int sps, float rolloff, int n_taps) {
    if (n_taps > FLL_BAND_EDGE_MAX_TAPS) {
        n_taps = FLL_BAND_EDGE_MAX_TAPS;
    }
    if (n_taps < 3) {
        n_taps = 3;
    }

    f->n_taps = n_taps;
    f->sps = sps;

    /* GNU Radio fll_band_edge_cc_impl::design_filter() exact port:
     *
     * The baseband filter is built using:
     *   M = round(filter_size / samps_per_sym)
     *   k = -M + i * 2.0 / samps_per_sym
     *   tap = sinc(rolloff * k - 0.5) + sinc(rolloff * k + 0.5)
     *
     * Then normalized by power, and modulated to band edges:
     *   freq = (-N + i) / (2.0 * samps_per_sym) where N = (filter_size - 1) / 2
     *   lower = tap * exp(-j * 2π * (1 + rolloff) * freq)
     *   upper = tap * exp(+j * 2π * (1 + rolloff) * freq)
     *
     * Taps are stored in REVERSE order for the FIR implementation.
     */

    float M = roundf((float)n_taps / (float)sps);
    int N = (n_taps - 1) / 2;

    /* Build baseband filter taps - use stack array since n_taps <= FLL_BAND_EDGE_MAX_TAPS */
    float bb[FLL_BAND_EDGE_MAX_TAPS];
    float power = 0.0f;

    for (int i = 0; i < n_taps; i++) {
        /* k = -M + i * 2.0 / samps_per_sym */
        float k = -M + (float)i * 2.0f / (float)sps;

        /* sinc(rolloff * k - 0.5) + sinc(rolloff * k + 0.5) */
        float arg_m = rolloff * k - 0.5f;
        float arg_p = rolloff * k + 0.5f;

        float sinc_m, sinc_p;
        if (fabsf(arg_m) < 1e-6f) {
            sinc_m = 1.0f;
        } else {
            sinc_m = sinf(kPi * arg_m) / (kPi * arg_m);
        }
        if (fabsf(arg_p) < 1e-6f) {
            sinc_p = 1.0f;
        } else {
            sinc_p = sinf(kPi * arg_p) / (kPi * arg_p);
        }

        bb[i] = sinc_m + sinc_p;
        power += bb[i] * bb[i];
    }

    /* Normalize by power (GNU Radio fll_band_edge_cc_impl::design_filter()).
     *
     * Note: GNU Radio divides by the sum of squares (power), not sqrt(power).
     * This affects the band-edge error magnitude and thus the effective loop gain. */
    if (power > 0.0f) {
        float norm = 1.0f / power;
        for (int i = 0; i < n_taps; i++) {
            bb[i] *= norm;
        }
    }

    /* Create band-edge filters by modulating to upper and lower band edges.
     * GNU Radio stores taps in REVERSE order, so we do the same.
     * freq = (-N + i) / (2.0 * samps_per_sym)
     * phase = 2π * (1 + rolloff) * freq
     */
    for (int i = 0; i < n_taps; i++) {
        float freq = (float)(-N + i) / (2.0f * (float)sps);
        float phase = kTwoPi * (1.0f + rolloff) * freq;

        /* Lower band edge: exp(-j * phase) - store in reverse order.
         * rev_idx ranges from n_taps-1 down to 0 as i goes 0 to n_taps-1. */
        size_t rev_idx = (size_t)(n_taps - 1 - i);
        f->taps_lower_r[rev_idx] = bb[i] * cosf(-phase);
        f->taps_lower_i[rev_idx] = bb[i] * sinf(-phase);

        /* Upper band edge: exp(+j * phase) - store in reverse order */
        f->taps_upper_r[rev_idx] = bb[i] * cosf(phase);
        f->taps_upper_i[rev_idx] = bb[i] * sinf(phase);
    }

    f->initialized = 1;
}

/*
 * Initialize FLL band-edge filter for a given samples-per-symbol.
 *
 * Designs the band-edge filters and sets loop parameters upfront, avoiding
 * the lazy initialization in op25_fll_band_edge_cc() that can cause poor
 * acquisition on the first few sample blocks after cold start or retune.
 *
 * This should be called during demod_reset_on_retune() when CQPSK is enabled
 * and the SPS is known.
 *
 * OP25 behavior: On SPS changes (P25p1 CC -> P25p2 VC), OP25 preserves the
 * FLL state (freq, phase, delay line) to maintain lock during transitions.
 * We adopt the same approach here.
 */
extern "C" void
dsd_fll_band_edge_init(dsd_fll_band_edge_state_t* f, int sps) {
    if (!f || sps < 1) {
        return;
    }

    int is_first_init = !f->initialized;
    int is_sps_change = f->initialized && f->sps != sps && f->sps > 0;

    /* Debug: log FLL init when DSD_NEO_DEBUG_CQPSK=1 */
    if (debug_cqpsk_enabled()) {
        if (is_first_init) {
            fprintf(stderr, "[FLL-INIT] first init sps=%d\n", sps);
        } else if (is_sps_change) {
            fprintf(stderr, "[FLL-INIT] sps change %d->%d (freq preserved)\n", f->sps, sps);
        } else {
            fprintf(stderr, "[FLL-INIT] retune reset sps=%d (freq preserved)\n", sps);
        }
    }

    /* Redesign filters only when SPS changes or on first init.
     * On same-SPS retune, keep existing filters but reset phase/delay. */
    if (is_first_init || is_sps_change) {
        /* OP25 parameters from p25_demodulator_dev.py line 403:
         *   self.fll = digital.fll_band_edge_cc(sps, excess_bw, 2*sps+1, TWO_PI/sps/350)
         *
         * excess_bw = 0.2 (from line ~60: _def_excess_bw = 0.2)
         * filter_size = 2*sps+1 = 11 for sps=5, 9 for sps=4
         * loop_bw = TWO_PI / sps / 350
         */
        float excess_bw = 0.2f;
        int filter_size = 2 * sps + 1;
        float loop_bw = kTwoPi / (float)sps / 350.0f;

        /* Design the band-edge filters for the new SPS.
         *
         * GNU Radio's set_samples_per_symbol() redesigns filters but preserves
         * the frequency estimate. This is critical because:
         * 1. Band-edge frequencies are SPS-dependent: ±(1+rolloff)/(2*sps)
         * 2. The LO offset (tracked by freq) is independent of symbol rate
         *
         * Filter redesign: 5 sps -> band-edges at ±0.12 normalized
         *                  4 sps -> band-edges at ±0.15 normalized
         * Using wrong filters causes garbage band-edge energy estimates. */
        fll_band_edge_design_filter(f, sps, excess_bw, filter_size);

        /* Set loop parameters using GNU Radio's control_loop update_gains() formula */
        f->loop_bw = loop_bw;
        float damping = 0.70710678118654752440f; /* sqrt(2)/2 - critically damped */
        float denom = 1.0f + 2.0f * damping * loop_bw + loop_bw * loop_bw;
        f->alpha = (4.0f * damping * loop_bw) / denom;
        f->beta = (4.0f * loop_bw * loop_bw) / denom;
        f->max_freq = 1.0f; /* rad/sample limit */
        f->min_freq = -1.0f;
    }

    /* State reset behavior - PRESERVE frequency, reset phase and delay.
     *
     * OP25 rx.py set_freq() calls demod.reset() which only resets the Costas
     * loop, NOT the FLL. The FLL frequency estimate is preserved across
     * hardware retunes.
     *
     * This works because the FLL tracks the RTL-SDR's local oscillator offset,
     * which is primarily determined by crystal PPM error. For an RTL-SDR with
     * 10 ppm error, the offset scales linearly with frequency:
     *   - 769 MHz: ~7.69 kHz offset
     *   - 771 MHz: ~7.71 kHz offset
     *   - Difference: ~20 Hz
     *
     * The FLL can easily track a 20 Hz change, but reacquiring from 0 Hz to
     * 200+ Hz is much slower and may fail on P25p2 TDMA bursts.
     *
     * Reset:
     *   - Phase: new samples have no phase relationship to old
     *   - Delay lines: must be cleared when filters are redesigned
     *   - Frequency: PRESERVED (critical for fast reacquisition)
     */
    f->phase = 0.0f;
    /* f->freq preserved - LO offset is similar across nearby frequencies */
    f->delay_idx = 0;
    for (int i = 0; i < FLL_BAND_EDGE_MAX_TAPS; i++) {
        f->delay_r[i] = 0.0f;
        f->delay_i[i] = 0.0f;
    }
}

/*
 * OP25-compatible FLL band-edge frequency lock loop.
 *
 * Direct port of GNU Radio's digital.fll_band_edge_cc as used in OP25:
 *   self.fll = digital.fll_band_edge_cc(sps, excess_bw, 2*sps+1, TWO_PI/sps/350)
 *
 * The FLL operates BEFORE timing recovery to correct coarse frequency offset.
 * This is critical for initial acquisition after channel retunes.
 */
extern "C" void
op25_fll_band_edge_cc(struct demod_state* d) {
    if (!d || !d->lowpassed || d->lp_len < 2) {
        return;
    }
    if (!d->cqpsk_enable) {
        return;
    }

    dsd_fll_band_edge_state_t* f = &d->fll_band_edge_state;

    /* Get SPS from TED state or default */
    int sps = d->ted_sps > 0 ? d->ted_sps : 5;

    /* Redesign filters when SPS changes, matching GNU Radio's set_samples_per_symbol().
     *
     * GNU Radio's set_samples_per_symbol():
     * 1. Redesigns the band-edge filters for the new SPS
     * 2. Preserves the frequency estimate (d_freq is NOT touched)
     *
     * This is critical because:
     * - Band-edge frequencies are SPS-dependent: ±(1+rolloff)/(2*sps)
     *   5 sps -> band-edges at ±0.12 normalized
     *   4 sps -> band-edges at ±0.15 normalized
     * - Using wrong-SPS filters causes garbage band-edge energy estimates
     * - The LO offset (tracked by freq) is independent of symbol rate
     *
     * OP25 behavior note: OP25 production (gnuradio <= 3.10.9.2) has this
     * commented out due to thread-safety issues, but OP25 dev calls it when
     * _fll_threadsafe is True. We don't have threading constraints. */
    int is_first_init = !f->initialized;
    int is_sps_change = f->initialized && f->sps != sps && f->sps > 0;
    int need_reinit = is_first_init || is_sps_change;

    if (need_reinit) {
        /* OP25 parameters from p25_demodulator_dev.py line 403:
         *   self.fll = digital.fll_band_edge_cc(sps, excess_bw, 2*sps+1, TWO_PI/sps/350)
         */
        float excess_bw = 0.2f;
        int filter_size = 2 * sps + 1;
        float loop_bw = kTwoPi / (float)sps / 350.0f;

        /* Design the band-edge filters (sets f->initialized = 1) */
        fll_band_edge_design_filter(f, sps, excess_bw, filter_size);

        /* Set loop parameters using GNU Radio's control_loop update_gains() formula */
        f->loop_bw = loop_bw;
        float damping = 0.70710678118654752440f; /* sqrt(2)/2 - critically damped */
        float denom = 1.0f + 2.0f * damping * loop_bw + loop_bw * loop_bw;
        f->alpha = (4.0f * damping * loop_bw) / denom;
        f->beta = (4.0f * loop_bw * loop_bw) / denom;
        f->max_freq = 1.0f; /* rad/sample limit */
        f->min_freq = -1.0f;

        if (is_first_init) {
            /* First init: zero all state */
            f->phase = 0.0f;
            f->freq = 0.0f;
            f->delay_idx = 0;
            for (int i = 0; i < FLL_BAND_EDGE_MAX_TAPS; i++) {
                f->delay_r[i] = 0.0f;
                f->delay_i[i] = 0.0f;
            }
        } else {
            /* SPS change: preserve freq (LO offset), clear delay line (filter changed) */
            f->phase = 0.0f;
            /* f->freq preserved - LO offset is independent of symbol rate */
            f->delay_idx = 0;
            for (int i = 0; i < FLL_BAND_EDGE_MAX_TAPS; i++) {
                f->delay_r[i] = 0.0f;
                f->delay_i[i] = 0.0f;
            }
        }

        /* Debug: log FLL init/reinit when DSD_NEO_DEBUG_CQPSK=1 */
        if (debug_cqpsk_enabled()) {
            float freq_hz = f->freq * ((float)(d->rate_out > 0 ? d->rate_out : 24000) / kTwoPi);
            if (is_first_init) {
                fprintf(stderr, "[FLL] init: sps=%d filter_size=%d loop_bw=%.6f\n", sps, filter_size, loop_bw);
            } else {
                fprintf(stderr, "[FLL] sps_change: sps=%d filter_size=%d freq=%.1fHz (preserved)\n", sps, filter_size,
                        freq_hz);
            }
        }
    }

    const int pairs = d->lp_len >> 1;
    float* iq = d->lowpassed;

    float phase = f->phase;
    float freq = f->freq;
    const float alpha = f->alpha;
    const float beta = f->beta;
    const float max_freq = f->max_freq;
    const float min_freq = f->min_freq;
    const int n_taps = f->n_taps;

    float* delay_r = f->delay_r;
    float* delay_i = f->delay_i;
    int delay_idx = f->delay_idx;

    for (int n = 0; n < pairs; n++) {
        const size_t nn = (size_t)n;
        float in_r = iq[nn * 2];
        float in_i = iq[nn * 2 + 1];

        /* NCO rotation: out = in * exp(+j*phase)
         * From GNU Radio fll_band_edge_cc_impl.cc:
         *   nco_out = gr_expj(d_phase)  // Note: POSITIVE phase!
         *   out[i] = in[i] * nco_out
         */
        float nco_r = cosf(phase);
        float nco_i = sinf(phase);
        float out_r = in_r * nco_r - in_i * nco_i;
        float out_i = in_r * nco_i + in_i * nco_r;

        /* Update delay line */
        delay_r[delay_idx] = out_r;
        delay_i[delay_idx] = out_i;

        /* Compute band-edge filter outputs */
        float lower_r = 0.0f, lower_i = 0.0f;
        float upper_r = 0.0f, upper_i = 0.0f;

        for (int k = 0; k < n_taps; k++) {
            int idx = (delay_idx - k + n_taps) % n_taps;
            float dr = delay_r[idx];
            float di = delay_i[idx];

            /* Lower band-edge filter: complex multiply */
            lower_r += dr * f->taps_lower_r[k] - di * f->taps_lower_i[k];
            lower_i += dr * f->taps_lower_i[k] + di * f->taps_lower_r[k];

            /* Upper band-edge filter: complex multiply */
            upper_r += dr * f->taps_upper_r[k] - di * f->taps_upper_i[k];
            upper_i += dr * f->taps_upper_i[k] + di * f->taps_upper_r[k];
        }

        /* Advance delay line index */
        delay_idx = (delay_idx + 1) % n_taps;

        /* Compute frequency error: |upper|^2 - |lower|^2
         *
         * From GNU Radio fll_band_edge_cc_impl.cc:
         *   out_upper = d_filter_lower->filter(out[i]);  // Note: SWAPPED!
         *   out_lower = d_filter_upper->filter(out[i]);  // Note: SWAPPED!
         *   error = norm(out_lower) - norm(out_upper);
         *
         * GNU Radio swaps the filter outputs - d_filter_lower produces out_upper
         * and d_filter_upper produces out_lower. This is intentional: the "lower"
         * band-edge filter detects energy that appears in the upper sideband when
         * there's a positive frequency offset.
         *
         * In dsd-neo, we use taps_lower to compute lower_* and taps_upper to compute
         * upper_*, so we need to swap the error formula to match GNU Radio's behavior:
         *   error = norm(upper) - norm(lower)  (equivalent to their swapped version)
         */
        float lower_mag2 = lower_r * lower_r + lower_i * lower_i;
        float upper_mag2 = upper_r * upper_r + upper_i * upper_i;
        float error = upper_mag2 - lower_mag2;

        /* Clamp error */
        if (error > 1.0f) {
            error = 1.0f;
        }
        if (error < -1.0f) {
            error = -1.0f;
        }

        /* GNU Radio control_loop advance_loop() - second-order loop filter.
         * From gr-blocks/include/gnuradio/blocks/control_loop.h:
         *   d_freq = d_freq + d_beta * error    (integral path)
         *   d_phase = d_phase + d_freq + d_alpha * error  (phase with proportional term)
         *
         * The alpha*error term provides the proportional path for fast transient
         * response. Without it, the loop is sluggish and may not track properly.
         */
        freq = freq + beta * error;

        /* Frequency limit (control_loop::frequency_limit) */
        if (freq > max_freq) {
            freq = max_freq;
        } else if (freq < min_freq) {
            freq = min_freq;
        }

        /* Phase advance with proportional term (the key fix!) */
        phase = phase + freq + alpha * error;

        /* Phase wrap to [-2pi, 2pi] (control_loop::phase_wrap)
         * GNU Radio wraps to ±2π, not ±π */
        while (phase > kTwoPi) {
            phase -= kTwoPi;
        }
        while (phase < -kTwoPi) {
            phase += kTwoPi;
        }

        /* Write output */
        iq[nn * 2] = out_r;
        iq[nn * 2 + 1] = out_i;
    }

    /* Save state */
    f->phase = phase;
    f->freq = freq;
    f->delay_idx = delay_idx;

    /* Debug: Log FLL band-edge state when DSD_NEO_DEBUG_CQPSK=1 */
    {
        static int call_count = 0;
        static float prev_freq = 0.0f;
        if (debug_cqpsk_enabled() && (++call_count % 50) == 0) {
            /* Convert freq rad/sample to Hz: f_hz = freq * Fs / (2π) */
            float Fs = (float)d->rate_out;
            float freq_hz = freq * Fs / kTwoPi;
            float delta_freq_hz = (freq - prev_freq) * Fs / kTwoPi;
            prev_freq = freq;
            /* Estimate "locked" heuristic: freq change is small */
            const char* lock_status = (fabsf(delta_freq_hz) < 10.0f) ? "locked" : "tracking";
            fprintf(stderr, "[FLL-BE] freq:%.1fHz delta:%.2fHz phase:%.3f alpha:%.6f beta:%.9f (%s)\n", freq_hz,
                    delta_freq_hz, phase, f->alpha, f->beta, lock_status);
        }
    }
}
