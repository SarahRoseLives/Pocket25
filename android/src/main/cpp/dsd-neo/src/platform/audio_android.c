// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 DSD-Flutter contributors
 */

/**
 * @file
 * @brief Android audio backend implementation using OpenSL ES.
 *
 * Provides audio output for decoded voice on Android devices.
 * Uses OpenSL ES for broad compatibility (API level 9+).
 */

#include <dsd-neo/platform/audio.h>

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <android/log.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "DSD-Audio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

/*============================================================================
 * Constants
 *============================================================================*/

#define AUDIO_BUFFER_COUNT 4
#define AUDIO_BUFFER_FRAMES 256  /* Smaller buffers for lower latency */

/*============================================================================
 * Internal Types
 *============================================================================*/

struct dsd_audio_stream {
    /* OpenSL ES objects */
    SLObjectItf player_obj;
    SLPlayItf player_play;
    SLAndroidSimpleBufferQueueItf player_queue;
    
    /* Audio parameters */
    int sample_rate;
    int channels;
    
    /* Double-buffering */
    int16_t* buffers[AUDIO_BUFFER_COUNT];
    size_t buffer_size;       /* Size in bytes */
    size_t buffer_frames;     /* Size in frames */
    int current_buffer;
    
    /* Ring buffer for incoming audio */
    int16_t* ring_buffer;
    size_t ring_size;         /* Size in frames */
    size_t ring_head;         /* Write position */
    size_t ring_tail;         /* Read position */
    pthread_mutex_t ring_mutex;
    pthread_cond_t ring_cond;
    
    int is_playing;
    int is_input;  /* Always 0 for now - output only */
};

/*============================================================================
 * Module State
 *============================================================================*/

static SLObjectItf s_engine_obj = NULL;
static SLEngineItf s_engine = NULL;
static SLObjectItf s_output_mix = NULL;
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
        LOGE("%s", msg);
    } else {
        s_last_error[0] = '\0';
    }
}

/* Ring buffer helpers */
static size_t
ring_available(dsd_audio_stream* s) {
    if (s->ring_head >= s->ring_tail) {
        return s->ring_head - s->ring_tail;
    }
    return s->ring_size - s->ring_tail + s->ring_head;
}

static size_t
ring_free(dsd_audio_stream* s) {
    return s->ring_size - ring_available(s) - 1;
}

static void
ring_write(dsd_audio_stream* s, const int16_t* data, size_t frames) {
    size_t samples = frames * (size_t)s->channels;
    for (size_t i = 0; i < samples; i++) {
        s->ring_buffer[s->ring_head * (size_t)s->channels + (i % (size_t)s->channels)] = data[i];
        if ((i + 1) % (size_t)s->channels == 0) {
            s->ring_head = (s->ring_head + 1) % s->ring_size;
        }
    }
}

static void
ring_read(dsd_audio_stream* s, int16_t* data, size_t frames) {
    size_t samples = frames * (size_t)s->channels;
    for (size_t i = 0; i < samples; i++) {
        data[i] = s->ring_buffer[s->ring_tail * (size_t)s->channels + (i % (size_t)s->channels)];
        if ((i + 1) % (size_t)s->channels == 0) {
            s->ring_tail = (s->ring_tail + 1) % s->ring_size;
        }
    }
}

/*============================================================================
 * OpenSL ES Callback
 *============================================================================*/

static void
player_callback(SLAndroidSimpleBufferQueueItf bq, void* context) {
    dsd_audio_stream* s = (dsd_audio_stream*)context;
    
    pthread_mutex_lock(&s->ring_mutex);
    
    int16_t* buf = s->buffers[s->current_buffer];
    size_t frames_to_read = s->buffer_frames;
    size_t available = ring_available(s);
    
    if (available >= frames_to_read) {
        ring_read(s, buf, frames_to_read);
    } else {
        /* Not enough data - output silence and what we have */
        memset(buf, 0, s->buffer_size);
        if (available > 0) {
            ring_read(s, buf, available);
        }
    }
    
    /* Signal that space is available */
    pthread_cond_signal(&s->ring_cond);
    pthread_mutex_unlock(&s->ring_mutex);
    
    /* Enqueue the buffer */
    SLresult result = (*bq)->Enqueue(bq, buf, (SLuint32)s->buffer_size);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to enqueue audio buffer: %d", (int)result);
    }
    
    s->current_buffer = (s->current_buffer + 1) % AUDIO_BUFFER_COUNT;
}

/*============================================================================
 * Public API
 *============================================================================*/

int
dsd_audio_init(void) {
    if (s_initialized) {
        return 0;
    }
    
    SLresult result;
    
    /* Create engine */
    result = slCreateEngine(&s_engine_obj, 0, NULL, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to create OpenSL ES engine");
        return -1;
    }
    
    /* Realize engine */
    result = (*s_engine_obj)->Realize(s_engine_obj, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to realize OpenSL ES engine");
        (*s_engine_obj)->Destroy(s_engine_obj);
        s_engine_obj = NULL;
        return -1;
    }
    
    /* Get engine interface */
    result = (*s_engine_obj)->GetInterface(s_engine_obj, SL_IID_ENGINE, &s_engine);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to get OpenSL ES engine interface");
        (*s_engine_obj)->Destroy(s_engine_obj);
        s_engine_obj = NULL;
        return -1;
    }
    
    /* Create output mix */
    result = (*s_engine)->CreateOutputMix(s_engine, &s_output_mix, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to create output mix");
        (*s_engine_obj)->Destroy(s_engine_obj);
        s_engine_obj = NULL;
        return -1;
    }
    
    /* Realize output mix */
    result = (*s_output_mix)->Realize(s_output_mix, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to realize output mix");
        (*s_output_mix)->Destroy(s_output_mix);
        (*s_engine_obj)->Destroy(s_engine_obj);
        s_output_mix = NULL;
        s_engine_obj = NULL;
        return -1;
    }
    
    s_initialized = 1;
    LOGI("OpenSL ES audio initialized");
    return 0;
}

void
dsd_audio_cleanup(void) {
    if (!s_initialized) {
        return;
    }
    
    if (s_output_mix) {
        (*s_output_mix)->Destroy(s_output_mix);
        s_output_mix = NULL;
    }
    
    if (s_engine_obj) {
        (*s_engine_obj)->Destroy(s_engine_obj);
        s_engine_obj = NULL;
        s_engine = NULL;
    }
    
    s_initialized = 0;
    LOGI("OpenSL ES audio cleanup complete");
}

int
dsd_audio_enumerate_devices(dsd_audio_device* inputs, dsd_audio_device* outputs, int max_count) {
    /* Android doesn't expose device enumeration through OpenSL ES */
    if (outputs && max_count > 0) {
        memset(&outputs[0], 0, sizeof(dsd_audio_device));
        outputs[0].index = 0;
        strncpy(outputs[0].name, "default", sizeof(outputs[0].name) - 1);
        strncpy(outputs[0].description, "Default Android Audio Output", sizeof(outputs[0].description) - 1);
        outputs[0].is_output = 1;
        outputs[0].initialized = 1;
    }
    
    if (inputs) {
        memset(inputs, 0, (size_t)max_count * sizeof(dsd_audio_device));
    }
    
    return 0;
}

int
dsd_audio_list_devices(void) {
    LOGI("Audio devices: default (Android Audio Output)");
    return 0;
}

dsd_audio_stream*
dsd_audio_open_input(const dsd_audio_params* params) {
    (void)params;
    set_error("Audio input not supported on Android");
    return NULL;
}

dsd_audio_stream*
dsd_audio_open_output(const dsd_audio_params* params) {
    if (!s_initialized) {
        if (dsd_audio_init() != 0) {
            return NULL;
        }
    }
    
    SLresult result;
    
    dsd_audio_stream* s = calloc(1, sizeof(dsd_audio_stream));
    if (!s) {
        set_error("Failed to allocate audio stream");
        return NULL;
    }
    
    s->sample_rate = params->sample_rate;
    s->channels = params->channels;
    s->buffer_frames = AUDIO_BUFFER_FRAMES;
    s->buffer_size = s->buffer_frames * (size_t)s->channels * sizeof(int16_t);
    
    /* Allocate playback buffers */
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        s->buffers[i] = calloc(s->buffer_frames * (size_t)s->channels, sizeof(int16_t));
        if (!s->buffers[i]) {
            set_error("Failed to allocate audio buffer");
            goto error;
        }
    }
    
    /* Allocate ring buffer (hold ~2 seconds of audio for bursty P25 Phase 2) */
    s->ring_size = (size_t)s->sample_rate * 2;
    s->ring_buffer = calloc(s->ring_size * (size_t)s->channels, sizeof(int16_t));
    if (!s->ring_buffer) {
        set_error("Failed to allocate ring buffer");
        goto error;
    }
    
    pthread_mutex_init(&s->ring_mutex, NULL);
    pthread_cond_init(&s->ring_cond, NULL);
    
    /* Configure audio source */
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
        AUDIO_BUFFER_COUNT
    };
    
    /* Map sample rate to OpenSL ES constant */
    SLuint32 sl_sample_rate;
    switch (s->sample_rate) {
        case 8000:  sl_sample_rate = SL_SAMPLINGRATE_8;    break;
        case 16000: sl_sample_rate = SL_SAMPLINGRATE_16;   break;
        case 22050: sl_sample_rate = SL_SAMPLINGRATE_22_05; break;
        case 44100: sl_sample_rate = SL_SAMPLINGRATE_44_1; break;
        case 48000: sl_sample_rate = SL_SAMPLINGRATE_48;   break;
        default:    sl_sample_rate = SL_SAMPLINGRATE_48;   break;
    }
    
    SLDataFormat_PCM format_pcm = {
        SL_DATAFORMAT_PCM,
        (SLuint32)s->channels,
        sl_sample_rate,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        s->channels == 2 ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT) : SL_SPEAKER_FRONT_CENTER,
        SL_BYTEORDER_LITTLEENDIAN
    };
    
    SLDataSource audio_src = {&loc_bufq, &format_pcm};
    
    /* Configure audio sink */
    SLDataLocator_OutputMix loc_outmix = {
        SL_DATALOCATOR_OUTPUTMIX,
        s_output_mix
    };
    SLDataSink audio_sink = {&loc_outmix, NULL};
    
    /* Create audio player */
    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    
    result = (*s_engine)->CreateAudioPlayer(s_engine, &s->player_obj,
                                            &audio_src, &audio_sink,
                                            1, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to create audio player");
        goto error;
    }
    
    /* Realize player */
    result = (*s->player_obj)->Realize(s->player_obj, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to realize audio player");
        goto error;
    }
    
    /* Get play interface */
    result = (*s->player_obj)->GetInterface(s->player_obj, SL_IID_PLAY, &s->player_play);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to get play interface");
        goto error;
    }
    
    /* Get buffer queue interface */
    result = (*s->player_obj)->GetInterface(s->player_obj, SL_IID_BUFFERQUEUE, &s->player_queue);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to get buffer queue interface");
        goto error;
    }
    
    /* Register callback */
    result = (*s->player_queue)->RegisterCallback(s->player_queue, player_callback, s);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to register callback");
        goto error;
    }
    
    /* Start playback */
    result = (*s->player_play)->SetPlayState(s->player_play, SL_PLAYSTATE_PLAYING);
    if (result != SL_RESULT_SUCCESS) {
        set_error("Failed to start playback");
        goto error;
    }
    
    s->is_playing = 1;
    
    /* Pre-fill ring buffer with silence (~1 second) to handle Phase 2 bursty audio */
    pthread_mutex_lock(&s->ring_mutex);
    size_t prefill_frames = s->ring_size / 2;  // Fill half the ring buffer
    int16_t* silence_buffer = (int16_t*)calloc(prefill_frames * (size_t)s->channels, sizeof(int16_t));
    if (silence_buffer) {
        ring_write(s, silence_buffer, prefill_frames);
        free(silence_buffer);
        LOGI("Pre-filled audio buffer with %zu frames of silence", prefill_frames);
    }
    pthread_mutex_unlock(&s->ring_mutex);
    
    /* Prime the buffers with silence to start the callback chain */
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        memset(s->buffers[i], 0, s->buffer_size);
        result = (*s->player_queue)->Enqueue(s->player_queue, s->buffers[i], (SLuint32)s->buffer_size);
        if (result != SL_RESULT_SUCCESS) {
            LOGE("Failed to enqueue initial buffer %d", i);
        }
    }
    
    LOGI("Audio output opened: %d Hz, %d ch", s->sample_rate, s->channels);
    return s;
    
error:
    if (s->player_obj) {
        (*s->player_obj)->Destroy(s->player_obj);
    }
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        free(s->buffers[i]);
    }
    free(s->ring_buffer);
    free(s);
    return NULL;
}

int
dsd_audio_read(dsd_audio_stream* stream, int16_t* buffer, size_t frames) {
    (void)stream;
    (void)buffer;
    (void)frames;
    set_error("Audio input not supported on Android");
    return -1;
}

int
dsd_audio_write(dsd_audio_stream* stream, const int16_t* buffer, size_t frames) {
    if (!stream || !buffer || frames == 0) {
        return 0;
    }
    
    pthread_mutex_lock(&stream->ring_mutex);
    
    size_t free_frames = ring_free(stream);
    
    /* If not enough space, advance tail to make room (drop oldest samples) */
    if (free_frames < frames) {
        size_t frames_to_drop = frames - free_frames;
        stream->ring_tail = (stream->ring_tail + frames_to_drop) % stream->ring_size;
    }
    
    /* Write all frames - we've guaranteed space */
    ring_write(stream, buffer, frames);
    
    pthread_mutex_unlock(&stream->ring_mutex);
    
    return (int)frames;
}

int
dsd_audio_drain(dsd_audio_stream* stream) {
    if (!stream) {
        return 0;
    }
    
    /* Wait for ring buffer to empty */
    pthread_mutex_lock(&stream->ring_mutex);
    while (ring_available(stream) > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000; /* 100ms */
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        int rc = pthread_cond_timedwait(&stream->ring_cond, &stream->ring_mutex, &ts);
        if (rc != 0) {
            break; /* Timeout */
        }
    }
    pthread_mutex_unlock(&stream->ring_mutex);
    
    return 0;
}

void
dsd_audio_close(dsd_audio_stream* stream) {
    if (!stream) {
        return;
    }
    
    LOGI("Closing audio stream");
    
    /* Stop playback */
    if (stream->player_play) {
        (*stream->player_play)->SetPlayState(stream->player_play, SL_PLAYSTATE_STOPPED);
    }
    
    /* Destroy player */
    if (stream->player_obj) {
        (*stream->player_obj)->Destroy(stream->player_obj);
    }
    
    /* Clean up synchronization */
    pthread_mutex_destroy(&stream->ring_mutex);
    pthread_cond_destroy(&stream->ring_cond);
    
    /* Free buffers */
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        free(stream->buffers[i]);
    }
    free(stream->ring_buffer);
    free(stream);
}

const char*
dsd_audio_get_error(void) {
    return s_last_error;
}

const char*
dsd_audio_backend_name(void) {
    return "opensl";
}
