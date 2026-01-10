// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief PortAudio backend implementation for the audio abstraction layer.
 *
 * This file provides the PortAudio implementation of the cross-platform
 * audio API defined in dsd-neo/platform/audio.h. PortAudio supports
 * multiple backends including WASAPI (Windows), ALSA/PulseAudio (Linux),
 * and CoreAudio (macOS).
 */

#include <dsd-neo/platform/platform.h>

#ifdef DSD_USE_PORTAUDIO

#include <dsd-neo/platform/audio.h>

#include <portaudio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Internal Types
 *============================================================================*/

struct dsd_audio_stream {
    PaStream* handle;
    int is_input;
    int channels;
    int sample_rate;
};

/*============================================================================
 * Module State
 *============================================================================*/

static int s_initialized = 0;
static char s_last_error[512] = "";

/*============================================================================
 * Internal Helpers
 *============================================================================*/

static void
set_error(const char* msg) {
    if (msg) {
        strncpy(s_last_error, msg, sizeof(s_last_error) - 1);
        s_last_error[sizeof(s_last_error) - 1] = '\0';
    } else {
        s_last_error[0] = '\0';
    }
}

static void
set_error_pa(PaError err) {
    set_error(Pa_GetErrorText(err));
}

/**
 * @brief Find device index by name.
 *
 * @param name Device name to search for (NULL for default).
 * @param is_input Non-zero for input devices, zero for output.
 * @return Device index or paNoDevice if not found.
 */
static PaDeviceIndex
find_device_by_name(const char* name, int is_input) {
    if (!name || !name[0]) {
        return is_input ? Pa_GetDefaultInputDevice() : Pa_GetDefaultOutputDevice();
    }

    int count = Pa_GetDeviceCount();
    if (count < 0) {
        return paNoDevice;
    }

    for (int i = 0; i < count; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) {
            continue;
        }

        /* Check if device supports the required direction */
        if (is_input && info->maxInputChannels <= 0) {
            continue;
        }
        if (!is_input && info->maxOutputChannels <= 0) {
            continue;
        }

        /* Match by name (case-sensitive) */
        if (strcmp(info->name, name) == 0) {
            return i;
        }
    }

    /* Try partial match */
    for (int i = 0; i < count; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) {
            continue;
        }

        if (is_input && info->maxInputChannels <= 0) {
            continue;
        }
        if (!is_input && info->maxOutputChannels <= 0) {
            continue;
        }

        if (strstr(info->name, name) != NULL) {
            return i;
        }
    }

    return paNoDevice;
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

int
dsd_audio_init(void) {
    if (s_initialized) {
        return 0;
    }

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        set_error_pa(err);
        return -1;
    }

    s_initialized = 1;
    set_error(NULL);
    return 0;
}

void
dsd_audio_cleanup(void) {
    if (s_initialized) {
        Pa_Terminate();
        s_initialized = 0;
    }
}

int
dsd_audio_enumerate_devices(dsd_audio_device* inputs, dsd_audio_device* outputs, int max_count) {
    if (!s_initialized) {
        if (dsd_audio_init() != 0) {
            return -1;
        }
    }

    if (inputs) {
        memset(inputs, 0, (size_t)max_count * sizeof(dsd_audio_device));
    }
    if (outputs) {
        memset(outputs, 0, (size_t)max_count * sizeof(dsd_audio_device));
    }

    int num_devices = Pa_GetDeviceCount();
    if (num_devices < 0) {
        set_error_pa(num_devices);
        return -1;
    }

    int in_idx = 0;
    int out_idx = 0;

    for (int i = 0; i < num_devices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) {
            continue;
        }

        /* Add to input list */
        if (info->maxInputChannels > 0 && inputs && in_idx < max_count) {
            dsd_audio_device* dev = &inputs[in_idx];
            dev->index = i;
            strncpy(dev->name, info->name, sizeof(dev->name) - 1);
            dev->name[sizeof(dev->name) - 1] = '\0';
            strncpy(dev->description, info->name, sizeof(dev->description) - 1);
            dev->description[sizeof(dev->description) - 1] = '\0';
            dev->is_input = 1;
            dev->is_output = 0;
            dev->initialized = 1;
            in_idx++;
        }

        /* Add to output list */
        if (info->maxOutputChannels > 0 && outputs && out_idx < max_count) {
            dsd_audio_device* dev = &outputs[out_idx];
            dev->index = i;
            strncpy(dev->name, info->name, sizeof(dev->name) - 1);
            dev->name[sizeof(dev->name) - 1] = '\0';
            strncpy(dev->description, info->name, sizeof(dev->description) - 1);
            dev->description[sizeof(dev->description) - 1] = '\0';
            dev->is_input = 0;
            dev->is_output = 1;
            dev->initialized = 1;
            out_idx++;
        }
    }

    return 0;
}

int
dsd_audio_list_devices(void) {
    if (!s_initialized) {
        if (dsd_audio_init() != 0) {
            fprintf(stderr, "Error: Failed to initialize PortAudio: %s\n", dsd_audio_get_error());
            return -1;
        }
    }

    int num_devices = Pa_GetDeviceCount();
    if (num_devices < 0) {
        fprintf(stderr, "Error: Failed to enumerate devices: %s\n", Pa_GetErrorText(num_devices));
        return -1;
    }

    PaDeviceIndex default_in = Pa_GetDefaultInputDevice();
    PaDeviceIndex default_out = Pa_GetDefaultOutputDevice();

    printf("\nPortAudio version: %s\n\n", Pa_GetVersionText());

    /* List output devices */
    int out_count = 0;
    for (int i = 0; i < num_devices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxOutputChannels <= 0) {
            continue;
        }

        out_count++;
        printf("=======[ Output Device #%d ]=======%s\n", out_count, (i == default_out) ? " [DEFAULT]" : "");
        printf("Name: %s\n", info->name);
        printf("Index: %d\n", i);
        printf("Max Channels: %d\n", info->maxOutputChannels);
        printf("Default Sample Rate: %.0f Hz\n", info->defaultSampleRate);

        const PaHostApiInfo* host = Pa_GetHostApiInfo(info->hostApi);
        if (host) {
            printf("Host API: %s\n", host->name);
        }
        printf("\n");
    }

    /* List input devices */
    int in_count = 0;
    for (int i = 0; i < num_devices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxInputChannels <= 0) {
            continue;
        }

        in_count++;
        printf("=======[ Input Device #%d ]=======%s\n", in_count, (i == default_in) ? " [DEFAULT]" : "");
        printf("Name: %s\n", info->name);
        printf("Index: %d\n", i);
        printf("Max Channels: %d\n", info->maxInputChannels);
        printf("Default Sample Rate: %.0f Hz\n", info->defaultSampleRate);

        const PaHostApiInfo* host = Pa_GetHostApiInfo(info->hostApi);
        if (host) {
            printf("Host API: %s\n", host->name);
        }
        printf("\n");
    }

    return 0;
}

dsd_audio_stream*
dsd_audio_open_input(const dsd_audio_params* params) {
    if (!params) {
        set_error("NULL parameters");
        return NULL;
    }

    if (!s_initialized) {
        if (dsd_audio_init() != 0) {
            return NULL;
        }
    }

    PaDeviceIndex dev_idx = find_device_by_name(params->device, 1);
    if (dev_idx == paNoDevice) {
        set_error("No suitable input device found");
        return NULL;
    }

    const PaDeviceInfo* dev_info = Pa_GetDeviceInfo(dev_idx);
    if (!dev_info) {
        set_error("Failed to get device info");
        return NULL;
    }

    dsd_audio_stream* stream = calloc(1, sizeof(dsd_audio_stream));
    if (!stream) {
        set_error("Out of memory");
        return NULL;
    }

    PaStreamParameters input_params;
    input_params.device = dev_idx;
    input_params.channelCount = params->channels;
    input_params.sampleFormat = paInt16;
    input_params.suggestedLatency = dev_info->defaultLowInputLatency;
    input_params.hostApiSpecificStreamInfo = NULL;

    PaError err = Pa_OpenStream(&stream->handle, &input_params, NULL, (double)params->sample_rate,
                                paFramesPerBufferUnspecified, paNoFlag, NULL, /* No callback - blocking I/O */
                                NULL);

    if (err != paNoError) {
        set_error_pa(err);
        free(stream);
        return NULL;
    }

    err = Pa_StartStream(stream->handle);
    if (err != paNoError) {
        set_error_pa(err);
        Pa_CloseStream(stream->handle);
        free(stream);
        return NULL;
    }

    stream->is_input = 1;
    stream->channels = params->channels;
    stream->sample_rate = params->sample_rate;

    return stream;
}

dsd_audio_stream*
dsd_audio_open_output(const dsd_audio_params* params) {
    if (!params) {
        set_error("NULL parameters");
        return NULL;
    }

    if (!s_initialized) {
        if (dsd_audio_init() != 0) {
            return NULL;
        }
    }

    PaDeviceIndex dev_idx = find_device_by_name(params->device, 0);
    if (dev_idx == paNoDevice) {
        set_error("No suitable output device found");
        return NULL;
    }

    const PaDeviceInfo* dev_info = Pa_GetDeviceInfo(dev_idx);
    if (!dev_info) {
        set_error("Failed to get device info");
        return NULL;
    }

    dsd_audio_stream* stream = calloc(1, sizeof(dsd_audio_stream));
    if (!stream) {
        set_error("Out of memory");
        return NULL;
    }

    PaStreamParameters output_params;
    output_params.device = dev_idx;
    output_params.channelCount = params->channels;
    output_params.sampleFormat = paInt16;
    output_params.suggestedLatency = dev_info->defaultLowOutputLatency;
    output_params.hostApiSpecificStreamInfo = NULL;

    PaError err = Pa_OpenStream(&stream->handle, NULL, &output_params, (double)params->sample_rate,
                                paFramesPerBufferUnspecified, paNoFlag, NULL, /* No callback - blocking I/O */
                                NULL);

    if (err != paNoError) {
        set_error_pa(err);
        free(stream);
        return NULL;
    }

    err = Pa_StartStream(stream->handle);
    if (err != paNoError) {
        set_error_pa(err);
        Pa_CloseStream(stream->handle);
        free(stream);
        return NULL;
    }

    stream->is_input = 0;
    stream->channels = params->channels;
    stream->sample_rate = params->sample_rate;

    return stream;
}

int
dsd_audio_read(dsd_audio_stream* stream, int16_t* buffer, size_t frames) {
    if (!stream || !stream->handle || !buffer) {
        set_error("Invalid arguments");
        return -1;
    }

    if (!stream->is_input) {
        set_error("Cannot read from output stream");
        return -1;
    }

    PaError err = Pa_ReadStream(stream->handle, buffer, (unsigned long)frames);
    if (err != paNoError && err != paInputOverflowed) {
        set_error_pa(err);
        return -1;
    }

    return (int)frames;
}

int
dsd_audio_write(dsd_audio_stream* stream, const int16_t* buffer, size_t frames) {
    if (!stream || !stream->handle || !buffer) {
        set_error("Invalid arguments");
        return -1;
    }

    if (stream->is_input) {
        set_error("Cannot write to input stream");
        return -1;
    }

    PaError err = Pa_WriteStream(stream->handle, buffer, (unsigned long)frames);
    if (err != paNoError && err != paOutputUnderflowed) {
        set_error_pa(err);
        return -1;
    }

    return (int)frames;
}

void
dsd_audio_close(dsd_audio_stream* stream) {
    if (!stream) {
        return;
    }

    if (stream->handle) {
        Pa_StopStream(stream->handle);
        Pa_CloseStream(stream->handle);
    }

    free(stream);
}

int
dsd_audio_drain(dsd_audio_stream* stream) {
    if (!stream || !stream->handle) {
        return -1;
    }

    if (stream->is_input) {
        /* Drain doesn't apply to input streams */
        return 0;
    }

    /* PortAudio doesn't have a direct drain equivalent.
     * Stopping the stream will wait for buffered audio to finish. */
    PaError err = Pa_StopStream(stream->handle);
    if (err != paNoError) {
        set_error_pa(err);
        return -1;
    }

    /* Restart the stream for continued use */
    err = Pa_StartStream(stream->handle);
    if (err != paNoError) {
        set_error_pa(err);
        return -1;
    }

    return 0;
}

const char*
dsd_audio_get_error(void) {
    return s_last_error;
}

const char*
dsd_audio_backend_name(void) {
    return "portaudio";
}

#endif /* DSD_USE_PORTAUDIO */
