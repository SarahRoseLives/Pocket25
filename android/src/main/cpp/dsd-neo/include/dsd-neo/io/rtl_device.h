// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief RTL-SDR device I/O public API.
 *
 * Declares the opaque `rtl_device` handle and functions for configuring and
 * streaming samples from an RTL-SDR, including optional offset tuning, gain
 * control, PPM correction, and asynchronous USB ingestion into an input ring.
 */

#pragma once

#include <stdint.h>

#include <dsd-neo/platform/threading.h>
#include <dsd-neo/runtime/input_ring.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle for RTL-SDR device */
struct rtl_device;

/**
 * @brief Create and initialize a local RTL-SDR device over USB (librtlsdr).
 *
 * @param dev_index Device index to open.
 * @param input_ring Pointer to input ring for incoming I/Q data.
 * @param combine_rotate_enabled Whether to use combined rotate+widen when offset tuning is disabled.
 * @return Pointer to rtl_device handle, or NULL on failure.
 */
struct rtl_device* rtl_device_create(int dev_index, struct input_ring_state* input_ring, int combine_rotate_enabled);

/**
 * @brief Create and initialize a remote RTL-SDR stream via rtl_tcp.
 *
 * Connects to an rtl_tcp server (default port 1234) and configures the
 * receiver via protocol commands. Sample bytes from the TCP stream are
 * widened and pushed into the same input ring used by the USB backend.
 *
 * @param host Remote hostname or IP (e.g., "127.0.0.1").
 * @param port Remote TCP port (e.g., 1234).
 * @param input_ring Pointer to input ring for incoming I/Q data.
 * @param combine_rotate_enabled Whether to use combined rotate+widen when offset tuning is disabled.
 * @param autotune_enabled Enable rtl_tcp adaptive buffering/autotune at startup (1=on, 0=off).
 * @return Pointer to rtl_device handle, or NULL on failure.
 */
struct rtl_device* rtl_device_create_tcp(const char* host, int port, struct input_ring_state* input_ring,
                                         int combine_rotate_enabled, int autotune_enabled);

/**
 * @brief Destroy an RTL-SDR device and free resources.
 *
 * @param dev Pointer to rtl_device handle.
 */
void rtl_device_destroy(struct rtl_device* dev);

/**
 * @brief Set device center frequency.
 *
 * @param dev RTL-SDR device handle.
 * @param frequency Frequency in Hz.
 * @return 0 on success, negative on failure.
 */
int rtl_device_set_frequency(struct rtl_device* dev, uint32_t frequency);

/**
 * @brief Set device sample rate.
 *
 * @param dev RTL-SDR device handle.
 * @param samp_rate Sample rate in Hz.
 * @return 0 on success, negative on failure.
 */
int rtl_device_set_sample_rate(struct rtl_device* dev, uint32_t samp_rate);

/**
 * @brief Query the current device sample rate.
 *
 * USB backend returns the actual rate reported by librtlsdr
 * (which may differ slightly from the requested value). The rtl_tcp
 * backend returns the last programmed value.
 *
 * @param dev RTL-SDR device handle.
 * @return Sample rate in Hz (non-negative) or negative on error.
 */
int rtl_device_get_sample_rate(struct rtl_device* dev);

/**
 * @brief Set tuner gain mode and value.
 *
 * @param dev RTL-SDR device handle.
 * @param gain Gain in tenths of dB, or AUTO_GAIN for automatic.
 * @return 0 on success, negative on failure.
 */
int rtl_device_set_gain(struct rtl_device* dev, int gain);

/**
 * @brief Set tuner gain to the nearest supported value (manual mode).
 *
 * For USB (librtlsdr) backend, queries the list of supported gains and
 * applies the nearest value to the requested target. For rtl_tcp, sends
 * the target value directly and lets the server handle quantization.
 *
 * @param dev RTL-SDR device handle.
 * @param target_tenth_db Target gain in tenths of dB (e.g., 180 for 18.0 dB).
 * @return 0 on success, negative on failure.
 */
int rtl_device_set_gain_nearest(struct rtl_device* dev, int target_tenth_db);

/**
 * @brief Get current tuner gain in tenths of dB as reported by the driver.
 *
 * Returns a non-negative value (e.g., 270 for 27.0 dB) on success, or a
 * negative value on error. When auto-gain is enabled, the returned value may
 * be undefined; prefer checking rtl_device_is_auto_gain().
 */
int rtl_device_get_tuner_gain(struct rtl_device* dev);

/**
 * @brief Return 1 if auto-gain is enabled (requested), 0 if manual.
 */
int rtl_device_is_auto_gain(struct rtl_device* dev);

/**
 * @brief Set frequency correction (PPM error).
 *
 * @param dev RTL-SDR device handle.
 * @param ppm_error PPM correction value.
 * @return 0 on success, negative on failure.
 */
int rtl_device_set_ppm(struct rtl_device* dev, int ppm_error);

/**
 * @brief Set direct sampling mode.
 *
 * @param dev RTL-SDR device handle.
 * @param on Direct sampling selection: 0=off, 1=I input, 2=Q input.
 * @return 0 on success, negative on failure.
 */
int rtl_device_set_direct_sampling(struct rtl_device* dev, int on);

/**
 * @brief Enable/disable offset tuning mode.
 *
 * @param dev RTL-SDR device handle.
 * @param on  1 to enable, 0 to disable.
 * @return 0 on success, negative on failure.
 */
int rtl_device_set_offset_tuning_enabled(struct rtl_device* dev, int on);

/**
 * @brief Set tuner IF bandwidth if supported by the driver.
 * @param dev RTL-SDR device handle.
 * @param bw_hz Target bandwidth in Hz (e.g., 200000).
 * @return 0 on success, negative on failure.
 */
int rtl_device_set_tuner_bandwidth(struct rtl_device* dev, uint32_t bw_hz);

/**
 * @brief Reset device buffer.
 *
 * @param dev RTL-SDR device handle.
 * @return 0 on success, negative on failure.
 */
int rtl_device_reset_buffer(struct rtl_device* dev);

/**
 * @brief Start asynchronous reading from the device.
 *
 * @param dev RTL-SDR device handle.
 * @param buf_len Buffer length for async read.
 * @return 0 on success, negative on failure.
 */
int rtl_device_start_async(struct rtl_device* dev, uint32_t buf_len);

/**
 * @brief Stop asynchronous reading and join the device thread.
 *
 * @param dev RTL-SDR device handle.
 * @return 0 on success, negative on failure.
 */
int rtl_device_stop_async(struct rtl_device* dev);

/**
 * @brief Mute the incoming raw USB/TCP byte stream for a short duration.
 *
 * Note: The argument is in raw input BYTES (u8 I/Q interleaved), not int16
 * samples. This matches how the underlying callback consumes the value
 * (clamping and subtracting from the remaining byte count per callback).
 *
 * @param dev RTL-SDR device handle.
 * @param bytes Number of input bytes to overwrite with 0x7F (mute).
 */
void rtl_device_mute(struct rtl_device* dev, int bytes);

/**
 * @brief Enable or disable the RTL-SDR bias tee.
 *
 * USB (librtlsdr) backend uses `rtlsdr_set_bias_tee` when available at
 * build time. The rtl_tcp backend forwards the request to the server via
 * protocol command 0x0E.
 *
 * @param dev RTL-SDR device handle.
 * @param on  Non-zero to enable, zero to disable.
 * @return 0 on success, negative on failure.
 */
int rtl_device_set_bias_tee(struct rtl_device* dev, int on);

/**
 * @brief Print tuner type and whether this librtlsdr build is expected to support
 * hardware offset tuning for the detected tuner. For rtl_tcp backend, prints that
 * the capability is determined by the server.
 *
 * This is a non-invasive probe based on tuner type heuristics (e.g., upstream
 * librtlsdr disables offset tuning for R820T/R828D). Actual enablement is still
 * attempted later in the normal initialization sequence.
 *
 * @param dev RTL-SDR device handle.
 */
void rtl_device_print_offset_capability(struct rtl_device* dev);

/**
 * @brief Enable or disable rtl_tcp adaptive buffering at runtime.
 *
 * No-op for the USB backend. Updates are applied immediately for rtl_tcp.
 *
 * @param dev RTL-SDR device handle.
 * @param onoff Non-zero to enable autotune; zero to disable.
 * @return 0 on success; negative on error.
 */
int rtl_device_set_tcp_autotune(struct rtl_device* dev, int onoff);
/**
 * @brief Query the rtl_tcp adaptive buffering enable flag.
 *
 * Returns 0 when not applicable (USB backend or null handle).
 *
 * @param dev RTL-SDR device handle.
 * @return 1 if autotune is enabled; 0 otherwise.
 */
int rtl_device_get_tcp_autotune(struct rtl_device* dev);

/**
 * @brief Set (or clear) RTL and tuner crystal reference frequencies.
 *
 * Pass 0 to leave either value unchanged at the driver default.
 * Applies to USB via librtlsdr and to rtl_tcp via protocol commands 0x0B/0x0C.
 *
 * @param dev RTL-SDR device handle.
 * @param rtl_xtal_hz RTL crystal frequency in Hz (0 to leave unchanged).
 * @param tuner_xtal_hz Tuner crystal frequency in Hz (0 to leave unchanged).
 * @return 0 on success; negative on failure.
 */
int rtl_device_set_xtal_freq(struct rtl_device* dev, uint32_t rtl_xtal_hz, uint32_t tuner_xtal_hz);

/**
 * @brief Enable or disable librtlsdr test mode (ramp signal instead of I/Q samples).
 *
 * Applies to USB (rtlsdr_set_testmode) and rtl_tcp (protocol cmd 0x07).
 *
 * @param dev RTL-SDR device handle.
 * @param on Non-zero to enable test mode; zero to disable.
 * @return 0 on success; negative on failure.
 */
int rtl_device_set_testmode(struct rtl_device* dev, int on);

/**
 * @brief Set intermediate frequency (IF) gain for a specific stage.
 *
 * Stage index and gain units follow librtlsdr: gain in tenths of a dB.
 * Applies to USB (rtlsdr_set_tuner_if_gain) and rtl_tcp (protocol cmd 0x06).
 *
 * @param dev RTL-SDR device handle.
 * @param stage IF stage index (0-based).
 * @param gain_tenth_db Gain in tenths of a dB.
 * @return 0 on success; negative on failure.
 */
int rtl_device_set_if_gain(struct rtl_device* dev, int stage, int gain_tenth_db);

#ifdef __cplusplus
}
#endif
