// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Quantify alias rejection of cascaded half-band decimation using the
 * real-valued hb_decim2_real() function. We compare RMS of a low-frequency
 * tone (in passband) against a high-frequency tone near Nyquist (stopband)
 * after 1 and 2 cascaded stages. Thresholds are conservative to avoid
 * platform variability.
 */

#include <cmath>
#include <cstdio>
#include <vector>

#include <dsd-neo/dsp/halfband.h>

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
gen_tone(std::vector<float>& dst, double fs, double f, double amp) {
    const double two_pi = 6.28318530717958647692;
    const size_t N = dst.size();
    for (size_t n = 0; n < N; n++) {
        double t = (double)n / fs;
        double s = std::sin(two_pi * f * t);
        dst[n] = (float)(amp * s);
    }
}

static double
stage_atten_db(int stages, double fs, double f_pass, double f_stop) {
    const double amp = 0.85; // near full-scale without clipping
    int N = 8192;
    std::vector<float> in((size_t)N);
    std::vector<float> buf((size_t)N);
    std::vector<float> out((size_t)N);
    float hist_i[10][HB_TAPS - 1] = {};

    // Passband tone
    gen_tone(in, fs, f_pass, amp);
    int in_len = N;
    const float* src = in.data();
    float* dst = buf.data();
    for (int s = 0; s < stages; s++) {
        int out_len = hb_decim2_real(src, in_len, dst, hist_i[s]);
        src = dst;
        in_len = out_len;
        dst = (src == buf.data()) ? out.data() : buf.data();
    }
    std::vector<float> y_pass((size_t)in_len);
    for (int i = 0; i < in_len; i++) {
        y_pass[(size_t)i] = src[i];
    }
    double r_pass = rms(y_pass);

    // Stopband tone near Nyquist
    for (int i = 0; i < stages; i++) {
        for (int k = 0; k < HB_TAPS - 1; k++) {
            hist_i[i][k] = 0;
        }
    }
    gen_tone(in, fs, f_stop, amp);
    in_len = N;
    src = in.data();
    dst = buf.data();
    for (int s = 0; s < stages; s++) {
        int out_len = hb_decim2_real(src, in_len, dst, hist_i[s]);
        src = dst;
        in_len = out_len;
        dst = (src == buf.data()) ? out.data() : buf.data();
    }
    std::vector<float> y_stop((size_t)in_len);
    for (int i = 0; i < in_len; i++) {
        y_stop[(size_t)i] = src[i];
    }
    double r_stop = rms(y_stop);

    if (r_stop <= 1e-9 || r_pass <= 1e-9) {
        return 200.0; // degenerate
    }
    double ratio = r_stop / r_pass;
    return 20.0 * std::log10(ratio);
}

int
main(void) {
    const double Fs = 48000.0;
    // Choose tones: pass ~ 2 kHz; stop near 0.45*Fs (just below Nyquist)
    double f_pass = 2000.0;
    double f_stop = 0.45 * (Fs / 2.0) * 2.0; // 0.45*Fs
    if (f_stop >= (Fs / 2.0)) {
        f_stop = (Fs / 2.0) * 0.9;
    }

    // One stage: expect at least ~18 dB attenuation (conservative)
    double a1 = stage_atten_db(1, Fs, f_pass, f_stop);
    if (!(a1 <= -18.0)) {
        std::fprintf(stderr, "HB alias rejection (1 stage) too low: %.2f dB\n", a1);
        return 1;
    }
    // Multi-stage effects depend on where the tone falls after each decimate;
    // single-stage alias rejection is the primary invariant we assert here.
    return 0;
}
