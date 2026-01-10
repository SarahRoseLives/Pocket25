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
#include <dsd-neo/core/audio_filters.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/power.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/dsp/dmr_sync.h>
#include <dsd-neo/dsp/sps_filters.h>
#include <dsd-neo/dsp/symbol_levels.h>
#ifdef USE_RTLSDR
#include <dsd-neo/runtime/rtl_stream_io_hooks.h>
#include <dsd-neo/runtime/rtl_stream_metrics_hooks.h>
#endif
#include <dsd-neo/platform/audio.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/platform/timing.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/exitflag.h>
#include <dsd-neo/runtime/log.h>
#include <dsd-neo/runtime/net_audio_input_hooks.h>
#include <dsd-neo/runtime/udp_audio_hooks.h>

#include <math.h>
#include <sndfile.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern dsd_socket_t Connect(char* hostname, int portno);
extern void cleanupAndExit(dsd_opts* opts, dsd_state* state);

static inline short
float_to_int16_clip(float v) {
    if (v > 32767.0f) {
        return 32767;
    }
    if (v < -32768.0f) {
        return -32768;
    }
    return (short)lrintf(v);
}

/*
 * Centralized window selection helpers per modulation. These encapsulate
 * left/right offsets used during symbol decision and allow a single point for
 * future tuning. When freeze_window is enabled (env/config), defaults are used
 * and any per-protocol dynamic tweaks are disabled for A/B comparisons.
 */
static inline void
select_window_c4fm(const dsd_state* state, int* l_edge, int* r_edge, int freeze_window) {
    int l = 2;
    int r = 2;
    if (!freeze_window) {
        if (DSD_SYNC_IS_YSF(state->synctype) || DSD_SYNC_IS_DMR_BS(state->lastsynctype)
            || state->lastsynctype == DSD_SYNC_DMR_MS_VOICE || state->lastsynctype == DSD_SYNC_DMR_MS_DATA) {
            l = 1; // YSF, DMR, some NXDN cases
        } else {
            l = 2; // P25 and NXDN96 prefer wider left window
        }
    }
    *l_edge = l;
    *r_edge = r;
}

static inline void
select_window_qpsk(int* l_edge, int* r_edge, int freeze_window) {
    (void)freeze_window; /* no dynamic tweaks for QPSK at present */
    *l_edge = 1;         // pick i == center-1
    *r_edge = 2;         // pick i == center+2
}

static inline void
select_window_gfsk(int* l_edge, int* r_edge, int freeze_window) {
    (void)freeze_window; /* no dynamic tweaks for GFSK at present */
    *l_edge = 1;         // pick i == center-1
    *r_edge = 1;         // pick i == center+1
}

#ifdef USE_RTLSDR
/* --- C4FM clock assist (EL / M&M) --- */
static inline int
slice_c4fm_level(int x, const dsd_state* s) {
    /* Map sample to nearest of {-3,-1,1,3} using center/min/max refs. */
    float c = s->center;
    float lo = (s->minref + c) / 2.0f;
    float hi = (s->maxref + c) / 2.0f;
    if ((float)x >= hi) {
        return 3;
    } else if ((float)x >= c) {
        return 1;
    } else if ((float)x >= lo) {
        return -1;
    } else {
        return -3;
    }
}

static inline void
maybe_c4fm_clock(dsd_opts* opts, dsd_state* state, int have_sync, int mode, int early, int mid, int late) {
    (void)opts;
    if (mode <= 0) {
        return;
    }
    /* Only on RTL pipeline; synced use is gated by runtime toggle to avoid
       perturbing steady-state decoders unless explicitly allowed. */
    if (opts->audio_in_type != AUDIO_IN_RTL) {
        return;
    }
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int allow_when_synced = (cfg && cfg->c4fm_clk_sync_is_set) ? (cfg->c4fm_clk_sync != 0) : 0;
    if (have_sync != 0 && !allow_when_synced) {
        return;
    }
    if (state->rf_mod != 0) {
        return; /* C4FM only */
    }

    /* Require valid neighborhood around center */
    if (state->symbolCenter < 1 || state->symbolCenter + 1 >= state->samplesPerSymbol) {
        return;
    }

    long long e = 0;
    if (mode == 1) { /* Early-Late using energy difference */
        long long er = (long long)early;
        long long lr = (long long)late;
        e = (lr * lr) - (er * er);
    } else if (mode == 2) { /* M&M using sliced decisions */
        int a_prev = state->c4fm_clk_prev_dec;
        int a_k;
        /* Prefer slicing on mid sample for stability */
        a_k = slice_c4fm_level(mid, state);
        if (a_prev == 0) {
            state->c4fm_clk_prev_dec = a_k;
            return; /* need one step of history */
        }
        /* Use data-aided early/late difference to gate direction on symbol polarity */
        long long diff = (long long)late - (long long)early;
        e = diff * (long long)a_k;
        state->c4fm_clk_prev_dec = a_k;
    } else {
        return;
    }

    /* Convert to sign and apply simple persistence before nudging center */
    int dir = 0;
    if (e > 0) {
        dir = +1; /* sample early → center → right */
    } else if (e < 0) {
        dir = -1; /* sample late → center → left */
    } else {
        state->c4fm_clk_run_dir = 0;
        state->c4fm_clk_run_len = 0;
        return;
    }

    if (state->c4fm_clk_cooldown > 0) {
        state->c4fm_clk_cooldown--;
        return;
    }

    if (dir == state->c4fm_clk_run_dir) {
        state->c4fm_clk_run_len++;
    } else {
        state->c4fm_clk_run_dir = dir;
        state->c4fm_clk_run_len = 1;
    }

    /* Nudge after brief persistence */
    if (state->c4fm_clk_run_len >= 4) {
        int c = state->symbolCenter + dir;
        int min_c = 1;
        int max_c = state->samplesPerSymbol - 2;
        if (c < min_c) {
            c = min_c;
        }
        if (c > max_c) {
            c = max_c;
        }
        state->symbolCenter = c;
        state->c4fm_clk_cooldown = 12; /* short cooldown */
        state->c4fm_clk_run_len = 0;
    }
}
#endif

#ifdef USE_RTLSDR
/*
 * Nudge symbolCenter by ±1 based on a smoothed TED residual when available.
 * Guards against oscillation using a small deadband and a cooldown period.
 *
 * Safety gates:
 *  - Only when using RTL input (audio_in_type == AUDIO_IN_RTL)
 *  - Only when not currently synchronized (have_sync == 0)
 *  - Only for C4FM path (rf_mod == 0) to avoid QPSK perturbations
 */
static inline void
maybe_auto_center(dsd_opts* opts, dsd_state* state, int have_sync) {
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int freeze_window = (cfg && cfg->window_freeze_is_set) ? (cfg->window_freeze != 0) : 0;
    if (freeze_window) {
        return; // explicit freeze requested
    }
    if (opts->audio_in_type != AUDIO_IN_RTL) {
        return; // only when using RTL stream/demod pipeline
    }
    /* If synced, only run when explicitly allowed by runtime config. */
    if (have_sync != 0) {
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        int allow_when_synced = (cfg && cfg->c4fm_clk_sync_is_set) ? (cfg->c4fm_clk_sync != 0) : 0;
        if (!allow_when_synced) {
            return;
        }
    }
    if (state->rf_mod != 0) {
        return; // limit to C4FM for now; avoid QPSK
    }

    /* Cooldown to avoid rapid flips */
    static int cooldown = 0;
    if (cooldown > 0) {
        cooldown--;
        return;
    }

    /* Read smoothed TED residual (can be 0 when TED disabled). */
    int e_ema = dsd_rtl_stream_metrics_hook_ted_bias();
    if (e_ema == 0) {
        return;
    }

    /* Small deadband and persistence guard */
    const int deadband = 5000; /* empirically small; residual uses coarse units */
    static int run_dir = 0;    /* -1, 0, +1 */
    static int run_len = 0;
    int dir = 0;
    if (e_ema > deadband) {
        dir = +1; /* sample was early; center → right */
    } else if (e_ema < -deadband) {
        dir = -1; /* sample was late; center → left */
    } else {
        run_dir = 0;
        run_len = 0;
        return;
    }

    if (dir == run_dir) {
        run_len++;
    } else {
        run_dir = dir;
        run_len = 1;
    }

    /* Require brief persistence before nudging center. */
    if (run_len >= 6) {
        int c = state->symbolCenter + dir;
        /* Keep a reasonable margin within [0..samplesPerSymbol-1] */
        int min_c = 1;
        int max_c = state->samplesPerSymbol - 2;
        if (c < min_c) {
            c = min_c;
        }
        if (c > max_c) {
            c = max_c;
        }
        state->symbolCenter = c;
        cooldown = 12; /* short cooldown after each nudge */
        run_len = 0;
    }
}
#endif

#ifdef USE_RTLSDR
/*
 * When using the RTL pipeline without resampling to 48 kHz, adjust
 * samplesPerSymbol and symbolCenter proportional to the current output rate
 * to preserve decoder timing windows. Runs cheaply with a rate-change guard.
 */
static inline void
maybe_adjust_sps_for_output_rate(dsd_opts* opts, dsd_state* state) {
    if (opts->audio_in_type != AUDIO_IN_RTL) {
        return; /* only for RTL input */
    }
    static unsigned int last_rate = 0;
    unsigned int Fs = 0;
    if (state->rtl_ctx) {
        Fs = dsd_rtl_stream_metrics_hook_output_rate_hz();
    }
    if (Fs == 0 || Fs == last_rate) {
        return;
    }
    last_rate = Fs;
    /* Refresh audio filters to match the new output rate. */
    init_audio_filters(state, (int)Fs);
    if (Fs == 48000) {
        return; /* canonical, keep existing SPS */
    }
    /* Scale SPS w.r.t. 48 kHz and preserve center ratio */
    int old_sps = state->samplesPerSymbol;
    if (old_sps <= 0) {
        old_sps = 10; /* safe default */
    }
    /* new_sps = round(old_sps * Fs / 48000) */
    long long num = (long long)old_sps * (long long)Fs;
    int new_sps = (int)((num + 24000) / 48000);
    if (new_sps < 2) {
        new_sps = 2;
    }
    if (new_sps == old_sps) {
        return;
    }
    /* Preserve existing center fraction within [1 .. new_sps-2] */
    double ratio = (double)state->symbolCenter / (double)old_sps;
    if (ratio < 0.05) {
        ratio = 0.05; /* avoid extreme left */
    }
    if (ratio > 0.95) {
        ratio = 0.95; /* avoid extreme right */
    }
    int new_center = (int)(ratio * (double)new_sps + 0.5);
    int min_c = 1;
    int max_c = new_sps - 2;
    if (new_center < min_c) {
        new_center = min_c;
    }
    if (new_center > max_c) {
        new_center = max_c;
    }
    state->samplesPerSymbol = new_sps;
    state->symbolCenter = new_center;
}
#endif

float
getSymbol(dsd_opts* opts, dsd_state* state, int have_sync) {
    float sample;
    int i, count;
    float symbol;
    float sum;
    sf_count_t result;
    const unsigned int analog_out_cap = (unsigned int)(sizeof(state->analog_out) / sizeof(state->analog_out[0]));

    sum = 0.0f;
    count = 0;
    sample = 0.0f; //init sample with a value of 0...see if this was causing issues with raw audio monitoring

#ifdef USE_RTLSDR
    /* C4FM clock assist capture around symbol center */
    int clk_mode = 0;
    int clk_early = 0, clk_mid = 0, clk_late = 0;
    if (state->rf_mod == 0) {
        const dsdneoRuntimeConfig* cfg_clk = dsd_neo_get_config();
        if (!cfg_clk) {
            dsd_neo_config_init(opts);
            cfg_clk = dsd_neo_get_config();
        }
        if (cfg_clk && cfg_clk->c4fm_clk_is_set) {
            clk_mode = cfg_clk->c4fm_clk_mode;
        }
    }
#endif

    /* Optional auto-centering based on TED residual (RTL path only, C4FM, when not synced) */
#ifdef USE_RTLSDR
    maybe_auto_center(opts, state, have_sync);
    /* Align SPS to current RTL output rate if not 48 kHz */
    maybe_adjust_sps_for_output_rate(opts, state);
#endif

    /* Resolve any window freeze override once per symbol to avoid inner-loop overhead */
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    int freeze_window = (cfg && cfg->window_freeze_is_set) ? (cfg->window_freeze != 0) : 0;

    /* Precompute left/right edges for current modulation once per symbol */
    int l_edge_pre = 0, r_edge_pre = 0;
    if (state->rf_mod == 0) {
        select_window_c4fm(state, &l_edge_pre, &r_edge_pre, freeze_window);
    } else if (state->rf_mod == 1) {
        select_window_qpsk(&l_edge_pre, &r_edge_pre, freeze_window);
    } else {
        select_window_gfsk(&l_edge_pre, &r_edge_pre, freeze_window);
    }

    /* Effective samples-per-symbol: when the RTL CQPSK path runs a decimating TED,
       the demodulated stream already arrives at symbol rate (1 sample/symbol). */
    int symbol_span = state->samplesPerSymbol;
    if (symbol_span < 1) {
        symbol_span = 1;
    }
#ifdef USE_RTLSDR
    int cqpsk_symbol_rate = 0;
    if (opts->audio_in_type == AUDIO_IN_RTL && state->rf_mod == 1) {
        int dsp_cqpsk = 0, dsp_fll = 0, dsp_ted = 0;
        dsd_rtl_stream_metrics_hook_dsp_get(&dsp_cqpsk, &dsp_fll, &dsp_ted);
        if (dsp_cqpsk && dsp_ted) {
            cqpsk_symbol_rate = 1;
            symbol_span = 1;
        }
    }
#else
    int cqpsk_symbol_rate = 0;
#endif
    if (symbol_span <= 1) {
        state->jitter = -1;
    }

    for (i = 0; i < symbol_span; i++) {

        // timing control
        if (symbol_span > 1 && (i == 0) && (have_sync == 0)) {
            if (state->samplesPerSymbol == 20) {
                if ((state->jitter >= 7) && (state->jitter <= 10)) {
                    i--;
                } else if ((state->jitter >= 11) && (state->jitter <= 14)) {
                    i++;
                }
            } else if (state->rf_mod == 1) {
                if ((state->jitter >= 0) && (state->jitter < state->symbolCenter)) {
                    i++; // fall back
                } else if ((state->jitter > state->symbolCenter) && (state->jitter < 10)) {
                    i--; // catch up
                }
            } else if (state->rf_mod == 2) {
                if ((state->jitter >= state->symbolCenter - 1) && (state->jitter <= state->symbolCenter)) {
                    i--;
                } else if ((state->jitter >= state->symbolCenter + 1) && (state->jitter <= state->symbolCenter + 2)) {
                    i++;
                }
            } else if (state->rf_mod == 0) {
                if ((state->jitter > 0) && (state->jitter <= state->symbolCenter)) {
                    i--; // catch up
                } else if ((state->jitter > state->symbolCenter) && (state->jitter < state->samplesPerSymbol)) {
                    i++; // fall back
                }
            }
            state->jitter = -1;
        }

        // Read the new sample from the input
        if (opts->audio_in_type == AUDIO_IN_PULSE) //audio stream input
        {
            short s = 0;
            if (opts->audio_in_stream) {
                dsd_audio_read(opts->audio_in_stream, &s, 1);
            }
            if (opts->input_volume_multiplier > 1) {
                int v = (int)s * opts->input_volume_multiplier;
                if (v > 32767) {
                    v = 32767;
                } else if (v < -32768) {
                    v = -32768;
                }
                s = (short)v;
            }
            sample = (float)s;
        }

        //stdin only, wav files moving to new number
        else if (opts->audio_in_type == AUDIO_IN_STDIN) //won't work in windows, needs posix pipe (mintty)
        {
            short s = 0;
            result = sf_read_short(opts->audio_in_file, &s, 1);
            if (opts->input_volume_multiplier > 1) {
                int v = (int)s * opts->input_volume_multiplier;
                if (v > 32767) {
                    v = 32767;
                } else if (v < -32768) {
                    v = -32768;
                }
                s = (short)v;
            }
            sample = (float)s;
            if (result == 0) {
                sf_close(opts->audio_in_file);
                cleanupAndExit(opts, state);
                return 0.0f;
            }
        }
        //wav files, same but using seperate value so we can still manipulate ncurses menu
        //since we can not worry about getch/stdin conflict
        else if (opts->audio_in_type == AUDIO_IN_WAV) {
            short s = 0;
            result = sf_read_short(opts->audio_in_file, &s, 1);
            if (opts->input_volume_multiplier > 1) {
                int v = (int)s * opts->input_volume_multiplier;
                if (v > 32767) {
                    v = 32767;
                } else if (v < -32768) {
                    v = -32768;
                }
                s = (short)v;
            }
            sample = (float)s;
            if (result == 0) {

                sf_close(opts->audio_in_file);
                fprintf(stderr, "\nEnd of %s\n", opts->audio_in_dev);
                //open pulse input if we are pulse output AND using ncurses terminal
                if (opts->audio_out_type == 0 && opts->use_ncurses_terminal == 1) {
                    opts->audio_in_type = AUDIO_IN_PULSE; //set input type
                    if (openAudioInput(opts) != 0) {
                        cleanupAndExit(opts, state);
                        return 0.0f;
                    }
                }
                //else cleanup and exit
                else {
                    cleanupAndExit(opts, state);
                    return 0.0f;
                }
            }
        } else if (opts->audio_in_type == AUDIO_IN_RTL) {
#ifdef USE_RTLSDR
            // Read demodulated stream here
            if (!state->rtl_ctx) {
                cleanupAndExit(opts, state);
                return 0.0f;
            }
            int got = 0;
            if (dsd_rtl_stream_io_hook_read(state, &sample, 1, &got) < 0 || got != 1) {
                cleanupAndExit(opts, state);
                return 0.0f;
            }
            //update root means square power level
            opts->rtl_pwr = dsd_rtl_stream_io_hook_return_pwr(state);
            /* Skip volume multiplier for CQPSK symbols - they're already properly
             * scaled by qpsk_differential_demod (phase * 4/π giving ±1, ±3 symbols).
             * The volume multiplier is meant for FM audio amplitude, not symbol levels. */
            if (!cqpsk_symbol_rate) {
                sample *= opts->rtl_volume_multiplier;
            }
#endif
        }

        //tcp socket input from SDR++ -- now with 1 retry if connection is broken
        else if (opts->audio_in_type == AUDIO_IN_TCP) {
            short s = 0;
            int tcp_result = dsd_net_audio_input_hook_tcp_read_sample(opts->tcp_in_ctx, (int16_t*)&s);
            if (opts->input_volume_multiplier > 1) {
                int v = (int)s * opts->input_volume_multiplier;
                if (v > 32767) {
                    v = 32767;
                } else if (v < -32768) {
                    v = -32768;
                }
                s = (short)v;
            }
            sample = (float)s;
            if (tcp_result == 0) {
            TCP_RETRY:
                if (exitflag == 1) {
                    cleanupAndExit(opts, state); //needed to break the loop on ctrl+c
                    return 0.0f;
                }
                // shorter backoff on TCP input stall to avoid wedging decode/SM
                int backoff_ms = 300; // default 300ms
                const dsdneoRuntimeConfig* cfg_retry = dsd_neo_get_config();
                if (!cfg_retry) {
                    dsd_neo_config_init(opts);
                    cfg_retry = dsd_neo_get_config();
                }
                if (cfg_retry && cfg_retry->tcpin_backoff_ms_is_set) {
                    backoff_ms = cfg_retry->tcpin_backoff_ms;
                }
                fprintf(stderr, "\nConnection to TCP Server Interrupted. Trying again in %d ms.\n", backoff_ms);
                sample = 0;
                dsd_net_audio_input_hook_tcp_close(opts->tcp_in_ctx); //close current connection on this end
                opts->tcp_in_ctx = NULL;
                dsd_socket_close(opts->tcp_sockfd);
                // short throttle to avoid busy loop, but keep UI/SM responsive
                dsd_sleep_ms((unsigned int)backoff_ms);

                //attempt to reconnect to socket
                opts->tcp_sockfd = 0;
                opts->tcp_sockfd = Connect(opts->tcp_hostname, opts->tcp_portno);
                if (opts->tcp_sockfd != 0) {
                    //reset audio input stream
                    opts->tcp_in_ctx = dsd_net_audio_input_hook_tcp_open(opts->tcp_sockfd, opts->wav_sample_rate);
                    if (opts->tcp_in_ctx == NULL) {
                        fprintf(stderr, "Error, couldn't Reconnect to TCP audio input\n");
                    } else {
                        LOG_INFO("TCP Socket Reconnected Successfully.\n");
                    }
                } else {
                    LOG_ERROR("TCP Socket Connection Error.\n");
                    // If using M17 or other keepalive mode, keep trying quickly
                    if (opts->frame_m17 == 1) {
                        goto TCP_RETRY;
                    }
                }

                //now retry reading sample
                short s_retry = 0;
                tcp_result = dsd_net_audio_input_hook_tcp_read_sample(opts->tcp_in_ctx, (int16_t*)&s_retry);
                sample = (float)s_retry;
                if (tcp_result == 0) {
                    dsd_net_audio_input_hook_tcp_close(opts->tcp_in_ctx);
                    opts->tcp_in_ctx = NULL;
                    dsd_socket_close(opts->tcp_sockfd);
                    opts->audio_in_type = AUDIO_IN_PULSE; //set input type
                    opts->tcp_sockfd =
                        0; //added this line so we will know if it connected when using ncurses terminal keyboard shortcut
                    if (openAudioInput(opts) != 0) {
                        cleanupAndExit(opts, state);
                        return 0.0f;
                    }
                    sample = 0; //zero sample on bad result, keep the ball rolling
                    fprintf(stderr, "Connection to TCP Server Disconnected.\n");
                }
            }
        }

        // UDP direct audio input (PCM16LE over UDP)
        else if (opts->audio_in_type == AUDIO_IN_UDP) {
            short s = 0;
            if (!dsd_net_audio_input_hook_udp_read_sample(opts, (int16_t*)&s)) {
                cleanupAndExit(opts, state);
                return 0.0f;
            }
            sample = (float)s;
            if (opts->input_volume_multiplier > 1) {
                int v = (int)s * opts->input_volume_multiplier;
                if (v > 32767) {
                    v = 32767;
                } else if (v < -32768) {
                    v = -32768;
                }
                sample = (float)v;
            }
        }

        //BUG REPORT: 1. DMR Simplex doesn't work with raw wav files. 2. Using the monitor w/ wav file saving may produce undecodable wav files.
        //reworked a bit to allow raw audio wav file saving without the monitoring poriton active
        if (have_sync == 0) {

            //do an extra checkfor carrier signal so that random raw audio spurts don't play during decoding
            // if ( (state->carrier == 1) && ((time(NULL) - state->last_vc_sync_time) < 2)) /This probably doesn't work correctly since we update time check when playing raw audio
            // {
            //   memset (state->analog_out, 0, sizeof(state->analog_out));
            //   state->analog_sample_counter = 0;
            // } //This is the root cause of issue listed above, will evaluate further at a later time for a more elegant solution, or determine if anything is negatively impacted by removing this

            /* Collect ~20 ms of audio based on current output Fs (defaults to 48 kHz; ~960 samples). */
            unsigned int analog_block = analog_out_cap;
            if (opts->audio_in_type == AUDIO_IN_RTL) {
#ifdef USE_RTLSDR
                unsigned int Fs = 0;
                if (state->rtl_ctx) {
                    Fs = dsd_rtl_stream_metrics_hook_output_rate_hz();
                }
                if (Fs > 0) {
                    analog_block = (unsigned int)(((uint64_t)Fs * 20 + 999) / 1000); /* ~20 ms */
                    if (analog_block < 320) {
                        analog_block = 320; /* floor to ~6.7 ms */
                    } else if (analog_block > 4000) {
                        analog_block = 4000; /* cap to ~83 ms */
                    }
                }
#endif
            }
            if (analog_block > analog_out_cap) {
                analog_block = analog_out_cap; /* never exceed buffer capacity */
            }

            //sanity check to prevent an overflow
            if ((unsigned int)state->analog_sample_counter >= analog_block) {
                state->analog_sample_counter = (int)analog_block - 1;
            }

            // Store float sample (native precision path)
            state->analog_out_f[state->analog_sample_counter++] = sample;

            if ((unsigned int)state->analog_sample_counter == analog_block) {
                //measure input power for non-RTL inputs (use float path)
                if (opts->audio_in_type != AUDIO_IN_RTL) {
                    opts->rtl_pwr = raw_pwr_f(state->analog_out_f, (int)analog_block, 1);
                    // Optional: warn on persistently low input level
                    if (opts->input_warn_db < 0.0) {
                        double db = pwr_to_dB(opts->rtl_pwr);
                        time_t now = time(NULL);
                        if (db <= opts->input_warn_db
                            && (opts->last_input_warn_time == 0
                                || (int)(now - opts->last_input_warn_time) >= opts->input_warn_cooldown_sec)) {
                            LOG_WARNING(
                                "Input level low (%.1f dBFS). Consider raising sender gain or use --input-volume.\n",
                                db);
                            opts->last_input_warn_time = now;
                        }
                    }
                }

                //raw wav file saving -- only write when not NXDN, dPMR, or M17 due to noise that can cause tons of false positives when no sync
                // Convert to int16 for WAV file (before filtering for raw capture)
                if (opts->wav_out_raw != NULL && opts->frame_nxdn48 == 0 && opts->frame_nxdn96 == 0
                    && opts->frame_dpmr == 0 && opts->frame_m17 == 0) {
                    for (unsigned int i = 0; i < analog_block; i++) {
                        state->analog_out[i] = float_to_int16_clip(state->analog_out_f[i]);
                    }
                    sf_write_short(opts->wav_out_raw, state->analog_out, analog_block);
                    sf_write_sync(opts->wav_out_raw);
                }

                //low pass filter (native float path)
                if (opts->use_lpf == 1) {
                    lpf_f(state, state->analog_out_f, (int)analog_block);
                }

                //high pass filter (native float path)
                if (opts->use_hpf == 1) {
                    hpf_f(state, state->analog_out_f, (int)analog_block);
                }

                //pass band filter (native float path)
                if (opts->use_pbf == 1) {
                    pbf_f(state, state->analog_out_f, (int)analog_block);
                }

                //manual gain control (native float path)
                if (opts->audio_gainA > 0.0f) {
                    analog_gain_f(opts, state, state->analog_out_f, (int)analog_block);
                }

                //automatic gain control (native float path)
                else {
                    agsm_f(opts, state, state->analog_out_f, (int)analog_block);
                }

                //Running PWR after filtering does remove the analog spike from the PWR value
                //but noise floor noise will still produce higher values
                // if (opts->audio_in_type != AUDIO_IN_RTL  && opts->monitor_input_audio == 1)
                //   opts->rtl_pwr = raw_pwr_f(state->analog_out_f, 960, 1);

                //seems to be working now, but PWR values are lower on actual analog signal than on no signal but noise
                if ((opts->rtl_pwr > opts->rtl_squelch_level) && opts->monitor_input_audio == 1 && state->carrier == 0
                    && opts->audio_out == 1) { //added carrier check here in lieu of disabling it above
                    // Convert float to int16 for output (final conversion at output stage)
                    for (unsigned int i = 0; i < analog_block; i++) {
                        state->analog_out[i] = float_to_int16_clip(state->analog_out_f[i]);
                    }
                    size_t bytes = (size_t)analog_block * sizeof(short);
                    if (opts->audio_out_type == 0) {
                        if (opts->audio_raw_out) {
                            dsd_audio_write(opts->audio_raw_out, state->analog_out, analog_block);
                        }
                    }

                    if (opts->audio_out_type == 8) {
                        dsd_udp_audio_hook_blast_analog(opts, state, bytes, state->analog_out);
                    }

                    // UI/scan heartbeat: avoid refreshing timers that the
                    // trunk SM depends on for hangtime and CC hunting logic.
                    // - Do NOT refresh last_cc_sync_time while trunking is
                    //   enabled and we are not voice‑tuned; the SM needs that
                    //   timer to age so CC hunting can start.
                    // - Do NOT refresh last_vc_sync_time while voice‑tuned; it
                    //   must reflect actual digital voice activity only.
                    if (opts->p25_trunk != 1) {
                        state->last_cc_sync_time = time(NULL);
                        state->last_cc_sync_time_m = dsd_time_now_monotonic_s();
                    }
                    if (!(opts->p25_trunk == 1 && opts->p25_is_tuned == 1)) {
                        state->last_vc_sync_time = time(NULL);
                        state->last_vc_sync_time_m = dsd_time_now_monotonic_s();
                    }
                }

                //raw wav file saving -- save the WAV file samples before we apply filtering to them
                // if (opts->wav_out_raw != NULL && opts->frame_nxdn48 == 0 && opts->frame_nxdn96 == 0 && opts->frame_dpmr == 0 && opts->frame_m17 == 0)
                // {
                //   sf_write_short(opts->wav_out_raw, state->analog_out, 960);
                //   sf_write_sync (opts->wav_out_raw);
                // }

                memset(state->analog_out_f, 0, sizeof(state->analog_out_f));
                memset(state->analog_out, 0, sizeof(state->analog_out));
                state->analog_sample_counter = 0;
            }
        }

        if (have_sync == 1) {
            //sanity check to prevent an overflow
            int analog_max_index = (int)analog_out_cap - 1;
            if (state->analog_sample_counter > analog_max_index) {
                state->analog_sample_counter = analog_max_index;
            }

            // Store float sample (native precision path)
            state->analog_out_f[state->analog_sample_counter++] = sample;

            if ((unsigned int)state->analog_sample_counter == analog_out_cap) {
                //raw wav file saving -- file size on this blimps pretty fast 1 min ~= 6 MB;  1 hour ~= 360 MB;
                if (opts->wav_out_raw != NULL) {
                    // Convert float to int16 for WAV file output
                    for (unsigned int i = 0; i < analog_out_cap; i++) {
                        state->analog_out[i] = float_to_int16_clip(state->analog_out_f[i]);
                    }
                    sf_write_short(opts->wav_out_raw, state->analog_out, analog_out_cap);
                    sf_write_sync(opts->wav_out_raw);
                }

                //zero out and reset counter
                memset(state->analog_out_f, 0, sizeof(state->analog_out_f));
                memset(state->analog_out, 0, sizeof(state->analog_out));
                state->analog_sample_counter = 0;
            }
        }

        /* Skip legacy scalar matched filters when consuming symbol-rate CQPSK stream
       from the RTL DSP path. The CQPSK pipeline already applied channel filtering
       and timing recovery in complex baseband; additional FIRs here distort the
       {-3,-1,+1,+3} levels and break the slicer. */
        if (opts->use_cosine_filter && !cqpsk_symbol_rate) {
            if (DSD_SYNC_IS_DMR_BS(state->lastsynctype) || DSD_SYNC_IS_DMR_MS(state->lastsynctype)
                || DSD_SYNC_IS_YSF(state->lastsynctype)) {
                sample = dmr_filter(sample, state->samplesPerSymbol);
            }

            else if (state->lastsynctype == DSD_SYNC_M17_STR_POS || state->lastsynctype == DSD_SYNC_M17_STR_NEG
                     || state->lastsynctype == DSD_SYNC_M17_LSF_POS || state->lastsynctype == DSD_SYNC_M17_LSF_NEG
                     || state->lastsynctype == DSD_SYNC_M17_PKT_POS || state->lastsynctype == DSD_SYNC_M17_PKT_NEG
                     || state->lastsynctype == DSD_SYNC_M17_PRE_POS || state->lastsynctype == DSD_SYNC_M17_PRE_NEG) {
                sample = m17_filter(sample, state->samplesPerSymbol);
            }

            // Apply matched filter to P25 Phase 1 (C4FM)
            else if (DSD_SYNC_IS_P25P1(state->lastsynctype)) {
                // OP25-compatible sinc de-emphasis filter
                sample = p25_filter(sample, state->samplesPerSymbol);
            }

            else if (DSD_SYNC_IS_DPMR(state->lastsynctype) || DSD_SYNC_IS_NXDN(state->lastsynctype)) {
                //if(state->samplesPerSymbol == 20)
                if (opts->frame_nxdn48 == 1) {
                    sample = nxdn_filter(sample, state->samplesPerSymbol);
                } else if (opts->frame_dpmr == 1) {
                    sample = dpmr_filter(sample, state->samplesPerSymbol);
                } else if (state->samplesPerSymbol == 8) //phase 2 cqpsk
                {
                    //sample = dmr_filter(sample); //work on filter later
                } else {
                    sample = dmr_filter(sample, state->samplesPerSymbol);
                }
            }
        }

        if ((sample > state->max) && (have_sync == 1) && (state->rf_mod == 0)) {
            sample = state->max;
        } else if ((sample < state->min) && (have_sync == 1) && (state->rf_mod == 0)) {
            sample = state->min;
        }

        if (sample > state->center) {
            if (sample > (state->maxref * 1.25)) {
                if ((state->jitter < 0) && (state->rf_mod == 1)) { // first spike out of place
                    state->jitter = i;
                }
                if ((opts->symboltiming == 1) && (have_sync == 0) && (state->lastsynctype != DSD_SYNC_NONE)) {
                    fprintf(stderr, "O");
                }
            } else {
                if ((opts->symboltiming == 1) && (have_sync == 0) && (state->lastsynctype != DSD_SYNC_NONE)) {
                    fprintf(stderr, "+");
                }
                if ((state->jitter < 0) && (state->lastsample < state->center)
                    && (state->rf_mod != 1)) { // first transition edge
                    state->jitter = i;
                }
            }
        } else { // sample < 0
            if (sample < (state->minref * 1.25)) {
                if ((state->jitter < 0) && (state->rf_mod == 1)) { // first spike out of place
                    state->jitter = i;
                }
                if ((opts->symboltiming == 1) && (have_sync == 0) && (state->lastsynctype != DSD_SYNC_NONE)) {
                    fprintf(stderr, "X");
                }
            } else {
                if ((opts->symboltiming == 1) && (have_sync == 0) && (state->lastsynctype != DSD_SYNC_NONE)) {
                    fprintf(stderr, "-");
                }
                if ((state->jitter < 0) && (state->lastsample > state->center)
                    && (state->rf_mod != 1)) { // first transition edge
                    state->jitter = i;
                }
            }
        }

        if (state->samplesPerSymbol == 20) //nxdn 4800 baud 2400 symbol rate
        {
            // if ((i >= 9) && (i <= 11))
            if ((i >= 7) && (i <= 13)) //7, 13 working good on multiple nxdn48, fewer random errors
            {
                sum += sample;
                count++;
            }
        }
        if (cqpsk_symbol_rate) {
            /* TED already decimated to symbol rate: consume one sample per symbol.
             * This must be checked first to override modulation-specific sampling
             * (e.g., sps==5 for P25 CQPSK at 24kHz). */
            sum += sample;
            count++;
        } else if (state->samplesPerSymbol == 5) {
            // provoice or gfsk at sps=5 (non-TED path)
            if (i == 2) {
                sum += sample;
                count++;
            }
        } else {
            if (state->rf_mod == 0) {
                // 0: C4FM modulation — average a small window around the symbol center
                // (matches dsd-fme behavior and is more tolerant to timing jitter on marginal signals).
                if ((i >= state->symbolCenter - l_edge_pre) && (i <= state->symbolCenter + r_edge_pre)) {
                    sum += sample;
                    count++;
                }

#ifdef TRACE_DSD
                if (i == state->symbolCenter - 1) {
                    state->debug_sample_left_edge = state->debug_sample_index - 1;
                }
                if (i == state->symbolCenter + 2) {
                    state->debug_sample_right_edge = state->debug_sample_index - 1;
                }
#endif
            } else { // QPSK or GFSK
                /* For very low SPS (e.g., when RTL DSP baseband is configured < 24 kHz or resampling is disabled),
                   the edge-pair window becomes too sparse/noisy for GFSK. Prefer the center sample to avoid
                   systematic symbol slicing bias that can corrupt data PDUs (e.g., LRRP). */
                if (state->rf_mod == 2 && state->samplesPerSymbol <= 4) {
                    if (i == state->symbolCenter) {
                        sum += sample;
                        count++;
                    }
                } else {
                    // Use symmetric two-sample window (precomputed)
                    if ((i == state->symbolCenter - l_edge_pre) || (i == state->symbolCenter + r_edge_pre)) {
                        sum += sample;
                        count++;
                    }
                }
            }
        }

        state->lastsample = sample;

#ifdef USE_RTLSDR
        if (clk_mode && state->rf_mod == 0) {
            int c = state->symbolCenter;
            if (i == c - 1) {
                clk_early = (int)lrintf(sample);
            } else if (i == c) {
                clk_mid = (int)lrintf(sample);
            } else if (i == c + 1) {
                clk_late = (int)lrintf(sample);
            }
        }
#endif
    }

    if (count > 0) {
        symbol = sum / (float)count;
    } else {
        symbol = 0.0f;
    }

    if ((opts->symboltiming == 1) && (have_sync == 0) && (state->lastsynctype != DSD_SYNC_NONE)) {
        if (state->jitter >= 0) {
            fprintf(stderr, " %i\n", state->jitter);
        } else {
            fprintf(stderr, "\n");
        }
    }

#ifdef TRACE_DSD
    if (state->samplesPerSymbol == 10) {
        float left, right;
        if (state->debug_label_file == NULL) {
            state->debug_label_file = fopen("pp_label.txt", "w");
        }
        left = state->debug_sample_left_edge / SAMPLE_RATE_IN;
        right = state->debug_sample_right_edge / SAMPLE_RATE_IN;
        if (state->debug_prefix != '\0') {
            if (state->debug_prefix == 'I') {
                fprintf(state->debug_label_file, "%f\t%f\t%c%c %.3f\n", left, right, state->debug_prefix,
                        state->debug_prefix_2, symbol);
            } else {
                fprintf(state->debug_label_file, "%f\t%f\t%c %.3f\n", left, right, state->debug_prefix, symbol);
            }
        } else {
            fprintf(state->debug_label_file, "%f\t%f\t%.3f\n", left, right, symbol);
        }
    }
#endif

    //read dibit capture bin files
    if (opts->audio_in_type == AUDIO_IN_SYMBOL_BIN) {
        //use fopen and read in a symbol, check op25 for clues
        if (opts->symbolfile == NULL) {
            fprintf(stderr, "Error Opening File %s\n", opts->audio_in_dev); //double check this
            return -1.0f;
        }

        state->symbolc = fgetc(opts->symbolfile);

        //fprintf(stderr, "%d", state->symbolc);
        if (feof(opts->symbolfile)) {
            // opts->audio_in_type = AUDIO_IN_PULSE; //switch to pulse after playback, ncurses terminal can initiate replay if wanted
            fclose(opts->symbolfile);
            fprintf(stderr, "\nEnd of %s\n", opts->audio_in_dev);
            //in debug mode, re-run .bin files over and over (look for memory leaks, etc)
            if (state->debug_mode == 1) {
                opts->symbolfile = NULL;
                opts->symbolfile = fopen(opts->audio_in_dev, "r");
                opts->audio_in_type = AUDIO_IN_SYMBOL_BIN; //symbol capture bin files
            }
            //open pulse input if we are pulse output AND using ncurses terminal
            else if (opts->audio_out_type == 0 && opts->use_ncurses_terminal == 1) {
                opts->audio_in_type = AUDIO_IN_PULSE; //set input type
                if (openAudioInput(opts) != 0) {
                    cleanupAndExit(opts, state);
                    return 0.0f;
                }
            }
            //else cleanup and exit
            else {
                cleanupAndExit(opts, state);
                return 0.0f;
            }
        }

        // Map capture dibit values (0..3) back to nominal symbol levels.
        // The .bin capture format stores dibits, not raw discriminator samples, so this mapping must
        // be stable regardless of the current demod mode (rf_mod).
        symbol = dsd_symbol_level_from_dibit((uint8_t)state->symbolc);
    }

    //.raw or .sym float symbol files
    if (opts->audio_in_type == AUDIO_IN_SYMBOL_FLT) {
        float float_symbol = 0.0f;
        size_t read_count = fread(&float_symbol, sizeof(float), 1, opts->symbolfile); //sizeof(float) is 4 (usually)
        if (read_count != 1) {
            exitflag = 1; // EOF or read error, exit loop cleanly
            symbol = 0.0f;
            return symbol;
        }
        if (feof(opts->symbolfile)) {
            exitflag = 1; //end of file, exit
        }
        // float_symbol = -float_symbol; //inversion
        symbol = float_symbol * 10000.0f;
    }

    /* Apply C4FM clock assist after symbol decision (unsynced only) */
#ifdef USE_RTLSDR
    if (clk_mode && state->rf_mod == 0) {
        maybe_c4fm_clock(opts, state, have_sync, clk_mode, clk_early, clk_mid, clk_late);
    }
#endif

    /* Store symbol in history for resample-on-sync support.
     * This enables threshold calibration and CACH re-digitization when
     * DMR sync is detected. Uses the dmr_sync module's history buffer API. */
    dmr_sample_history_push(state, symbol);

    state->symbolcnt++;
    return (symbol);
}
