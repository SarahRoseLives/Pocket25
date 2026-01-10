// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief RAII orchestrator for RTL-SDR stream lifecycle and control.
 *
 * Wraps legacy C streaming control with a C++ class managing start/stop,
 * tuning, and reads with error propagation. Intended as a safer API surface.
 */

#include <dsd-neo/io/rtl_stream.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdlib.h>
#include <string.h>

extern "C" {
// Local forward declarations for legacy functions now hidden from public headers
int dsd_rtl_stream_open(dsd_opts* opts);
void dsd_rtl_stream_close(void);
int dsd_rtl_stream_read(float* out, size_t count, dsd_opts* opts, dsd_state* state);
int dsd_rtl_stream_tune(dsd_opts* opts, long int frequency);
unsigned int dsd_rtl_stream_output_rate(void);
int dsd_rtl_stream_soft_stop(void);
}

namespace {
/**
 * @brief Allocate and duplicate a snapshot of @ref dsd_opts.
 *
 * @param src Source options pointer (may be NULL).
 * @return Newly allocated duplicate or NULL on allocation failure or NULL input.
 */
static dsd_opts*
copy_opts(const dsd_opts* src) {
    if (!src) {
        return nullptr;
    }
    dsd_opts* dst = (dsd_opts*)malloc(sizeof(dsd_opts));
    if (!dst) {
        return nullptr;
    }
    memcpy(dst, src, sizeof(dsd_opts));
    return dst;
}
} // namespace

RtlSdrOrchestrator::RtlSdrOrchestrator(const dsd_opts& opts)
    : opts_(copy_opts(&opts)), started_(false), last_error_code_(0) {}

/**
 * @brief Destructor. Ensures stop() is called and frees internal options.
 */
RtlSdrOrchestrator::~RtlSdrOrchestrator() {
    stop();
    if (opts_) {
        free(opts_);
        opts_ = nullptr;
    }
}

/**
 * @brief Initialize and start the stream threads and device async I/O.
 * @return 0 on success, <0 on error.
 */
int
RtlSdrOrchestrator::start() {
    if (started_) {
        return 0;
    }
    if (!opts_) {
        last_error_code_ = -1;
        return last_error_code_;
    }
    int r = dsd_rtl_stream_open(opts_);
    if (r < 0) {
        last_error_code_ = r;
        return r;
    }
    started_ = true;
    last_error_code_ = 0;
    return 0;
}

/**
 * @brief Stop threads and cleanup resources. Safe to call multiple times.
 * @return 0 on success.
 */
int
RtlSdrOrchestrator::stop() {
    if (!started_) {
        return 0;
    }
    /*
     * Use the soft-stop path to avoid touching the global exitflag.
     * The ncurses menu restarts/destroys RTL streams as part of reconfiguring
     * device parameters (gain/bandwidth/etc). Calling the hard close would set
     * exitflag=1 and terminate the whole application when merely closing the
     * menu. The soft-stop mirrors cleanup (threads, rings, device) without
     * requesting process exit.
     */
    dsd_rtl_stream_soft_stop();
    started_ = false;
    last_error_code_ = 0;
    return 0;
}

int
RtlSdrOrchestrator::soft_stop() {
    if (!started_) {
        return 0;
    }
    dsd_rtl_stream_soft_stop();
    started_ = false;
    last_error_code_ = 0;
    return 0;
}

/**
 * @brief Tune to a new center frequency in Hz.
 * @param center_freq_hz Frequency in Hz.
 * @return 0 on success, <0 on error.
 */
int
RtlSdrOrchestrator::tune(uint32_t center_freq_hz) {
    if (!started_) {
        last_error_code_ = -1;
        return last_error_code_;
    }
    if (!opts_) {
        last_error_code_ = -2;
        return last_error_code_;
    }
    dsd_rtl_stream_tune(opts_, (long int)center_freq_hz);
    last_error_code_ = 0;
    return 0;
}

/**
 * @brief Read up to count audio samples.
 * @param out Destination buffer.
 * @param count Max samples to read.
 * @param out_got [out] Number of samples read.
 * @return 0 on success, <0 on error (e.g., shutdown).
 */
int
RtlSdrOrchestrator::read(float* out, size_t count, int& out_got) {
    if (!started_) {
        last_error_code_ = -1;
        return last_error_code_;
    }
    if (!opts_) {
        last_error_code_ = -2;
        return last_error_code_;
    }
    int got = dsd_rtl_stream_read(out, count, opts_, (dsd_state*)nullptr);
    if (got < 0) {
        last_error_code_ = got;
        return got;
    }
    out_got = got;
    last_error_code_ = 0;
    return 0;
}

/**
 * @brief Current output sample rate in Hz.
 * @return Output sample rate in Hz.
 */
unsigned int
RtlSdrOrchestrator::output_rate() const {
    return dsd_rtl_stream_output_rate();
}
