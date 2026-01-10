// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief OP25-compatible CQPSK signal chain interface.
 *
 * Direct port of OP25's CQPSK demodulator signal chain:
 *   AGC -> Gardner (timing) -> diff_phasor -> Costas (carrier)
 *
 * From OP25's p25_demodulator_dev.py line 486:
 *   self.connect(self.if_out, self.agc, self.fll, self.clock, self.diffdec, self.costas, ...)
 *
 * Where:
 *   - clock = op25_repeater.gardner_cc (timing recovery only)
 *   - diffdec = digital.diff_phasor_cc (differential decoding at symbol rate)
 *   - costas = op25_repeater.costas_loop_cc (carrier tracking at symbol rate)
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct demod_state; /* fwd decl */

typedef struct {
    float phase;
    float freq;
    float max_freq;
    float min_freq;
    float damping;
    float loop_bw;
    float alpha;
    float beta;
    float error;
    int initialized;
} dsd_costas_loop_state_t;

/**
 * @brief OP25-compatible FLL band-edge filter state.
 *
 * Direct port of GNU Radio's digital.fll_band_edge_cc used in OP25:
 *   self.fll = digital.fll_band_edge_cc(sps, excess_bw, 2*sps+1, TWO_PI/sps/350)
 *
 * The FLL uses band-edge filters to estimate frequency error before timing recovery.
 * This is critical for initial frequency acquisition on channel retunes.
 *
 * Filter size = 2*sps+1. At 48 kHz DSP bandwidth:
 *   - P25P1: SPS=10 -> 21 taps
 *   - P25P2: SPS=8  -> 17 taps
 *   - NXDN:  SPS=20 -> 41 taps
 * Size 48 supports SPS up to 23 (e.g., future 2400 sym/s modes at higher rates).
 */
#define FLL_BAND_EDGE_MAX_TAPS 48

typedef struct {
    float phase;    /* NCO phase accumulator (radians) */
    float freq;     /* NCO frequency (rad/sample) */
    float max_freq; /* Max frequency limit (rad/sample) */
    float min_freq; /* Min frequency limit (rad/sample) */
    float loop_bw;  /* Loop bandwidth */
    float alpha;    /* Loop filter gain (phase/proportional) - from control_loop */
    float beta;     /* Loop filter gain (frequency/integral) - from control_loop */

    /* Band-edge filter taps (upper and lower) */
    float taps_lower_r[FLL_BAND_EDGE_MAX_TAPS];
    float taps_lower_i[FLL_BAND_EDGE_MAX_TAPS];
    float taps_upper_r[FLL_BAND_EDGE_MAX_TAPS];
    float taps_upper_i[FLL_BAND_EDGE_MAX_TAPS];
    int n_taps;

    /* Filter delay line */
    float delay_r[FLL_BAND_EDGE_MAX_TAPS];
    float delay_i[FLL_BAND_EDGE_MAX_TAPS];
    int delay_idx;

    int sps; /* Samples per symbol (for reinit detection) */
    int initialized;
} dsd_fll_band_edge_state_t;

/* OP25-compatible defaults for CQPSK carrier recovery.
 *
 * OP25 uses loop_bw=0.008, damping=sqrt(2)/2, computed alpha/beta:
 *   denom = 1.0 + 2.0 * damping * loop_bw + loop_bw * loop_bw
 *   alpha = (4 * damping * loop_bw) / denom  ≈ 0.0223
 *   beta  = (4 * loop_bw * loop_bw) / denom  ≈ 0.000253
 *
 * From p25_demodulator_dev.py:
 *   costas_alpha = 0.008 (this is loop_bw, NOT alpha)
 *   costas = op25_repeater.costas_loop_cc(costas_alpha, 4, TWO_PI/4)
 *
 * Frequency limits: ±1.0 rad/sample
 * Phase limits: ±π/2 (clamped, not wrapped)
 */

/** @brief OP25 Costas loop bandwidth. */
static inline float
dsd_neo_costas_default_loop_bw_op25(void) {
    return 0.008f;
}

/** @brief Default Costas loop alpha (phase gain) - computed from OP25 loop_bw/damping. */
static inline float
dsd_neo_costas_default_alpha(void) {
    /* loop_bw=0.008, damping=sqrt(2)/2
       denom = 1.0 + 2.0 * 0.7071 * 0.008 + 0.008^2 ≈ 1.01137
       alpha = (4 * 0.7071 * 0.008) / 1.01137 ≈ 0.0223 */
    return 0.0223f;
}

/** @brief Default Costas loop beta (frequency gain) - computed from OP25 loop_bw/damping. */
static inline float
dsd_neo_costas_default_beta(void) {
    /* beta = (4 * 0.008^2) / 1.01137 ≈ 0.000253 */
    return 0.000253f;
}

/** @brief Default max frequency (rad/sample) - OP25: ±1.0 rad/sample. */
static inline float
dsd_neo_costas_default_max_freq(void) {
    return 1.0f;
}

/** @brief Default Costas loop bandwidth (legacy, for non-CQPSK modes). */
static inline float
dsd_neo_costas_default_loop_bw(void) {
    return 6.28318530717958647692f / 100.0f;
}

/** @brief Default Costas loop damping factor (sqrt(2)/2). */
static inline float
dsd_neo_costas_default_damping(void) {
    return 0.70710678118654752440f; /* sqrt(2)/2 */
}

/**
 * @brief Reset Costas loop state for fresh carrier acquisition.
 *
 * Per OP25's costas_reset() in p25_demodulator_dev.py:574-576:
 *   self.costas.set_frequency(0)
 *   self.costas.set_phase(0)
 *
 * Call this on channel retunes to clear stale phase/frequency estimates
 * from the previous channel.
 *
 * @param c Costas loop state to reset.
 */
void dsd_costas_reset(dsd_costas_loop_state_t* c);

/**
 * @brief OP25-compatible Gardner timing recovery block.
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
 * Key OP25 parameters (from p25_demodulator_dev.py and gardner_cc_impl.cc):
 *   - gain_mu = 0.025
 *   - gain_omega = 0.1 * gain_mu^2 = 0.0000625
 *   - omega_rel = 0.002 (±0.2%)
 *
 * @param d Demodulator state. Input: lowpassed (sample-rate IQ after AGC).
 *          Output: lowpassed (symbol-rate samples).
 */
void op25_gardner_cc(struct demod_state* d);

/**
 * @brief External differential phasor decoder (matches GNU Radio diff_phasor_cc).
 *
 * Computes y[n] = x[n] * conj(x[n-1]) to produce differential phase output.
 *
 * From OP25's p25_demodulator_dev.py line 408:
 *   self.diffdec = digital.diff_phasor_cc()
 *
 * This is applied AFTER Gardner timing recovery, producing differential
 * phase symbols for the Costas loop.
 *
 * @param d Demodulator state. Modifies lowpassed in-place to differential phasors.
 */
void op25_diff_phasor_cc(struct demod_state* d);

/**
 * @brief OP25-compatible Costas loop at symbol rate.
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
 * Key OP25 parameters (from p25_demodulator_dev.py and costas_loop_cc_impl.cc):
 *   - loop_bw = 0.008 (called "costas_alpha" in p25_demodulator_dev.py)
 *   - damping = sqrt(2)/2 (critically damped)
 *   - max_phase = π/2
 *   - Computed: alpha ≈ 0.0223, beta ≈ 0.000253
 *
 * @param d Demodulator state. Modifies lowpassed in-place with carrier correction.
 */
void op25_costas_loop_cc(struct demod_state* d);

/**
 * @brief Reset FLL band-edge state for fresh frequency acquisition.
 *
 * Call this on channel retunes to clear stale frequency estimates.
 *
 * @param f FLL state to reset.
 */
void dsd_fll_band_edge_reset(dsd_fll_band_edge_state_t* f);

/**
 * @brief Initialize FLL band-edge filter for a given samples-per-symbol.
 *
 * Designs the band-edge filters and sets loop parameters. Call this during
 * cold start or retune initialization to ensure the FLL is ready before
 * processing samples. This avoids the lazy initialization that can cause
 * poor acquisition on the first few sample blocks.
 *
 * @param f   FLL state to initialize.
 * @param sps Samples per symbol (e.g., 5 for P25p1, 4 for P25p2).
 */
void dsd_fll_band_edge_init(dsd_fll_band_edge_state_t* f, int sps);

/**
 * @brief OP25-compatible FLL band-edge frequency lock loop.
 *
 * Direct port of GNU Radio's digital.fll_band_edge_cc as used in OP25:
 *   self.fll = digital.fll_band_edge_cc(sps, excess_bw, 2*sps+1, TWO_PI/sps/350)
 *
 * This FLL uses band-edge filters to detect and correct frequency offset
 * before timing recovery. The error signal is derived from the difference
 * in power between upper and lower band-edge filter outputs.
 *
 * Signal flow:
 *   Input: AGC'd complex samples at sample rate
 *   Processing:
 *     1. NCO rotation: out = in * exp(-j*phase)
 *     2. Band-edge filtering (upper and lower)
 *     3. Error computation: |x_lower|^2 - |x_upper|^2
 *     4. Loop filter update (frequency only, no phase term)
 *   Output: Frequency-corrected complex samples
 *
 * Key OP25 parameters (from p25_demodulator_dev.py line 403):
 *   - sps = samples per symbol (5 for P25p1, 4 for P25p2)
 *   - excess_bw = 0.2 (rolloff factor)
 *   - filter_size = 2*sps+1
 *   - loop_bw = TWO_PI/sps/350
 *
 * @param d Demodulator state. Modifies lowpassed in-place with frequency correction.
 */
void op25_fll_band_edge_cc(struct demod_state* d);

#ifdef __cplusplus
}
#endif
