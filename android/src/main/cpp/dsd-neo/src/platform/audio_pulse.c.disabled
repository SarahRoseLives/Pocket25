// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief PulseAudio backend implementation for the audio abstraction layer.
 *
 * This file provides the PulseAudio implementation of the cross-platform
 * audio API defined in dsd-neo/platform/audio.h. It wraps the pa_simple API
 * for stream I/O and uses the mainloop API for device enumeration.
 */

#include <dsd-neo/platform/platform.h>

#if DSD_PLATFORM_POSIX

#include <dsd-neo/platform/audio.h>

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Internal Types
 *============================================================================*/

struct dsd_audio_stream {
    pa_simple* handle;
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
set_error_pa(int pa_errno) {
    set_error(pa_strerror(pa_errno));
}

/*============================================================================
 * Device Enumeration Callbacks
 *============================================================================*/

typedef struct {
    dsd_audio_device* devices;
    int max_count;
    int current;
} enum_context;

static void
pa_enum_state_cb(pa_context* c, void* userdata) {
    int* ready = userdata;
    pa_context_state_t state = pa_context_get_state(c);

    switch (state) {
        case PA_CONTEXT_READY: *ready = 1; break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED: *ready = 2; break;
        default: break;
    }
}

static void
pa_sink_enum_cb(pa_context* c, const pa_sink_info* info, int eol, void* userdata) {
    enum_context* ctx = userdata;
    (void)c;

    if (eol > 0 || !info) {
        return;
    }

    if (ctx->current >= ctx->max_count) {
        return;
    }

    dsd_audio_device* dev = &ctx->devices[ctx->current];
    dev->index = (int)info->index;
    strncpy(dev->name, info->name, sizeof(dev->name) - 1);
    dev->name[sizeof(dev->name) - 1] = '\0';
    if (info->description) {
        strncpy(dev->description, info->description, sizeof(dev->description) - 1);
        dev->description[sizeof(dev->description) - 1] = '\0';
    } else {
        dev->description[0] = '\0';
    }
    dev->is_input = 0;
    dev->is_output = 1;
    dev->initialized = 1;
    ctx->current++;
}

static void
pa_source_enum_cb(pa_context* c, const pa_source_info* info, int eol, void* userdata) {
    enum_context* ctx = userdata;
    (void)c;

    if (eol > 0 || !info) {
        return;
    }

    if (ctx->current >= ctx->max_count) {
        return;
    }

    dsd_audio_device* dev = &ctx->devices[ctx->current];
    dev->index = (int)info->index;
    strncpy(dev->name, info->name, sizeof(dev->name) - 1);
    dev->name[sizeof(dev->name) - 1] = '\0';
    if (info->description) {
        strncpy(dev->description, info->description, sizeof(dev->description) - 1);
        dev->description[sizeof(dev->description) - 1] = '\0';
    } else {
        dev->description[0] = '\0';
    }
    dev->is_input = 1;
    dev->is_output = 0;
    dev->initialized = 1;
    ctx->current++;
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

int
dsd_audio_init(void) {
    if (s_initialized) {
        return 0;
    }
    /* PulseAudio doesn't require explicit initialization for pa_simple */
    s_initialized = 1;
    set_error(NULL);
    return 0;
}

void
dsd_audio_cleanup(void) {
    s_initialized = 0;
}

int
dsd_audio_enumerate_devices(dsd_audio_device* inputs, dsd_audio_device* outputs, int max_count) {
    pa_mainloop* ml = NULL;
    pa_mainloop_api* mlapi = NULL;
    pa_context* ctx = NULL;
    pa_operation* op = NULL;
    int ready = 0;
    int state = 0;
    int ret = -1;

    if (inputs) {
        memset(inputs, 0, (size_t)max_count * sizeof(dsd_audio_device));
    }
    if (outputs) {
        memset(outputs, 0, (size_t)max_count * sizeof(dsd_audio_device));
    }

    ml = pa_mainloop_new();
    if (!ml) {
        set_error("Failed to create PulseAudio mainloop");
        return -1;
    }

    mlapi = pa_mainloop_get_api(ml);
    ctx = pa_context_new(mlapi, "dsd-neo-enum");
    if (!ctx) {
        set_error("Failed to create PulseAudio context");
        pa_mainloop_free(ml);
        return -1;
    }

    if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        set_error_pa(pa_context_errno(ctx));
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return -1;
    }

    pa_context_set_state_callback(ctx, pa_enum_state_cb, &ready);

    enum_context out_ctx = {outputs, max_count, 0};
    enum_context in_ctx = {inputs, max_count, 0};

    /* State machine for async enumeration */
    for (;;) {
        if (ready == 0) {
            pa_mainloop_iterate(ml, 1, NULL);
            continue;
        }

        if (ready == 2) {
            set_error("PulseAudio connection failed");
            goto cleanup;
        }

        switch (state) {
            case 0: /* Enumerate sinks (outputs) */
                if (outputs) {
                    op = pa_context_get_sink_info_list(ctx, pa_sink_enum_cb, &out_ctx);
                }
                state++;
                break;

            case 1: /* Wait for sink enumeration */
                if (!op || pa_operation_get_state(op) == PA_OPERATION_DONE) {
                    if (op) {
                        pa_operation_unref(op);
                        op = NULL;
                    }
                    /* Enumerate sources (inputs) */
                    if (inputs) {
                        op = pa_context_get_source_info_list(ctx, pa_source_enum_cb, &in_ctx);
                    }
                    state++;
                }
                break;

            case 2: /* Wait for source enumeration */
                if (!op || pa_operation_get_state(op) == PA_OPERATION_DONE) {
                    if (op) {
                        pa_operation_unref(op);
                        op = NULL;
                    }
                    ret = 0;
                    goto cleanup;
                }
                break;
            default: set_error("Internal error: unexpected enumeration state"); goto cleanup;
        }

        pa_mainloop_iterate(ml, 1, NULL);
    }

cleanup:
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return ret;
}

int
dsd_audio_list_devices(void) {
    dsd_audio_device inputs[16];
    dsd_audio_device outputs[16];

    if (dsd_audio_enumerate_devices(inputs, outputs, 16) < 0) {
        fprintf(stderr, "Error: Failed to enumerate audio devices: %s\n", dsd_audio_get_error());
        return -1;
    }

    printf("\n");

    /* Print outputs (sinks) */
    for (int i = 0; i < 16; i++) {
        if (!outputs[i].initialized) {
            break;
        }
        printf("=======[ Output Device #%d ]=======\n", i + 1);
        printf("Description: %s\n", outputs[i].description);
        printf("Name: %s\n", outputs[i].name);
        printf("Index: %d\n", outputs[i].index);
        printf("\n");
    }

    /* Print inputs (sources) */
    for (int i = 0; i < 16; i++) {
        if (!inputs[i].initialized) {
            break;
        }
        printf("=======[ Input Device #%d ]=======\n", i + 1);
        printf("Description: %s\n", inputs[i].description);
        printf("Name: %s\n", inputs[i].name);
        printf("Index: %d\n", inputs[i].index);
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

    dsd_audio_stream* stream = calloc(1, sizeof(dsd_audio_stream));
    if (!stream) {
        set_error("Out of memory");
        return NULL;
    }

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16NE;
    ss.channels = (uint8_t)params->channels;
    ss.rate = (uint32_t)params->sample_rate;

    /* Buffer attributes for low latency input */
    pa_buffer_attr attr;
    attr.maxlength = (uint32_t)-1;
    attr.tlength = (uint32_t)-1;
    attr.prebuf = (uint32_t)-1;
    /* fragsize tuned for ~100ms latency at 48kHz stereo */
    attr.fragsize = (uint32_t)(params->sample_rate * params->channels * 2 / 10);

    int err = 0;
    const char* dev = (params->device && params->device[0]) ? params->device : NULL;
    const char* app = (params->app_name && params->app_name[0]) ? params->app_name : "DSD-neo";

    stream->handle = pa_simple_new(NULL, app, PA_STREAM_RECORD, dev, "Audio Input", &ss, NULL, &attr, &err);

    if (!stream->handle) {
        set_error_pa(err);
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

    dsd_audio_stream* stream = calloc(1, sizeof(dsd_audio_stream));
    if (!stream) {
        set_error("Out of memory");
        return NULL;
    }

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16NE;
    ss.channels = (uint8_t)params->channels;
    ss.rate = (uint32_t)params->sample_rate;

    int err = 0;
    const char* dev = (params->device && params->device[0]) ? params->device : NULL;
    const char* app = (params->app_name && params->app_name[0]) ? params->app_name : "DSD-neo";

    stream->handle = pa_simple_new(NULL, app, PA_STREAM_PLAYBACK, dev, "Audio Output", &ss, NULL, NULL, &err);

    if (!stream->handle) {
        set_error_pa(err);
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

    size_t bytes = frames * (size_t)stream->channels * sizeof(int16_t);
    int err = 0;

    if (pa_simple_read(stream->handle, buffer, bytes, &err) < 0) {
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

    size_t bytes = frames * (size_t)stream->channels * sizeof(int16_t);
    int err = 0;

    if (pa_simple_write(stream->handle, buffer, bytes, &err) < 0) {
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
        pa_simple_free(stream->handle);
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

    int err = 0;
    if (pa_simple_drain(stream->handle, &err) < 0) {
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
    return "pulse";
}

#endif /* DSD_PLATFORM_POSIX */
