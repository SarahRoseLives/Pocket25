// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief RAII orchestrator for RTL-SDR stream lifecycle and control.
 *
 * Declares the C++ wrapper class that manages start/stop lifecycle, tuning,
 * and buffered reads over the legacy C RTL-SDR streaming control.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief RAII-class orchestrator for RTL-SDR streaming pipeline.
 *
 * Wraps the existing C orchestration with a safer lifecycle:
 * constructor stores options, start() initializes and launches threads,
 * stop() tears everything down, and the destructor auto-stops when needed.
 * This maintains current behavior while enabling a cleaner API surface.
 */
class RtlSdrOrchestrator {
  public:
    /**
     * @brief Construct a stream with an option snapshot.
     * @param opts @ref dsd_opts used to configure the stream. Copied internally.
     */
    explicit RtlSdrOrchestrator(const dsd_opts& opts);

    /**
     * @brief Destructor. Ensures stop() is called.
     */
    ~RtlSdrOrchestrator();

    /**
     * @brief Initialize and start the stream threads and device async I/O.
     * @return 0 on success, <0 on error.
     */
    int start();

    /**
     * @brief Stop threads and cleanup resources. Safe to call multiple times.
     * @return 0 on success.
     */
    int stop();
    /**
     * @brief Soft-stop without setting global exit flags (used for UI restarts).
     *
     * Leaves process-level exit flags untouched so the UI can restart streams
     * without tearing down the entire application state.
     *
     * @return 0 on success.
     */
    int soft_stop();

    /**
     * @brief Tune to a new center frequency in Hz.
     * @param center_freq_hz Frequency in Hz.
     * @return 0 on success, <0 on error.
     */
    int tune(uint32_t center_freq_hz);

    /**
     * @brief Read up to count audio samples.
     * @param out Destination buffer.
     * @param count Max samples to read.
     * @param out_got [out] Number of samples read.
     * @return 0 on success, <0 on error (e.g., shutdown).
     */
    int read(float* out, size_t count, int& out_got);

    /**
     * @brief Current output sample rate in Hz.
     * @return Output sample rate in Hz.
     */
    unsigned int output_rate() const;

    /**
     * @brief Whether the last operation succeeded.
     * @return true if the last operation returned success; otherwise false.
     */
    bool
    ok() const {
        return last_error_code_ == 0;
    }

    /**
     * @brief Error code from the last failing operation (if any).
     * @return 0 when the last operation succeeded; otherwise negative error code.
     */
    int
    last_error_code() const {
        return last_error_code_;
    }

  private:
    // Non-copyable to avoid accidental shared lifecycle
    RtlSdrOrchestrator(const RtlSdrOrchestrator&) = delete;
    RtlSdrOrchestrator& operator=(const RtlSdrOrchestrator&) = delete;

    // Mutable snapshot of options passed into C API
    dsd_opts* opts_;
    bool started_;
    int last_error_code_;
};
