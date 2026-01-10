// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 *
 * FIR Filter Design Functions
 *
 * Ported from GNU Radio gr-filter/lib/firdes.cc and gr-fft/lib/window.cc
 * Original Copyright 2002,2007,2008,2012,2013,2018,2021 Free Software Foundation, Inc.
 *
 * This is a direct port of GNU Radio's firdes implementation so we can
 * generate taps that match GNU Radio's firdes::low_pass() across platforms.
 */

#include <dsd-neo/dsp/firdes.h>

#include <math.h>
#include <string.h>

/* GNU Radio's M_PI definition */
#ifndef GR_M_PI
#define GR_M_PI 3.14159265358979323846
#endif

/*
 * ============================================================================
 * Window Functions - Direct port from gr-fft/lib/window.cc
 * ============================================================================
 */

/**
 * @brief Get max attenuation for a window type.
 *
 * From gr-fft/lib/window.cc window::max_attenuation()
 */
double
dsd_window_max_attenuation(dsd_window_type_t window_type) {
    switch (window_type) {
        case DSD_WIN_HAMMING: return 53;
        case DSD_WIN_HANN: return 44;
        case DSD_WIN_BLACKMAN: return 74;
        case DSD_WIN_RECTANGULAR: return 21;
        case DSD_WIN_BLACKMAN_HARRIS: return 92;
        case DSD_WIN_BARTLETT: return 27;
        case DSD_WIN_FLATTOP: return 93;
        case DSD_WIN_KAISER:
            /* Kaiser requires beta parameter, use default approximation */
            return (6.76 / 0.1102 + 8.7);
        default: return 53; /* default to Hamming */
    }
}

/**
 * @brief Build a Hamming window.
 *
 * From gr-fft/lib/window.cc window::hamming():
 *   for (int n = 0; n < ntaps; n++)
 *       taps[n] = 0.54 - 0.46 * cos((2 * GR_M_PI * n) / M);
 * where M = ntaps - 1
 */
void
dsd_window_hamming(int ntaps, float* taps_out) {
    float M = (float)(ntaps - 1);

    for (int n = 0; n < ntaps; n++) {
        taps_out[n] = 0.54f - 0.46f * cosf((2.0f * (float)GR_M_PI * (float)n) / M);
    }
}

/**
 * @brief Build a Hann window.
 *
 * From gr-fft/lib/window.cc window::hann():
 *   for (int n = 0; n < ntaps; n++)
 *       taps[n] = 0.5 - 0.5 * cos((2 * GR_M_PI * n) / M);
 * where M = ntaps - 1
 */
void
dsd_window_hann(int ntaps, float* taps_out) {
    float M = (float)(ntaps - 1);

    for (int n = 0; n < ntaps; n++) {
        taps_out[n] = 0.5f - 0.5f * cosf((2.0f * (float)GR_M_PI * (float)n) / M);
    }
}

/**
 * @brief Build a Blackman window.
 *
 * From gr-fft/lib/window.cc window::blackman():
 *   return coswindow(ntaps, 0.42, 0.5, 0.08);
 */
static void
dsd_window_blackman(int ntaps, float* taps_out) {
    float M = (float)(ntaps - 1);

    for (int n = 0; n < ntaps; n++) {
        taps_out[n] = 0.42f - 0.5f * cosf((2.0f * (float)GR_M_PI * (float)n) / M)
                      + 0.08f * cosf((4.0f * (float)GR_M_PI * (float)n) / M);
    }
}

/**
 * @brief Build a Blackman-Harris window (92 dB attenuation).
 *
 * From gr-fft/lib/window.cc window::blackman_harris(ntaps, 92):
 *   return coswindow(ntaps, 0.35875, 0.48829, 0.14128, 0.01168);
 */
static void
dsd_window_blackman_harris(int ntaps, float* taps_out) {
    float M = (float)(ntaps - 1);

    for (int n = 0; n < ntaps; n++) {
        taps_out[n] = 0.35875f - 0.48829f * cosf((2.0f * (float)GR_M_PI * (float)n) / M)
                      + 0.14128f * cosf((4.0f * (float)GR_M_PI * (float)n) / M)
                      - 0.01168f * cosf((6.0f * (float)GR_M_PI * (float)n) / M);
    }
}

/**
 * @brief Build a Bartlett (triangular) window.
 *
 * From gr-fft/lib/window.cc window::bartlett()
 */
static void
dsd_window_bartlett(int ntaps, float* taps_out) {
    float M = (float)(ntaps - 1);

    for (int n = 0; n < ntaps / 2; n++) {
        taps_out[n] = 2.0f * (float)n / M;
    }
    for (int n = ntaps / 2; n < ntaps; n++) {
        taps_out[n] = 2.0f - 2.0f * (float)n / M;
    }
}

/**
 * @brief Build a flat-top window.
 *
 * From gr-fft/lib/window.cc window::flattop()
 */
static void
dsd_window_flattop(int ntaps, float* taps_out) {
    float M = (float)(ntaps - 1);
    double scale = 4.63867;

    for (int n = 0; n < ntaps; n++) {
        taps_out[n] =
            (float)((1.0 / scale) - (1.93 / scale) * cos((2.0 * GR_M_PI * n) / M)
                    + (1.29 / scale) * cos((4.0 * GR_M_PI * n) / M) - (0.388 / scale) * cos((6.0 * GR_M_PI * n) / M)
                    + (0.028 / scale) * cos((8.0 * GR_M_PI * n) / M));
    }
}

/**
 * @brief Build a rectangular window (all ones).
 */
static void
dsd_window_rectangular(int ntaps, float* taps_out) {
    for (int n = 0; n < ntaps; n++) {
        taps_out[n] = 1.0f;
    }
}

/**
 * @brief Build a window function.
 */
void
dsd_window_build(dsd_window_type_t window_type, int ntaps, float* taps_out) {
    switch (window_type) {
        case DSD_WIN_HAMMING: dsd_window_hamming(ntaps, taps_out); break;
        case DSD_WIN_HANN: dsd_window_hann(ntaps, taps_out); break;
        case DSD_WIN_BLACKMAN: dsd_window_blackman(ntaps, taps_out); break;
        case DSD_WIN_BLACKMAN_HARRIS: dsd_window_blackman_harris(ntaps, taps_out); break;
        case DSD_WIN_BARTLETT: dsd_window_bartlett(ntaps, taps_out); break;
        case DSD_WIN_FLATTOP: dsd_window_flattop(ntaps, taps_out); break;
        case DSD_WIN_RECTANGULAR:
        default: dsd_window_rectangular(ntaps, taps_out); break;
    }
}

/*
 * ============================================================================
 * Filter Design Functions - Direct port from gr-filter/lib/firdes.cc
 * ============================================================================
 */

/**
 * @brief Compute the number of taps for a given window and transition width.
 *
 * From gr-filter/lib/firdes.cc firdes::compute_ntaps():
 *   double a = fft::window::max_attenuation(window_type, param);
 *   int ntaps = (int)(a * sampling_freq / (22.0 * transition_width));
 *   if ((ntaps & 1) == 0) ntaps++;
 *   return ntaps;
 */
int
dsd_firdes_compute_ntaps(double sampling_freq, double transition_width, dsd_window_type_t window_type) {
    double a = dsd_window_max_attenuation(window_type);
    int ntaps = (int)(a * sampling_freq / (22.0 * transition_width));

    /* Ensure odd number of taps */
    if ((ntaps & 1) == 0) {
        ntaps++;
    }

    return ntaps;
}

/**
 * @brief Design a low-pass FIR filter using the window method.
 *
 * Direct port of GNU Radio's firdes::low_pass() from gr-filter/lib/firdes.cc:
 *
 *   int ntaps = compute_ntaps(sampling_freq, transition_width, window_type, param);
 *   vector<float> taps(ntaps);
 *   vector<float> w = window(window_type, ntaps, param);
 *
 *   int M = (ntaps - 1) / 2;
 *   double fwT0 = 2 * GR_M_PI * cutoff_freq / sampling_freq;
 *
 *   for (int n = -M; n <= M; n++) {
 *       if (n == 0)
 *           taps[n + M] = fwT0 / GR_M_PI * w[n + M];
 *       else
 *           taps[n + M] = sin(n * fwT0) / (n * GR_M_PI) * w[n + M];
 *   }
 *
 *   // normalize for unity gain at DC
 *   double fmax = taps[0 + M];
 *   for (int n = 1; n <= M; n++)
 *       fmax += 2 * taps[n + M];
 *   gain /= fmax;
 *   for (int i = 0; i < ntaps; i++)
 *       taps[i] *= gain;
 */
int
dsd_firdes_low_pass(double gain, double sampling_freq, double cutoff_freq, double transition_width,
                    dsd_window_type_t window_type, float* taps_out, int max_taps) {
    /* Sanity checks matching GNU Radio's sanity_check_1f() */
    if (sampling_freq <= 0.0) {
        return -1;
    }
    if (cutoff_freq <= 0.0 || cutoff_freq > sampling_freq / 2.0) {
        return -1;
    }
    if (transition_width <= 0.0) {
        return -1;
    }

    /* Compute number of taps */
    int ntaps = dsd_firdes_compute_ntaps(sampling_freq, transition_width, window_type);

    if (ntaps > max_taps) {
        return -1;
    }

    /* Allocate temporary window buffer on stack (reasonable size for typical filters) */
    float window[1024];
    if (ntaps > 1024) {
        return -1; /* Filter too large */
    }

    /* Build window */
    dsd_window_build(window_type, ntaps, window);

    /* Construct the truncated ideal impulse response [sin(x)/x for low pass]
     * From firdes.cc lines 90-103 */
    int M = (ntaps - 1) / 2;
    double fwT0 = 2.0 * GR_M_PI * cutoff_freq / sampling_freq;

    for (int n = -M; n <= M; n++) {
        if (n == 0) {
            taps_out[n + M] = (float)((fwT0 / GR_M_PI) * window[n + M]);
        } else {
            /* sin(n * fwT0) / (n * GR_M_PI) * window[n + M] */
            taps_out[n + M] = (float)((sin(n * fwT0) / (n * GR_M_PI)) * window[n + M]);
        }
    }

    /* Find the factor to normalize the gain, fmax.
     * For low-pass, gain @ zero freq = 1.0
     * From firdes.cc lines 105-112 */
    double fmax = taps_out[0 + M];
    for (int n = 1; n <= M; n++) {
        fmax += 2.0 * taps_out[n + M];
    }

    /* Normalize */
    gain /= fmax;

    for (int i = 0; i < ntaps; i++) {
        taps_out[i] *= (float)gain;
    }

    return ntaps;
}
