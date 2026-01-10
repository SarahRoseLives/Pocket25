// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Smoke test for the post-demod polyphase audio decimator (M > 2).
 * We inject interleaved I/Q where Q=0 and provide a custom demod function
 * that collapses to mono audio (I-channel). Then we run full_demod with
 * post_downsample=4 and verify:
 *  - Output length is ~ input_len/4
 *  - A high-frequency tone near Nyquist is attenuated relative to a
 *    low-frequency tone by a conservative margin.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <dsd-neo/dsp/demod_pipeline.h>
#include <dsd-neo/dsp/demod_state.h>

static double
rms(const std::vector<float>& x) {
    long double acc = 0.0;
    for (size_t i = 0; i < x.size(); i++) {
        long double v = (long double)x[i];
        acc += v * v;
    }
    if (x.empty()) {
        return 0.0;
    }
    return std::sqrt((double)(acc / (long double)x.size()));
}

static void
gen_tone_iq(std::vector<float>& iq, double fs, double f, double amp) {
    const double two_pi = 6.28318530717958647692;
    const int pairs = (int)(iq.size() / 2);
    for (int n = 0; n < pairs; n++) {
        double t = (double)n / fs;
        double s = std::sin(two_pi * f * t);
        double v = amp * s;
        if (v > 1.0) {
            v = 1.0;
        }
        if (v < -1.0) {
            v = -1.0;
        }
        iq[(size_t)(n << 1) + 0] = (float)v; // I
        iq[(size_t)(n << 1) + 1] = 0.0f;     // Q
    }
}

static void
copy_i_to_audio_demod(struct demod_state* d) {
    // Collapse interleaved I/Q to mono audio in result
    int Npairs = d->lp_len / 2;
    for (int n = 0; n < Npairs; n++) {
        d->result[n] = d->lowpassed[(size_t)(n << 1) + 0];
    }
    d->result_len = Npairs;
}

static int
run_once(double fs, double f) {
    const int M = 4;
    const double amp = 0.8;
    int Npairs = 4096;
    std::vector<float> iq((size_t)Npairs * 2);
    gen_tone_iq(iq, fs, f, amp);

    demod_state* d = (demod_state*)malloc(sizeof(demod_state));
    if (!d) {
        return 0;
    }
    std::memset(d, 0, sizeof(*d));
    d->lowpassed = d->input_cb_buf; // use internal buffer
    d->lp_len = Npairs * 2;
    for (int i = 0; i < d->lp_len; i++) {
        d->input_cb_buf[i] = iq[(size_t)i];
    }
    d->downsample_passes = 0;
    d->post_downsample = M;
    d->mode_demod = &copy_i_to_audio_demod;
    d->rate_out = (int)fs;
    d->deemph = 0;
    d->audio_lpf_enable = 0;
    d->iq_dc_block_enable = 0;
    d->squelch_gate_open = 1;
    d->squelch_env = 1.0f;
    d->squelch_env_attack = 0.125f;
    d->squelch_env_release = 0.03125f;

    full_demod(d);
    int rv = d->result_len;
    free(d);
    return rv;
}

int
main(void) {
    const double Fs = 48000.0;
    // Passband ~1 kHz; Stopband ~10 kHz
    double f_pass = 1000.0;
    double f_stop = 10000.0;

    // Run passband
    const int M = 4;
    int Npairs = 4096;
    int N = Npairs * 2;

    // Build single run to capture outputs
    std::vector<float> iq_pass((size_t)N);
    gen_tone_iq(iq_pass, Fs, f_pass, 0.8);
    demod_state* d1 = (demod_state*)malloc(sizeof(demod_state));
    if (!d1) {
        return 1;
    }
    std::memset(d1, 0, sizeof(*d1));
    for (int i = 0; i < N; i++) {
        d1->input_cb_buf[i] = iq_pass[(size_t)i];
    }
    d1->lowpassed = d1->input_cb_buf;
    d1->lp_len = N;
    d1->downsample_passes = 0;
    d1->post_downsample = M;
    d1->mode_demod = &copy_i_to_audio_demod;
    d1->rate_out = (int)Fs;
    d1->squelch_gate_open = 1;
    d1->squelch_env = 1.0f;
    d1->squelch_env_attack = 0.125f;
    d1->squelch_env_release = 0.03125f;
    full_demod(d1);
    int out_len_pass = d1->result_len;

    // Stopband run
    std::vector<float> iq_stop((size_t)N);
    gen_tone_iq(iq_stop, Fs, f_stop, 0.8);
    demod_state* d2 = (demod_state*)malloc(sizeof(demod_state));
    if (!d2) {
        free(d1);
        return 1;
    }
    std::memset(d2, 0, sizeof(*d2));
    for (int i = 0; i < N; i++) {
        d2->input_cb_buf[i] = iq_stop[(size_t)i];
    }
    d2->lowpassed = d2->input_cb_buf;
    d2->lp_len = N;
    d2->downsample_passes = 0;
    d2->post_downsample = M;
    d2->mode_demod = &copy_i_to_audio_demod;
    d2->rate_out = (int)Fs;
    d2->squelch_gate_open = 1;
    d2->squelch_env = 1.0f;
    d2->squelch_env_attack = 0.125f;
    d2->squelch_env_release = 0.03125f;
    full_demod(d2);
    int out_len_stop = d2->result_len;

    if (!(out_len_pass >= (Npairs / M) - 2 && out_len_pass <= (Npairs / M) + 2)) {
        std::fprintf(stderr, "polydecim: unexpected length pass=%d ref=%d\n", out_len_pass, Npairs / M);
        return 1;
    }
    if (!(out_len_stop == out_len_pass)) {
        std::fprintf(stderr, "polydecim: length mismatch stop=%d pass=%d\n", out_len_stop, out_len_pass);
        return 1;
    }
    std::vector<float> y_pass((size_t)out_len_pass);
    std::vector<float> y_stop((size_t)out_len_stop);
    for (int i = 0; i < out_len_pass; i++) {
        y_pass[(size_t)i] = d1->result[i];
    }
    for (int i = 0; i < out_len_stop; i++) {
        y_stop[(size_t)i] = d2->result[i];
    }
    double rp = rms(y_pass);
    double rs = rms(y_stop);
    if (rp <= 1e-9 || rs <= 0.0) {
        std::fprintf(stderr, "polydecim: degenerate RMS rp=%.3f rs=%.3f\n", rp, rs);
        free(d1);
        free(d2);
        return 1;
    }
    double att_db = 20.0 * std::log10(rs / rp);
    if (!(att_db <= -15.0)) { // conservative bound
        std::fprintf(stderr, "polydecim: attenuation too small %.2f dB\n", att_db);
        free(d1);
        free(d2);
        return 1;
    }
    free(d1);
    free(d2);
    return 0;
}
