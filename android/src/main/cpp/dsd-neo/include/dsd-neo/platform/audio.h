// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

/**
 * @file
 * @brief Cross-platform audio I/O abstraction for DSD-neo.
 *
 * Provides a unified API for audio input/output that works with
 * PulseAudio (Linux) and PortAudio (cross-platform/Windows).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque audio stream handle */
typedef struct dsd_audio_stream dsd_audio_stream;

/* Audio device information */
typedef struct dsd_audio_device {
    int index;
    char name[512];
    char description[256];
    int is_input;
    int is_output;
    int initialized;
} dsd_audio_device;

/* Audio stream parameters */
typedef struct dsd_audio_params {
    int sample_rate;      /* e.g., 8000, 48000 */
    int channels;         /* 1 (mono) or 2 (stereo) */
    int bits_per_sample;  /* 16 */
    const char* device;   /* Device name/identifier, or NULL for default */
    const char* app_name; /* Application name for audio server (optional) */
} dsd_audio_params;

/**
 * @brief Initialize audio subsystem.
 *
 * Must be called before any audio operations.
 *
 * @return 0 on success, non-zero on failure.
 */
int dsd_audio_init(void);

/**
 * @brief Cleanup audio subsystem.
 *
 * Call at program termination.
 */
void dsd_audio_cleanup(void);

/**
 * @brief Enumerate available audio devices.
 *
 * @param inputs    Array to receive input devices (may be NULL).
 * @param outputs   Array to receive output devices (may be NULL).
 * @param max_count Maximum devices per array (typically 16).
 * @return 0 on success, non-zero on failure.
 */
int dsd_audio_enumerate_devices(dsd_audio_device* inputs, dsd_audio_device* outputs, int max_count);

/**
 * @brief Print available audio devices to stdout.
 *
 * Convenience function for CLI listing.
 *
 * @return 0 on success, non-zero on failure.
 */
int dsd_audio_list_devices(void);

/**
 * @brief Open an audio input stream.
 *
 * @param params    Stream parameters.
 * @return Stream handle, or NULL on failure.
 */
dsd_audio_stream* dsd_audio_open_input(const dsd_audio_params* params);

/**
 * @brief Open an audio output stream.
 *
 * @param params    Stream parameters.
 * @return Stream handle, or NULL on failure.
 */
dsd_audio_stream* dsd_audio_open_output(const dsd_audio_params* params);

/**
 * @brief Read audio samples from input stream.
 *
 * Blocks until requested samples are available.
 *
 * @param stream    Audio stream handle.
 * @param buffer    Buffer to receive samples (int16_t).
 * @param frames    Number of frames to read.
 * @return Number of frames read, or negative on error.
 */
int dsd_audio_read(dsd_audio_stream* stream, int16_t* buffer, size_t frames);

/**
 * @brief Write audio samples to output stream.
 *
 * May block until buffer space is available.
 *
 * @param stream    Audio stream handle.
 * @param buffer    Samples to write (int16_t).
 * @param frames    Number of frames to write.
 * @return Number of frames written, or negative on error.
 */
int dsd_audio_write(dsd_audio_stream* stream, const int16_t* buffer, size_t frames);

/**
 * @brief Close an audio stream.
 *
 * @param stream    Stream handle (safe to pass NULL).
 */
void dsd_audio_close(dsd_audio_stream* stream);

/**
 * @brief Flush audio output buffers.
 *
 * @param stream    Stream handle.
 * @return 0 on success, non-zero on failure.
 */
int dsd_audio_drain(dsd_audio_stream* stream);

/**
 * @brief Get human-readable error string.
 *
 * @return Last error message, or empty string if none.
 */
const char* dsd_audio_get_error(void);

/**
 * @brief Get the name of the active audio backend.
 *
 * @return Backend name string (e.g., "pulse", "portaudio").
 */
const char* dsd_audio_backend_name(void);

#ifdef __cplusplus
}
#endif
