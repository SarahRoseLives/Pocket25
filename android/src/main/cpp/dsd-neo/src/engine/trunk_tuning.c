// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/engine/trunk_tuning.h>

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/dsp/frame_sync.h>
#include <dsd-neo/io/rigctl_client.h>
#include <dsd-neo/io/rtl_stream_c.h>
#include <dsd-neo/protocol/dmr/dmr_block.h>
#include <dsd-neo/protocol/p25/p25_sm_watchdog.h>
#include <dsd-neo/protocol/p25/p25p2_frame.h>
#include <dsd-neo/runtime/config.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/**
 * @brief Return to the P25 control channel, draining audio and resetting state.
 *
 * Handles both rigctl- and RTL-backed pipelines and refreshes internal timers,
 * last-tuned frequencies, and channel tracking metadata.
 *
 * @param opts Decoder options (tuning targets and sockets).
 * @param state Decoder state to reset.
 */
void
dsd_engine_return_to_cc(dsd_opts* opts, dsd_state* state) {
    // Audio drain is handled by dsd_engine_trunk_tune_to_cc() before the hardware retune.

    // Extra safeguards due to sync issues with NXDN
    memset(state->nxdn_sacch_frame_segment, 1, sizeof(state->nxdn_sacch_frame_segment));
    memset(state->nxdn_sacch_frame_segcrc, 1, sizeof(state->nxdn_sacch_frame_segcrc));

    memset(state->active_channel, 0, sizeof(state->active_channel));

    //reset dmr blocks
    dmr_reset_blocks(opts, state);

    //zero out additional items
    state->lasttg = 0;
    state->lasttgR = 0;
    state->lastsrc = 0;
    state->lastsrcR = 0;
    state->gi[0] = -1;
    state->gi[1] = -1;
    state->payload_algid = 0;
    state->payload_algidR = 0;
    state->payload_keyid = 0;
    state->payload_keyidR = 0;
    state->payload_mi = 0;
    state->payload_miR = 0;
    state->payload_miP = 0;
    state->payload_miN = 0;
    opts->p25_is_tuned = 0;
    opts->trunk_is_tuned = 0;
    state->p25_vc_freq[0] = state->p25_vc_freq[1] = 0;
    state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = 0;
    // Reset voice activity timer to avoid carrying over a stale recent-voice
    // window into the next VC follow and to help the SM's idle fallback.
    state->last_vc_sync_time = 0;
    state->last_vc_sync_time_m = 0.0;
    // Clear P25p2 per-slot audio gating
    state->p25_p2_audio_allowed[0] = 0;
    state->p25_p2_audio_allowed[1] = 0;
    // Clear P25p2 per-slot Packet/Data flags
    state->p25_call_is_packet[0] = 0;
    state->p25_call_is_packet[1] = 0;
    state->p25_p2_active_slot = -1;
    // Do not alter user slot On/Off toggles here; UI controls own persistence.

    // Tune back to the control channel when known (best-effort). Prefer the
    // explicit CC when available; otherwise fall back to any tracked CC.
    // Avoid sending a zero/unknown frequency to the tuner which can wedge the
    // pipeline at DC and delay CC hunting.
    long int cc = (state->p25_cc_freq != 0) ? state->p25_cc_freq : state->trunk_cc_freq;
    if (opts->p25_trunk == 1 && cc != 0) {
        // Compute CC TED SPS dynamically based on actual demodulator output rate.
        // P25P1 CC = 4800 sym/s, P25P2 TDMA CC = 6000 sym/s.
        int sym_rate = (state->p25_cc_is_tdma == 1) ? 6000 : 4800;
        int demod_rate = 0;
#ifdef USE_RTLSDR
        if (state->rtl_ctx) {
            demod_rate = (int)rtl_stream_output_rate(state->rtl_ctx);
        }
#endif
        int cc_sps = dsd_opts_compute_sps_rate(opts, sym_rate, demod_rate);
        dsd_engine_trunk_tune_to_cc(opts, state, cc, cc_sps);
    }

    // Set symbol timing for CC based on CC type and actual demodulator rate.
    // samplesPerSymbol is used by the legacy symbol slicer code.
    int demod_rate = 0;
#ifdef USE_RTLSDR
    if (state->rtl_ctx) {
        demod_rate = (int)rtl_stream_output_rate(state->rtl_ctx);
    }
#endif
    if (state->p25_cc_is_tdma == 0) {
        // P25P1 CC: 4800 sym/s
        state->samplesPerSymbol = dsd_opts_compute_sps_rate(opts, 4800, demod_rate);
        state->symbolCenter = dsd_opts_symbol_center(state->samplesPerSymbol);
    } else {
        // P25P2 TDMA CC: 6000 sym/s
        state->samplesPerSymbol = dsd_opts_compute_sps_rate(opts, 6000, demod_rate);
        state->symbolCenter = dsd_opts_symbol_center(state->samplesPerSymbol);
    }
}

/**
 * @brief Tune to a specific voice/control channel and mark the trunking state.
 *
 * Drains queued audio, tunes via rigctl or RTL stream, and updates timers and
 * bookkeeping fields so the SM can track the new voice channel.
 *
 * @param opts Decoder options with tuning configuration.
 * @param state Decoder state to update.
 * @param freq Target frequency in Hz.
 * @param ted_sps TED samples-per-symbol to set (0 = no override).
 */
void
dsd_engine_trunk_tune_to_freq(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    if (!opts || !state || freq <= 0) {
        return;
    }

    // Reset modulation auto-detect state (ham tracking, vote counters) to ensure
    // fresh acquisition on the new channel and avoid carrying stale decisions.
    dsd_frame_sync_reset_mod_state();

    // Reset P25P2 frame processing state when tuning to a voice channel.
    // This is critical: without resetting the global bit buffers (p2bit, p2xbit),
    // ESS buffers (ess_a, ess_b), and counters (vc_counter, ts_counter), the
    // decoder will process new channel data using stale buffers from the previous
    // channel, causing decode failures. The symptom is: first P25P2 tune works,
    // but subsequent voice channel grants fail to lock with tanking EVM/SNR.
    // Only reset for P25P2 (ted_sps matching the TDMA symbol rate), not P25P1 or other modes.
    int p25p2_demod_rate = 0;
#ifdef USE_RTLSDR
    if (state->rtl_ctx) {
        p25p2_demod_rate = (int)rtl_stream_output_rate(state->rtl_ctx);
    }
#endif
    int p25p2_sps = dsd_opts_compute_sps_rate(opts, 6000, p25p2_demod_rate);
    if (ted_sps == p25p2_sps) {
        p25_p2_frame_reset();
    }

    // NOTE: We intentionally do NOT call rtl_stream_reset_costas() here.
    //
    // The Costas/TED state reset must happen AFTER the hardware retune completes,
    // which is handled by demod_reset_on_retune() in the controller thread.
    //
    // If we reset here (before the hardware retune), the DSP thread will:
    //   1. See the reset Costas state (phase=0, freq=0)
    //   2. Continue processing samples from the OLD frequency
    //   3. Try to lock on the wrong signal, corrupting the loop state
    //   4. When the retune completes, the loop is in a bad state
    //
    // By deferring the reset to the controller thread, the DSP continues with
    // the current (correct for current signal) state until the hardware retune
    // completes, then resets and acquires on the new signal.
    //
    // This matches OP25's architecture where configure_tdma/set_omega are called
    // synchronously with the frequency change, not asynchronously before it.

    // Set TED samples-per-symbol override if provided by caller.
    // The state machine determines the correct SPS for the current DSP rate and channel type
    // and passes it directly.
#ifdef USE_RTLSDR
    if (opts->audio_in_type == AUDIO_IN_RTL && ted_sps > 0) {
        rtl_stream_set_ted_sps(ted_sps);
        // Optional debug: log VC tuning parameters when CQPSK debug is enabled.
        {
            const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
            if (!cfg) {
                dsd_neo_config_init(NULL);
                cfg = dsd_neo_get_config();
            }
            if (cfg && cfg->debug_cqpsk_enable) {
                fprintf(stderr, "[TUNE] VC freq=%ld Hz rf_mod=%d sps=%d center=%d ted_sps=%d\n", freq, state->rf_mod,
                        state->samplesPerSymbol, state->symbolCenter, ted_sps);
            }
        }
    }
#endif

    // Ensure any queued audio tail plays before changing channels
    // Avoid blocking drain when invoked from SM tick context.
    if (!p25_sm_in_tick()) {
        dsd_drain_audio_output(opts);
    }
    if (opts->use_rigctl == 1) {
        if (opts->setmod_bw != 0) {
            SetModulation(opts->rigctl_sockfd, opts->setmod_bw);
        }
        SetFreq(opts->rigctl_sockfd, freq);
    } else if (opts->audio_in_type == AUDIO_IN_RTL) {
#ifdef USE_RTLSDR
        if (state->rtl_ctx) {
            rtl_stream_tune(state->rtl_ctx, (uint32_t)freq);
        }
#endif
    }
    state->p25_vc_freq[0] = state->p25_vc_freq[1] = freq;
    state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = freq;
    opts->p25_is_tuned = 1;
    opts->trunk_is_tuned = 1;
    // Reset activity timers so noCarrier() does not immediately force a return
    // to CC before we have a chance to acquire sync on the new VC.
    state->last_vc_sync_time = time(NULL);
    state->last_vc_sync_time_m = dsd_time_now_monotonic_s();
    state->last_cc_sync_time = state->last_vc_sync_time;
    state->last_cc_sync_time_m = state->last_vc_sync_time_m;
    state->p25_last_vc_tune_time = state->last_vc_sync_time;
    state->p25_last_vc_tune_time_m = state->last_vc_sync_time_m;
}

/**
 * @brief Tune to a P25 control channel candidate without marking as tuned.
 *
 * Avoids setting voice-tuned flags and only updates CC tracking metadata so
 * the state machine can continue its hunt.
 *
 * @param opts Decoder options with tuning configuration.
 * @param state Decoder state to update.
 * @param freq Target control channel frequency in Hz.
 * @param ted_sps TED samples-per-symbol to set (0 = no change). Caller should
 *        pass the CC SPS (4 for P25P2 TDMA CC, 5 for P25P1 CC).
 */
void
dsd_engine_trunk_tune_to_cc(dsd_opts* opts, dsd_state* state, long int freq, int ted_sps) {
    if (!opts || !state || freq <= 0) {
        return;
    }
    // Reset modulation auto-detect state for fresh acquisition.
    dsd_frame_sync_reset_mod_state();

    // NOTE: Costas/TED reset is deferred to the controller thread after the hardware
    // retune completes. See dsd_engine_trunk_tune_to_freq for the full rationale.

    // Ensure any queued audio tail plays before changing channels
    // Avoid blocking drain when invoked from SM tick context.
    if (!p25_sm_in_tick()) {
        dsd_drain_audio_output(opts);
    }
    if (opts->use_rigctl == 1) {
        if (opts->setmod_bw != 0) {
            SetModulation(opts->rigctl_sockfd, opts->setmod_bw);
        }
        SetFreq(opts->rigctl_sockfd, freq);
    } else if (opts->audio_in_type == AUDIO_IN_RTL) {
#ifdef USE_RTLSDR
        // Set TED SPS for control channel BEFORE tuning so that demod_reset_on_retune()
        // (triggered by the controller thread after retune) uses the correct CC SPS,
        // not the stale VC override value.
        // Clear the override so non-P25 protocols can have SPS computed automatically.
        // Use no_override variant so rate-change refresh can recalculate SPS later.
        if (ted_sps > 0) {
            rtl_stream_clear_ted_sps_override();
            rtl_stream_set_ted_sps_no_override(ted_sps);
        }
        if (state->rtl_ctx) {
            rtl_stream_tune(state->rtl_ctx, (uint32_t)freq);
        }
#endif
    }
    // Do not set p25_is_tuned/trunk_is_tuned here; this is a CC hunt action.
    state->trunk_cc_freq = (long int)freq;
    state->last_cc_sync_time = time(NULL);
    state->last_cc_sync_time_m = dsd_time_now_monotonic_s();
}
