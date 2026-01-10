// SPDX-License-Identifier: GPL-3.0-or-later
// Stub audio implementation for Android - no real audio I/O needed for control channel decoding

#include <dsd-neo/platform/audio.h>
#include <stdlib.h>
#include <string.h>

// Stub audio stream structure
struct dsd_audio_stream {
    int dummy;
};

int dsd_audio_init(void) {
    return 0;
}

void dsd_audio_cleanup(void) {
}

int dsd_audio_enumerate_devices(dsd_audio_device* inputs, dsd_audio_device* outputs, int max_count) {
    (void)inputs; (void)outputs; (void)max_count;
    return 0;
}

int dsd_audio_list_devices(void) {
    return 0;
}

dsd_audio_stream* dsd_audio_open_input(const dsd_audio_params* params) {
    (void)params;
    return NULL;
}

dsd_audio_stream* dsd_audio_open_output(const dsd_audio_params* params) {
    (void)params;
    return NULL;
}

int dsd_audio_read(dsd_audio_stream* stream, int16_t* buffer, size_t samples) {
    (void)stream; (void)buffer; (void)samples;
    return -1; // Always fail - no real audio
}

int dsd_audio_write(dsd_audio_stream* stream, const int16_t* buffer, size_t samples) {
    (void)stream; (void)buffer; (void)samples;
    return -1; // Always fail - no real audio
}

int dsd_audio_drain(dsd_audio_stream* stream) {
    (void)stream;
    return 0;
}

void dsd_audio_close(dsd_audio_stream* stream) {
    if (stream) {
        free(stream);
    }
}

const char* dsd_audio_get_error(void) {
    return "Audio not available on Android";
}
