// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*
 * Copyright (C) 2010 DSD Author
 * GPG Key ID: 0x3F1D7FD0 (74EF 430D F7F2 0A48 FCE6  F630 FAA2 635D 3F1D 7FD0)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/audio.h>
#include <dsd-neo/platform/file_compat.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/runtime/log.h>
#include <dsd-neo/runtime/net_audio_input_hooks.h>
#include <dsd-neo/runtime/udp_audio_hooks.h>

#include <sndfile.h>

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
closeAudioOutput(dsd_opts* opts) {
    /* Close primary audio output stream */
    if (opts->audio_out_stream) {
        dsd_audio_close(opts->audio_out_stream);
        opts->audio_out_stream = NULL;
    }
    /* Close secondary output stream (slot 2/right) */
    if (opts->audio_out_streamR) {
        dsd_audio_close(opts->audio_out_streamR);
        opts->audio_out_streamR = NULL;
    }
    /* Close raw/analog audio output stream */
    if (opts->audio_raw_out) {
        dsd_audio_close(opts->audio_raw_out);
        opts->audio_raw_out = NULL;
    }
}

void
closeAudioInput(dsd_opts* opts) {
    if (opts->audio_in_stream) {
        dsd_audio_close(opts->audio_in_stream);
        opts->audio_in_stream = NULL;
    }
}

int
openAudioOutput(dsd_opts* opts) {
    const char* dev = NULL;
    if (opts->pa_output_idx[0] != '\0') {
        dev = opts->pa_output_idx;
    }

    dsd_audio_params params;
    params.device = dev;
    params.app_name = "DSD-neo";

    /* Open raw/analog output stream for ProVoice or analog monitor mode */
    if (opts->frame_provoice == 1 || opts->monitor_input_audio == 1) {
        params.sample_rate = opts->pulse_raw_rate_out;
        params.channels = opts->pulse_raw_out_channels;
        params.bits_per_sample = 16;
        opts->audio_raw_out = dsd_audio_open_output(&params);
        if (!opts->audio_raw_out) {
            LOG_ERROR("Failed to open raw audio output: %s", dsd_audio_get_error());
            return -1;
        }
    }

    /* Open main digital audio output stream (unless in analog-only mode) */
    if (opts->analog_only == 0) {
        params.sample_rate = opts->pulse_digi_rate_out;
        params.channels = opts->pulse_digi_out_channels;
        params.bits_per_sample = 16;
        opts->audio_out_stream = dsd_audio_open_output(&params);
        if (!opts->audio_out_stream) {
            LOG_ERROR("Failed to open audio output: %s", dsd_audio_get_error());
            if (opts->audio_raw_out) {
                dsd_audio_close(opts->audio_raw_out);
                opts->audio_raw_out = NULL;
            }
            return -1;
        }
    }
    return 0;
}

int
openAudioInput(dsd_opts* opts) {
    const char* dev = NULL;
    if (opts->pa_input_idx[0] != '\0') {
        dev = opts->pa_input_idx;
    }

    dsd_audio_params params;
    params.sample_rate = opts->pulse_digi_rate_in;
    params.channels = opts->pulse_digi_in_channels;
    params.bits_per_sample = 16;
    params.device = dev;
    params.app_name = (opts->m17encoder == 1) ? "DSD-neo M17" : "DSD-neo";

    opts->audio_in_stream = dsd_audio_open_input(&params);
    if (!opts->audio_in_stream) {
        LOG_ERROR("Failed to open audio input: %s", dsd_audio_get_error());
        return -1;
    }
    return 0;
}

void
dsd_drain_audio_output(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    /* Only act if audio output is enabled */
    if (opts->audio_out != 1) {
        return;
    }
    /* Audio stream: drain any queued samples */
    if (opts->audio_out_type == 0) {
        if (opts->audio_out_stream) {
            (void)dsd_audio_drain(opts->audio_out_stream);
        }
        if (opts->audio_raw_out) {
            (void)dsd_audio_drain(opts->audio_raw_out);
        }
        return;
    }
    /* UDP/STDOUT: nothing meaningful to drain; attempt fsync for file descriptors */
    if (opts->audio_out_type == 1 || opts->audio_out_type == 8) {
        if (opts->audio_out_fd >= 0) {
            (void)dsd_fsync(opts->audio_out_fd);
        }
        return;
    }
}

void
parse_audio_input_string(dsd_opts* opts, char* input) {
    char* curr;
    curr = strtok(input, ":");
    if (curr != NULL) {
        strncpy(opts->pa_input_idx, curr, 99);
        opts->pa_input_idx[99] = '\0';
        fprintf(stderr, "\n");
        fprintf(stderr, "Audio Input Device: %s; ", opts->pa_input_idx);
        fprintf(stderr, "\n");
    }
}

void
parse_audio_output_string(dsd_opts* opts, char* input) {
    char* curr;
    curr = strtok(input, ":");
    if (curr != NULL) {
        strncpy(opts->pa_output_idx, curr, 99);
        opts->pa_output_idx[99] = '\0';
        fprintf(stderr, "\n");
        fprintf(stderr, "Audio Output Device: %s; ", opts->pa_output_idx);
        fprintf(stderr, "\n");
    }
}

int
audio_list_devices(void) {
    return dsd_audio_list_devices();
}

void
processAudio(dsd_opts* opts, dsd_state* state) {

    int i, n;
    float aout_abs, max, gainfactor, gaindelta, maxbuf;

    if (opts->audio_gain == (float)0) {
        // detect max level
        max = 0;

        state->audio_out_temp_buf_p = state->audio_out_temp_buf;
        for (n = 0; n < 160; n++) {
            aout_abs = fabsf(*state->audio_out_temp_buf_p);
            if (aout_abs > max) {
                max = aout_abs;
            }
            state->audio_out_temp_buf_p++;
        }
        *state->aout_max_buf_p = max;

        state->aout_max_buf_p++;

        state->aout_max_buf_idx++;

        if (state->aout_max_buf_idx > 24) {
            state->aout_max_buf_idx = 0;
            state->aout_max_buf_p = state->aout_max_buf;
        }

        // lookup max history
        for (i = 0; i < 25; i++) {
            maxbuf = state->aout_max_buf[i];
            if (maxbuf > max) {
                max = maxbuf;
            }
        }

        // determine optimal gain level
        if (max > (float)0) {
            gainfactor = ((float)30000 / max);
        } else {
            gainfactor = (float)50;
        }
        if (gainfactor < state->aout_gain) {
            state->aout_gain = gainfactor;
            gaindelta = (float)0;
        } else {
            if (gainfactor > (float)50) {
                gainfactor = (float)50;
            }
            gaindelta = gainfactor - state->aout_gain;
            if (gaindelta > ((float)0.05 * state->aout_gain)) {
                gaindelta = ((float)0.05 * state->aout_gain);
            }
        }
        gaindelta /= (float)160;
    } else {
        gaindelta = (float)0;
    }

    if (opts->audio_gain >= 0) {
        // adjust output gain
        state->audio_out_temp_buf_p = state->audio_out_temp_buf;
        for (n = 0; n < 160; n++) {
            *state->audio_out_temp_buf_p = (state->aout_gain + ((float)n * gaindelta)) * (*state->audio_out_temp_buf_p);
            state->audio_out_temp_buf_p++;
        }
        state->aout_gain += ((float)160 * gaindelta);
    }

    // copy audio data to output buffer and upsample if necessary
    state->audio_out_temp_buf_p = state->audio_out_temp_buf;
    //we only want to upsample when using sample rates greater than 8k for output
    if (opts->pulse_digi_rate_out > 8000) {
        for (n = 0; n < 160; n++) {
            upsample(state, *state->audio_out_temp_buf_p);
            state->audio_out_temp_buf_p++;
            state->audio_out_float_buf_p += 6;
            state->audio_out_idx += 6;
            state->audio_out_idx2 += 6;
        }
        state->audio_out_float_buf_p -= (960 + opts->playoffset);
        // copy to output (short) buffer
        for (n = 0; n < 960; n++) {
            if (*state->audio_out_float_buf_p > 32767.0F) {
                *state->audio_out_float_buf_p = 32767.0F;
            } else if (*state->audio_out_float_buf_p < -32768.0F) {
                *state->audio_out_float_buf_p = -32768.0F;
            }
            *state->audio_out_buf_p = (short)*state->audio_out_float_buf_p;
            //tap the pointer here and store the short upsample buffer samples
            state->s_lu[n] = (short)*state->audio_out_float_buf_p;
            state->audio_out_buf_p++;
            state->audio_out_float_buf_p++;
        }
        state->audio_out_float_buf_p += opts->playoffset;
    } else {
        for (n = 0; n < 160; n++) {
            if (*state->audio_out_temp_buf_p > 32767.0F) {
                *state->audio_out_temp_buf_p = 32767.0F;
            } else if (*state->audio_out_temp_buf_p < -32768.0F) {
                *state->audio_out_temp_buf_p = -32768.0F;
            }
            *state->audio_out_buf_p = (short)*state->audio_out_temp_buf_p;
            //tap the pointer here and store the short buffer samples
            state->s_l[n] = (short)*state->audio_out_temp_buf_p;
            //debug
            // fprintf (stderr, " %d", state->s_l[n]);
            state->audio_out_buf_p++;
            state->audio_out_temp_buf_p++;
            state->audio_out_idx++;
            state->audio_out_idx2++;
        }
    }
}

void
processAudioR(dsd_opts* opts, dsd_state* state) {

    int i, n;
    float aout_abs, max, gainfactor, gaindelta, maxbuf;
    if (opts->audio_gainR == (float)0) {
        // detect max level
        max = 0;

        state->audio_out_temp_buf_pR = state->audio_out_temp_bufR;
        for (n = 0; n < 160; n++) {
            aout_abs = fabsf(*state->audio_out_temp_buf_pR);
            if (aout_abs > max) {
                max = aout_abs;
            }
            state->audio_out_temp_buf_pR++;
        }
        *state->aout_max_buf_pR = max;

        state->aout_max_buf_pR++;

        state->aout_max_buf_idxR++;

        if (state->aout_max_buf_idxR > 24) {
            state->aout_max_buf_idxR = 0;
            state->aout_max_buf_pR = state->aout_max_bufR;
        }

        // lookup max history
        for (i = 0; i < 25; i++) {
            maxbuf = state->aout_max_bufR[i];
            if (maxbuf > max) {
                max = maxbuf;
            }
        }

        // determine optimal gain level
        if (max > (float)0) {
            gainfactor = ((float)30000 / max);
        } else {
            gainfactor = (float)50;
        }
        if (gainfactor < state->aout_gainR) {
            state->aout_gainR = gainfactor;
            gaindelta = (float)0;
        } else {
            if (gainfactor > (float)50) {
                gainfactor = (float)50;
            }
            gaindelta = gainfactor - state->aout_gainR;
            if (gaindelta > ((float)0.05 * state->aout_gainR)) {
                gaindelta = ((float)0.05 * state->aout_gainR);
            }
        }
        gaindelta /= (float)160;
    } else {
        gaindelta = (float)0;
    }

    if (opts->audio_gainR >= 0) {
        // adjust output gain
        state->audio_out_temp_buf_pR = state->audio_out_temp_bufR;
        for (n = 0; n < 160; n++) {
            *state->audio_out_temp_buf_pR =
                (state->aout_gainR + ((float)n * gaindelta)) * (*state->audio_out_temp_buf_pR);
            state->audio_out_temp_buf_pR++;
        }
        state->aout_gainR += ((float)160 * gaindelta);
    }

    // copy audio data to output buffer and upsample if necessary
    state->audio_out_temp_buf_pR = state->audio_out_temp_bufR;
    //we only want to upsample when using sample rates greater than 8k for output,
    if (opts->pulse_digi_rate_out > 8000) {
        for (n = 0; n < 160; n++) {
            upsample(state, *state->audio_out_temp_buf_pR);
            state->audio_out_temp_buf_pR++;
            state->audio_out_float_buf_pR += 6;
            state->audio_out_idxR += 6;
            state->audio_out_idx2R += 6;
        }
        state->audio_out_float_buf_pR -= (960 + opts->playoffsetR);
        // copy to output (short) buffer
        for (n = 0; n < 960; n++) {
            if (*state->audio_out_float_buf_pR > 32767.0F) {
                *state->audio_out_float_buf_pR = 32767.0F;
            } else if (*state->audio_out_float_buf_pR < -32768.0F) {
                *state->audio_out_float_buf_pR = -32768.0F;
            }
            *state->audio_out_buf_pR = (short)*state->audio_out_float_buf_pR;
            //tap the pointer here and store the short upsample buffer samples
            state->s_ru[n] = (short)*state->audio_out_float_buf_pR;
            state->audio_out_buf_pR++;
            state->audio_out_float_buf_pR++;
        }
        state->audio_out_float_buf_pR += opts->playoffsetR;
    } else {
        for (n = 0; n < 160; n++) {
            if (*state->audio_out_temp_buf_pR > 32767.0F) {
                *state->audio_out_temp_buf_pR = 32767.0F;
            } else if (*state->audio_out_temp_buf_pR < -32768.0F) {
                *state->audio_out_temp_buf_pR = -32768.0F;
            }
            *state->audio_out_buf_pR = (short)*state->audio_out_temp_buf_pR;
            //tap the pointer here and store the short buffer samples
            state->s_r[n] = (short)*state->audio_out_temp_buf_pR;
            state->audio_out_buf_pR++;
            state->audio_out_temp_buf_pR++;
            state->audio_out_idxR++;
            state->audio_out_idx2R++;
        }
    }
}

void
writeSynthesizedVoice(dsd_opts* opts, dsd_state* state) {
    int n;
    short aout_buf[160];
    short* aout_buf_p;

    aout_buf_p = aout_buf;
    state->audio_out_temp_buf_p = state->audio_out_temp_buf;

    for (n = 0; n < 160; n++) {
        if (*state->audio_out_temp_buf_p > (float)32767) {
            *state->audio_out_temp_buf_p = (float)32767;
        } else if (*state->audio_out_temp_buf_p < (float)-32768) {
            *state->audio_out_temp_buf_p = (float)-32768;
        }
        *aout_buf_p = (short)*state->audio_out_temp_buf_p;
        aout_buf_p++;
        state->audio_out_temp_buf_p++;
    }

    sf_write_short(opts->wav_out_f, aout_buf, 160);
}

void
writeSynthesizedVoiceR(dsd_opts* opts, dsd_state* state) {
    int n;
    short aout_buf[160];
    short* aout_buf_p;

    aout_buf_p = aout_buf;
    state->audio_out_temp_buf_pR = state->audio_out_temp_bufR;

    for (n = 0; n < 160; n++) {
        if (*state->audio_out_temp_buf_pR > (float)32767) {
            *state->audio_out_temp_buf_pR = (float)32767;
        } else if (*state->audio_out_temp_buf_pR < (float)-32768) {
            *state->audio_out_temp_buf_pR = (float)-32768;
        }
        *aout_buf_p = (short)*state->audio_out_temp_buf_pR;
        aout_buf_p++;
        state->audio_out_temp_buf_pR++;
    }

    sf_write_short(opts->wav_out_fR, aout_buf, 160);
}

//short Mono to Stereo version for new static .wav files in stereo format for TDMA
void
writeSynthesizedVoiceMS(dsd_opts* opts, dsd_state* state) {
    int n;
    short aout_buf[160];
    short* aout_buf_p;

    aout_buf_p = aout_buf;
    state->audio_out_temp_buf_p = state->audio_out_temp_buf;

    for (n = 0; n < 160; n++) {
        if (*state->audio_out_temp_buf_p > (float)32767) {
            *state->audio_out_temp_buf_p = (float)32767;
        } else if (*state->audio_out_temp_buf_p < (float)-32768) {
            *state->audio_out_temp_buf_p = (float)-32768;
        }
        *aout_buf_p = (short)*state->audio_out_temp_buf_p;
        aout_buf_p++;
        state->audio_out_temp_buf_p++;
    }

    short ss[320];
    for (n = 0; n < 160; n++) {
        ss[(n * 2) + 0] = aout_buf[n];
        ss[(n * 2) + 1] = aout_buf[n];
    }

    sf_write_short(opts->wav_out_f, ss, 320);
}

void
writeRawSample(dsd_opts* opts, dsd_state* state, short sample) {
    UNUSED(state);

    //short aout_buf[160];
    //sf_write_short(opts->wav_out_raw, aout_buf, 160);

    //only write if actual audio, truncate silence
    if (sample != 0) {
        sf_write_short(opts->wav_out_raw, &sample, 2); //2 to match pulseaudio input sample read
    }
}

void
playSynthesizedVoice(dsd_opts* opts, dsd_state* state) {

    //don't synthesize voice if slot is turned off
    if (opts->slot1_on == 0) {
        //clear any previously buffered audio
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
        state->audio_out_idx = 0;
        goto end_psv;
    }

    if (state->audio_out_idx > opts->delay) {
        if (opts->audio_out == 1 && opts->audio_out_type == 1) {
            ssize_t written = dsd_write(opts->audio_out_fd, (state->audio_out_buf_p - state->audio_out_idx),
                                        (size_t)state->audio_out_idx * sizeof(short));
            if (written < 0) {
                LOG_WARN("playSynthesizedVoice: failed to write %zu bytes to audio_out_fd",
                         (size_t)state->audio_out_idx * sizeof(short));
            }
            state->audio_out_idx = 0;
        } else if (opts->audio_out == 1 && opts->audio_out_type == 0) {
            /* Use audio abstraction layer */
            if (opts->audio_out_stream) {
                dsd_audio_write(opts->audio_out_stream, (state->audio_out_buf_p - state->audio_out_idx),
                                (size_t)state->audio_out_idx);
            }
            state->audio_out_idx = 0;
        } else if (opts->audio_out == 1
                   && opts->audio_out_type == 8) //UDP Audio Out -- Forgot some things still use this for now
        {
            dsd_udp_audio_hook_blast(opts, state, (size_t)state->audio_out_idx * sizeof(short),
                                     (state->audio_out_buf_p - state->audio_out_idx));
            state->audio_out_idx = 0;
        } else {
            state->audio_out_idx = 0; //failsafe for audio_out == 0
        }
    }

end_psv:

    if (state->audio_out_idx2 >= 800000) {
        state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
        state->audio_out_buf_p = state->audio_out_buf + 100;
        memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
        memset(state->audio_out_buf, 0, 100 * sizeof(short));
        state->audio_out_idx2 = 0;
    }
}

void
playSynthesizedVoiceR(dsd_opts* opts, dsd_state* state) {

    if (state->audio_out_idxR > opts->delay) {
        // output synthesized speech to sound card
        if (opts->audio_out == 1 && opts->audio_out_type == 0) {
            /* Use audio abstraction layer */
            if (opts->audio_out_streamR) {
                dsd_audio_write(opts->audio_out_streamR, (state->audio_out_buf_pR - state->audio_out_idxR),
                                (size_t)state->audio_out_idxR);
            }
            state->audio_out_idxR = 0;
        } else if (
            opts->audio_out == 1
            && opts->audio_out_type
                   == 8) //UDP Audio Out -- Not sure how this would handle, but R never gets called anymore, so just here for symmetry
        {
            dsd_udp_audio_hook_blast(opts, state, (size_t)state->audio_out_idxR * sizeof(short),
                                     (state->audio_out_buf_pR - state->audio_out_idxR));
            state->audio_out_idxR = 0;
        } else {
            state->audio_out_idxR = 0; //failsafe for audio_out == 0
        }
    }

    if (state->audio_out_idx2R >= 800000) {
        state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
        state->audio_out_buf_pR = state->audio_out_bufR + 100;
        memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
        memset(state->audio_out_bufR, 0, 100 * sizeof(short));
        state->audio_out_idx2R = 0;
    }
}

void
openAudioOutDevice(dsd_opts* opts, int speed) {
    UNUSED(speed);

    /* Handle device type detection */
    if (strncmp(opts->audio_out_dev, "pulse", 5) == 0 || strncmp(opts->audio_out_dev, "pa:", 3) == 0) {
        opts->audio_out_type = 0; /* Audio stream output */
    }
    if (strncmp(opts->audio_in_dev, "pulse", 5) == 0) {
        opts->audio_in_type = AUDIO_IN_PULSE;
    }
    fprintf(stderr, "Audio Out Device: %s\n", opts->audio_out_dev);
}

int
openAudioInDevice(dsd_opts* opts) {
    if (opts->audio_in_file) {
        sf_close(opts->audio_in_file);
        opts->audio_in_file = NULL;
    }
    if (opts->audio_in_file_info) {
        free(opts->audio_in_file_info);
        opts->audio_in_file_info = NULL;
    }
    if (opts->symbolfile) {
        fclose(opts->symbolfile);
        opts->symbolfile = NULL;
    }
    if (opts->tcp_in_ctx) {
        dsd_net_audio_input_hook_tcp_close(opts->tcp_in_ctx);
        opts->tcp_in_ctx = NULL;
    }
    if (opts->udp_in_ctx) {
        dsd_net_audio_input_hook_udp_stop(opts);
    }

    char* extension;
    const char ch = '.';
    extension = strrchr(opts->audio_in_dev, ch); //return extension if this is a .wav or .bin file

    //if no extension set, give default of .wav -- bugfix for github issue #105
    // if (extension == NULL) extension = ".wav";

    // get info of device/file
    if (strncmp(opts->audio_in_dev, "-", 1) == 0) {
        opts->audio_in_type = AUDIO_IN_STDIN;
        opts->audio_in_file_info = calloc(1, sizeof(SF_INFO));
        if (opts->audio_in_file_info == NULL) {
            LOG_ERROR("Error, couldn't allocate memory for audio input\n");
            return -1;
        }
        opts->audio_in_file_info->samplerate = opts->wav_sample_rate;
        opts->audio_in_file_info->channels = 1;
        opts->audio_in_file_info->seekable = 0;
        opts->audio_in_file_info->format = SF_FORMAT_RAW | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
        opts->audio_in_file = sf_open_fd(dsd_fileno(stdin), SFM_READ, opts->audio_in_file_info, 0);

        if (opts->audio_in_file == NULL) {
            LOG_ERROR("Error, couldn't open stdin with libsndfile: %s\n", sf_strerror(NULL));
            free(opts->audio_in_file_info);
            opts->audio_in_file_info = NULL;
            return -1;
        }
    }

    else if (strncmp(opts->audio_in_dev, "m17udp", 6) == 0) {
        opts->audio_in_type = AUDIO_IN_NULL; //NULL audio device
    }

    else if (strncmp(opts->audio_in_dev, "udp", 3) == 0) {
        // UDP direct audio input (PCM16LE)
        opts->audio_in_type = AUDIO_IN_UDP;
        // parse optional udp:addr:port string
        // default bind 127.0.0.1:7355 (matches TCP default)
        if (opts->udp_in_portno == 0) {
            opts->udp_in_portno = 7355;
        }
        if (opts->udp_in_bindaddr[0] == '\0') {
            snprintf(opts->udp_in_bindaddr, sizeof(opts->udp_in_bindaddr), "%s", "127.0.0.1");
        }
        // Start UDP input
        if (dsd_net_audio_input_hook_udp_start(opts, opts->udp_in_bindaddr, opts->udp_in_portno, opts->wav_sample_rate)
            < 0) {
            fprintf(stderr, "Error, couldn't start UDP input on %s:%d\n", opts->udp_in_bindaddr, opts->udp_in_portno);
            return -1;
        }
        fprintf(stderr, "Waiting for UDP audio on %s:%d ...\n", opts->udp_in_bindaddr, opts->udp_in_portno);
    }

    else if (strncmp(opts->audio_in_dev, "tcp", 3) == 0) {
        opts->audio_in_type = AUDIO_IN_TCP;
        opts->tcp_in_ctx = dsd_net_audio_input_hook_tcp_open(opts->tcp_sockfd, opts->wav_sample_rate);
        if (opts->tcp_in_ctx == NULL) {
            LOG_ERROR("Error, couldn't open TCP audio input\n");
            return -1;
        }
    }

    // else if (strncmp(opts->audio_in_dev, "udp", 3) == 0)
    // {
    //   opts->audio_in_type = AUDIO_IN_UDP;
    //   opts->audio_in_file_info = calloc(1, sizeof(SF_INFO));
    //   opts->audio_in_file_info->samplerate=opts->wav_sample_rate;
    //   opts->audio_in_file_info->channels=1;
    //   opts->audio_in_file_info->seekable=0;
    //   opts->audio_in_file_info->format=SF_FORMAT_RAW|SF_FORMAT_PCM_16|SF_ENDIAN_LITTLE;
    //   opts->udp_file_in = sf_open_fd(opts->udp_sockfd, SFM_READ, opts->audio_in_file_info, 0);

    //   if(opts->udp_file_in == NULL)
    //   {
    //     fprintf(stderr, "Error, couldn't open UDP with libsndfile: %s\n", sf_strerror(NULL));
    //     return;
    //   }
    // }

    else if (strncmp(opts->audio_in_dev, "rtl", 3) == 0) {
#ifdef USE_RTLSDR
        opts->audio_in_type = AUDIO_IN_RTL;
#else
        opts->audio_in_type = AUDIO_IN_PULSE;
        sprintf(opts->audio_in_dev, "pulse");
#endif
    } else if (strncmp(opts->audio_in_dev, "pulse", 5) == 0) {
        opts->audio_in_type = AUDIO_IN_PULSE;
    }

    //if no extension set, treat as named pipe or extensionless wav file -- bugfix for github issue #105
    else if (extension == NULL) {
        opts->audio_in_type = AUDIO_IN_WAV;
        opts->audio_in_file_info = calloc(1, sizeof(SF_INFO));
        if (opts->audio_in_file_info == NULL) {
            LOG_ERROR("Error, couldn't allocate memory for audio input\n");
            return -1;
        }
        opts->audio_in_file_info->samplerate = opts->wav_sample_rate;
        opts->audio_in_file_info->channels = 1;
        opts->audio_in_file_info->seekable = 0;
        opts->audio_in_file_info->format = SF_FORMAT_RAW | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
        opts->audio_in_file = sf_open(opts->audio_in_dev, SFM_READ, opts->audio_in_file_info);

        if (opts->audio_in_file == NULL) {
            LOG_ERROR("Error, couldn't open file/pipe with libsndfile: %s\n", sf_strerror(NULL));
            free(opts->audio_in_file_info);
            opts->audio_in_file_info = NULL;
            return -1;
        }
    }

    //test .rrc files with hardset wav file settings
    else if (strncmp(extension, ".rrc", 4) == 0) {
        //debug
        fprintf(stderr, "Opening M17 .rrc headless wav file\n");

        opts->audio_in_type = AUDIO_IN_WAV;
        opts->audio_in_file_info = calloc(1, sizeof(SF_INFO));
        if (opts->audio_in_file_info == NULL) {
            LOG_ERROR("Error, couldn't allocate memory for audio input\n");
            return -1;
        }
        opts->audio_in_file_info->samplerate = 48000;
        opts->audio_in_file_info->channels = 1;
        opts->audio_in_file_info->seekable = 0;
        opts->audio_in_file_info->format = SF_FORMAT_RAW | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
        opts->audio_in_file = sf_open(opts->audio_in_dev, SFM_READ, opts->audio_in_file_info);

        if (opts->audio_in_file == NULL) {
            LOG_ERROR("Error, couldn't open %s with libsndfile: %s\n", opts->audio_in_dev, sf_strerror(NULL));
            free(opts->audio_in_file_info);
            opts->audio_in_file_info = NULL;
            return -1;
        }
    }

    //Open .raw files as a float input type
    else if (strncmp(extension, ".raw", 4) == 0) {
        struct stat stat_buf;
        if (stat(opts->audio_in_dev, &stat_buf) != 0) {
            LOG_ERROR("Error, couldn't open raw (float) file %s\n", opts->audio_in_dev);
            return -1;
        }
        if (S_ISREG(stat_buf.st_mode)) {
            opts->symbolfile = fopen(opts->audio_in_dev, "r");
            if (opts->symbolfile == NULL) {
                LOG_ERROR("Error, couldn't open raw (float) file %s\n", opts->audio_in_dev);
                return -1;
            }
            opts->audio_in_type = AUDIO_IN_SYMBOL_FLT; //float symbol input
        } else {
            opts->audio_in_type = AUDIO_IN_PULSE;
        }
    }

    //Open .raw files as a float input type
    else if (strncmp(extension, ".sym", 4) == 0) {
        struct stat stat_buf;
        if (stat(opts->audio_in_dev, &stat_buf) != 0) {
            LOG_ERROR("Error, couldn't open sym (float) file %s\n", opts->audio_in_dev);
            return -1;
        }
        if (S_ISREG(stat_buf.st_mode)) {
            opts->symbolfile = fopen(opts->audio_in_dev, "r");
            if (opts->symbolfile == NULL) {
                LOG_ERROR("Error, couldn't open sym (float) file %s\n", opts->audio_in_dev);
                return -1;
            }
            opts->audio_in_type = AUDIO_IN_SYMBOL_FLT; //float symbol input
        } else {
            opts->audio_in_type = AUDIO_IN_PULSE;
        }
    }

    else if (strncmp(extension, ".bin", 4) == 0) {
        struct stat stat_buf;
        if (stat(opts->audio_in_dev, &stat_buf) != 0) {
            LOG_ERROR("Error, couldn't open bin file %s\n", opts->audio_in_dev);
            return -1;
        }
        if (S_ISREG(stat_buf.st_mode)) {
            opts->symbolfile = fopen(opts->audio_in_dev, "r");
            if (opts->symbolfile == NULL) {
                LOG_ERROR("Error, couldn't open bin file %s\n", opts->audio_in_dev);
                return -1;
            }
            opts->audio_in_type = AUDIO_IN_SYMBOL_BIN; //symbol capture bin files
        } else {
            opts->audio_in_type = AUDIO_IN_PULSE;
        }
    }
    //open as wav file as last resort, wav files subseptible to sample rate issues if not 48000
    else {
        struct stat stat_buf;
        if (stat(opts->audio_in_dev, &stat_buf) != 0) {
            LOG_ERROR("Error, couldn't open wav file %s\n", opts->audio_in_dev);
            return -1;
        }
        if (S_ISREG(stat_buf.st_mode)) {
            opts->audio_in_type = AUDIO_IN_WAV; //two now, seperating STDIN and wav files
            opts->audio_in_file_info = calloc(1, sizeof(SF_INFO));
            if (opts->audio_in_file_info == NULL) {
                LOG_ERROR("Error, couldn't allocate memory for audio input\n");
                return -1;
            }
            opts->audio_in_file_info->samplerate = opts->wav_sample_rate;
            opts->audio_in_file_info->channels = 1;
            opts->audio_in_file_info->channels = 1;
            opts->audio_in_file_info->seekable = 0;
            opts->audio_in_file_info->format = SF_FORMAT_RAW | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
            opts->audio_in_file = sf_open(opts->audio_in_dev, SFM_READ, opts->audio_in_file_info);

            if (opts->audio_in_file == NULL) {
                LOG_ERROR("Error, couldn't open wav file %s\n", opts->audio_in_dev);
                free(opts->audio_in_file_info);
                opts->audio_in_file_info = NULL;
                return -1;
            }

        }
        //open pulse audio if no bin or wav
        else //seems this condition is never met
        {
            LOG_ERROR("Error, couldn't open input file.\n");
            return -1;
        }
    }
    if (opts->split == 1) {
        fprintf(stderr, "Audio In Device: %s\n", opts->audio_in_dev);
    } else {
        fprintf(stderr, "Audio In/Out Device: %s\n", opts->audio_in_dev);
    }
    return 0;
}
