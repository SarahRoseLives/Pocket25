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

#ifndef DSD_NEO_DSP_FIRDES_H
#define DSD_NEO_DSP_FIRDES_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Window function types matching GNU Radio's fft::window::win_type
 */
typedef enum {
    DSD_WIN_RECTANGULAR = 0,
    DSD_WIN_HAMMING = 1,
    DSD_WIN_HANN = 2,
    DSD_WIN_BLACKMAN = 3,
    DSD_WIN_BLACKMAN_HARRIS = 4,
    DSD_WIN_KAISER = 5,
    DSD_WIN_BARTLETT = 6,
    DSD_WIN_FLATTOP = 7
} dsd_window_type_t;

/**
 * @brief Compute the number of taps for a given window type and transition width.
 *
 * Direct port of GNU Radio's firdes::compute_ntaps().
 *
 * @param sampling_freq   Sampling frequency in Hz
 * @param transition_width Transition band width in Hz
 * @param window_type      Window type (determines max attenuation)
 * @return Number of taps (always odd)
 */
int dsd_firdes_compute_ntaps(double sampling_freq, double transition_width, dsd_window_type_t window_type);

/**
 * @brief Get max attenuation for a window type.
 *
 * Direct port of GNU Radio's fft::window::max_attenuation().
 *
 * @param window_type Window type
 * @return Max attenuation in dB
 */
double dsd_window_max_attenuation(dsd_window_type_t window_type);

/**
 * @brief Build a window function.
 *
 * Direct port of GNU Radio's fft::window::build() for supported types.
 *
 * @param window_type Window type
 * @param ntaps       Number of taps
 * @param taps_out    Output buffer (must have space for ntaps floats)
 */
void dsd_window_build(dsd_window_type_t window_type, int ntaps, float* taps_out);

/**
 * @brief Build a Hamming window.
 *
 * Direct port of GNU Radio's fft::window::hamming().
 * Formula: w[n] = 0.54 - 0.46 * cos(2*pi*n / (M-1))
 *
 * @param ntaps    Number of taps
 * @param taps_out Output buffer
 */
void dsd_window_hamming(int ntaps, float* taps_out);

/**
 * @brief Build a Hann window.
 *
 * Direct port of GNU Radio's fft::window::hann().
 * Formula: w[n] = 0.5 - 0.5 * cos(2*pi*n / (M-1))
 *
 * @param ntaps    Number of taps
 * @param taps_out Output buffer
 */
void dsd_window_hann(int ntaps, float* taps_out);

/**
 * @brief Design a low-pass FIR filter using the window method.
 *
 * Direct port of GNU Radio's firdes::low_pass().
 *
 * From GNU Radio documentation:
 * - cutoff_freq is the CENTER of the transition band
 * - Uses windowed-sinc design method
 * - Normalizes for unity gain at DC
 *
 * @param gain             Filter gain (typically 1.0)
 * @param sampling_freq    Sampling frequency in Hz
 * @param cutoff_freq      Cutoff frequency in Hz (center of transition band)
 * @param transition_width Transition band width in Hz
 * @param window_type      Window type
 * @param taps_out         Output buffer for filter taps
 * @param max_taps         Maximum size of taps_out buffer
 * @return Number of taps written, or -1 on error
 */
int dsd_firdes_low_pass(double gain, double sampling_freq, double cutoff_freq, double transition_width,
                        dsd_window_type_t window_type, float* taps_out, int max_taps);

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_DSP_FIRDES_H */
