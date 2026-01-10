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

#include <dsd-neo/core/cleanup.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dibit.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/frame.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/sync_patterns.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/dsp/dmr_sync.h>
#include <dsd-neo/dsp/symbol.h>
#include <dsd-neo/dsp/sync_calibration.h>
#include <dsd-neo/dsp/sync_hamming.h>
#ifdef USE_RTLSDR
#include <dsd-neo/runtime/rtl_stream_metrics_hooks.h>
#endif
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/platform/atomic_compat.h>
#include <dsd-neo/runtime/colors.h>
#include <dsd-neo/runtime/comp.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/exitflag.h>
#include <dsd-neo/runtime/frame_sync_hooks.h>
#include <dsd-neo/runtime/telemetry.h>

#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline void
dmr_set_symbol_timing(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return;
    }

    int demod_rate = 0;
#ifdef USE_RTLSDR
    if (opts->audio_in_type == AUDIO_IN_RTL && state->rtl_ctx) {
        demod_rate = (int)dsd_rtl_stream_metrics_hook_output_rate_hz();
    }
#endif

    state->samplesPerSymbol = dsd_opts_compute_sps_rate(opts, 4800, demod_rate);
    state->symbolCenter = dsd_opts_symbol_center(state->samplesPerSymbol);
}

/* Modulation auto-detect state (file scope for reset access).
 * Vote counters and Hamming distance tracking for C4FM/QPSK/GFSK switching.
 * These are atomic because trunk_tune_to_freq() resets them from the tuning
 * thread while getFrameSync() reads/writes them on the DSP thread. */
static atomic_int g_vote_qpsk = 0;
static atomic_int g_vote_c4fm = 0;
static atomic_int g_vote_gfsk = 0;
static atomic_int g_ham_c4fm_recent = 24;
static atomic_int g_ham_qpsk_recent = 24;
static atomic_int g_ham_gfsk_recent = 24;
static atomic_int g_qpsk_dwell_enter_ms = 0;

void
dsd_frame_sync_reset_mod_state(void) {
    atomic_store(&g_vote_qpsk, 0);
    atomic_store(&g_vote_c4fm, 0);
    atomic_store(&g_vote_gfsk, 0);
    atomic_store(&g_ham_c4fm_recent, 24);
    atomic_store(&g_ham_qpsk_recent, 24);
    atomic_store(&g_ham_gfsk_recent, 24);
    atomic_store(&g_qpsk_dwell_enter_ms, 0);
}

/*
 * P25 CQPSK handling - matches OP25 exactly.
 *
 * OP25 does NOT use constellation permutation tables. It only uses:
 * 1. Normal sync detection (P25_FRAME_SYNC_MAGIC)
 * 2. Polarity reversal detection (reverse_p ^= 0x02)
 * 3. Tuning error detection (log only, no dibit remapping)
 *
 * The Costas loop handles legitimate 90° phase ambiguity via PT_45 rotation.
 * Tuning errors (±1200Hz, ±2400Hz) cannot be fixed by dibit remapping - they
 * require RF correction.
 */

void
printFrameSync(dsd_opts* opts, dsd_state* state, char* frametype, int offset, char* modulation) {
    UNUSED3(state, offset, modulation);

    char timestr[9];
    getTimeC_buf(timestr);
    if (opts->verbose > 0) {
        fprintf(stderr, "%s ", timestr);
        fprintf(stderr, "Sync: %s ", frametype);
    }

    //oops, that made a nested if-if-if-if statement,
    //causing a memory leak

    // if (opts->verbose > 2)
    //fprintf (stderr,"o: %4i ", offset);
    // if (opts->verbose > 1)
    //fprintf (stderr,"mod: %s ", modulation);
    // if (opts->verbose > 2)
    //fprintf (stderr,"g: %f ", state->aout_gain);

    /* stack buffer; no free */
}

int
getFrameSync(dsd_opts* opts, dsd_state* state) {
    /* Defensive: inputs are required for sync logic */
    if (!opts || !state) {
        return -1;
    }

    /* Dwell timer for CQPSK entry uses file-scope g_qpsk_dwell_enter_ms. */
    const time_t now = time(NULL);
    // Periodic P25 trunk SM heartbeat (once per second) to enforce hangtime
    // fallbacks even if frame processing stalls due to signal loss.
    // Note: Only run the P25 trunk SM tick when P25 is the active protocol
    // context. This avoids unintended retunes (CC hunting) while trunking on
    // other protocols such as NXDN/DMR/EDACS.
    static time_t last_tick = 0;
    static time_t last_p25_seen = 0; // runtime cache of recent P25 activity
    if (now != last_tick) {
        // Detect current P25 activity via last observed sync type (P25p1: 0/1;
        // P25p2: 35/36) and maintain a small grace window after last P25
        // observation so watchdog fallbacks still run on brief fades.
        int p25_by_sync = (state && DSD_SYNC_IS_P25(state->lastsynctype)) ? 1 : 0;
        if (p25_by_sync) {
            last_p25_seen = now;
        }
        int p25_recent = (last_p25_seen != 0 && (now - last_p25_seen) <= 3) ? 1 : 0; // 3s grace
        int p25_active = p25_by_sync || p25_recent || (state && state->p25_p2_active_slot != -1);
        // Only drive the P25 trunk SM heartbeat when P25 trunking is enabled
        // and P25 appears to be the active/most-recent protocol context. This
        // avoids unintended CC hunts while trunking NXDN/DMR/EDACS.
        if (opts && opts->p25_trunk == 1 && p25_active) {
            dsd_frame_sync_hook_p25_sm_try_tick(opts, state);
        }
        last_tick = now;
    }
    /* detects frame sync and returns frame type
   *  0 = +P25p1
   *  1 = -P25p1
   *  2 = +X2-TDMA (non inverted signal data frame)
   *  3 = -X2-TDMA (inverted signal voice frame)
   *  4 = +X2-TDMA (non inverted signal voice frame)
   *  5 = -X2-TDMA (inverted signal data frame)
   *  6 = +D-STAR
   *  7 = -D-STAR
   *  8 = +M17 STR (non inverted stream frame)
   *  9 = -M17 STR (inverted stream frame)
   * 10 = +DMR (non inverted signal data frame)
   * 11 = -DMR (inverted signal voice frame)
   * 12 = +DMR (non inverted signal voice frame)
   * 13 = -DMR (inverted signal data frame)
   * 14 = +ProVoice
   * 15 = -ProVoice
   * 16 = +M17 LSF (non inverted link frame)
   * 17 = -M17 LSF (inverted link frame)
   * 18 = +D-STAR_HD
   * 19 = -D-STAR_HD
   * 20 = +dPMR Frame Sync 1
   * 21 = +dPMR Frame Sync 2
   * 22 = +dPMR Frame Sync 3
   * 23 = +dPMR Frame Sync 4
   * 24 = -dPMR Frame Sync 1
   * 25 = -dPMR Frame Sync 2
   * 26 = -dPMR Frame Sync 3
   * 27 = -dPMR Frame Sync 4
   * 28 = +NXDN (sync only)
   * 29 = -NXDN (sync only)
   * 30 = +YSF
   * 31 = -YSF
   * 32 = DMR MS Voice
   * 33 = DMR MS Data
   * 34 = DMR RC Data
   * 35 = +P25 P2
   * 36 = -P25 P2
   * 37 = +EDACS
     * 38 = -EDACS
     */

    // P25 CC hunting and all tuner control are owned by the P25 SM now.

    /* Respect user-locked demod selection from CLI (-mc/-mg/-mq/-m2).
     * For QPSK/CQPSK locks, also configure a sane CQPSK DSP chain so
     * that P25 LSM-style signals work out-of-the-box without env hacks. */
    {
        if (opts->mod_cli_lock) {
            int forced = opts->mod_qpsk ? 1 : (opts->mod_gfsk ? 2 : 0);
            state->rf_mod = forced;
        }
    }

    int i, t, dibit, sync, synctest_pos, lastt;
    float symbol;
    char synctest[25];
    char synctest12[13]; //dPMR
    char synctest10[11]; //NXDN FSW only
    char synctest32[33];
    char synctest20[21]; //YSF, P25P2
    char synctest48[49]; //EDACS
    char synctest8[9];   //M17
    char synctest16[17]; //M17 Preamble
    char modulation[8];
    char* synctest_p;
    char synctest_buf[10240]; //what actually is assigned to this, can't find its use anywhere?
    float lmin, lmax;
    int lidx;

    //assign t_max value based on decoding type expected (all non-auto decodes first)
    int t_max = 24; //initialize as an actual value to prevent any overflow related issues
    if (opts->frame_nxdn48 == 1 || opts->frame_nxdn96 == 1) {
        t_max = 10;
    }
    //dPMR
    else if (opts->frame_dpmr == 1) {
        t_max = 12; //based on Frame_Sync_2 pattern
    } else if (opts->frame_m17 == 1) {
        t_max = 8;
    } else if (DSD_SYNC_IS_YSF(state->lastsynctype)) {
        t_max = 20; //20 on YSF
    }
    //if Phase 2, then only 19
    else if (DSD_SYNC_IS_P25P2(state->lastsynctype) || (state->p25_p2_active_slot >= 0 && opts->frame_p25p2 == 1)) {
        t_max = 19; //Phase 2 S-ISCH and TDMA VC from trunk grant are only 19
    } else {
        t_max = 24; //24 for everything else
    }

    float lbuf[48],
        lbuf2
            [48]; //if we use t_max in these arrays, and t >=  t_max in condition below, then it can overflow those checks in there if t exceeds t_max
    float lsum;
    //init the lbuf
    memset(lbuf, 0, sizeof(lbuf));
    memset(lbuf2, 0, sizeof(lbuf2));

    // detect frame sync
    t = 0;
    synctest10[10] = 0;
    synctest[24] = 0;
    synctest8[8] = 0;
    synctest12[12] = 0;
    synctest16[16] = 0;
    synctest48[48] = 0;
    synctest32[32] = 0;
    synctest20[20] = 0;
    modulation[7] = 0; //not initialized or terminated (unsure if this would be an issue or not)
    synctest_pos = 0;
    synctest_p = synctest_buf + 10;
    sync = 0;
    // lmin/lmax initialized later before use
    lidx = 0;
    lastt = 0;

    //run here as well
    if (opts->use_ncurses_terminal == 1) {
        ui_publish_both_and_redraw(opts, state);
    }

    //slot 1
    watchdog_event_history(opts, state, 0);
    watchdog_event_current(opts, state, 0);

    //slot 2 for TDMA systems
    watchdog_event_history(opts, state, 1);
    watchdog_event_current(opts, state, 1);

    if ((opts->symboltiming == 1) && (state->carrier == 1)) {
        //fprintf (stderr,"\nSymbol Timing:\n");
        //printw("\nSymbol Timing:\n");
    }
    /* Modulation auto-detect state uses file-scope globals (g_vote_*, g_ham_*_recent)
     * so they can be reset by dsd_frame_sync_reset_mod_state() on channel tune. */

    while (sync == 0) {

        t++;

        //run ncurses printer more frequently when no sync to speed up responsiveness of it during no sync period
        //NOTE: Need to monitor and test this, if responsiveness issues arise, then disable this
        if (opts->use_ncurses_terminal == 1 && ((t % 300) == 0)) { //t maxes out at 1800 (6 times each getFrameSync)
            ui_publish_both_and_redraw(opts, state);
        }

        symbol = getSymbol(opts, state, 0);

        lbuf[lidx] = symbol;
        state->sbuf[state->sidx] = symbol;
        if (lidx == (t_max - 1)) //23 //9 for NXDN
        {
            lidx = 0;
        } else {
            lidx++;
        }
        if (state->sidx == (opts->ssize - 1)) {
            state->sidx = 0;
        } else {
            state->sidx++;
        }

        if (lastt == t_max) {
            lastt = 0;

            /* Reset SPS hunt counter when carrier is detected (sync found).
             * This locks the SPS at the rate that successfully found sync. */
            if (state->carrier == 1) {
                state->sps_hunt_counter = 0;
            }

            /* Skip auto switching entirely if user locked demod (-m[c/g/q/2]). */
            if (!opts->mod_cli_lock) {
                /* Start with current modulation; Hamming distance will override if
                 * another modulation shows clearly better sync pattern matching. */
                int want_mod = state->rf_mod; /* 0=C4FM, 1=QPSK, 2=GFSK */

                /* Bias decision with demod SNR when available to avoid C4FM<->QPSK flapping
                   on P25 CQPSK. Compare normalized quality levels since the modulations have
                   different typical SNR ranges:
                   - C4FM: moderate quality at 4-10 dB, good at >10 dB
                   - QPSK: moderate quality at 10-16 dB, good at >16 dB
                   QPSK typically reads ~6 dB higher than C4FM for equivalent signal quality.
                   Prefer QPSK when its normalized SNR clearly exceeds C4FM; conversely prefer
                   C4FM only when it exceeds QPSK by a larger margin. Also apply a small
                   stickiness when already in QPSK and SNRs are similar. */
#ifdef USE_RTLSDR
                do {
                    /* Pull smoothed SNR; fall back to lightweight estimators if needed */
                    double snr_c = dsd_rtl_stream_metrics_hook_snr_c4fm_db();
                    double snr_q = dsd_rtl_stream_metrics_hook_snr_cqpsk_db();
                    if (snr_c <= -50.0) {
                        snr_c = dsd_rtl_stream_metrics_hook_snr_c4fm_eye_db();
                    }
                    if (snr_q <= -50.0) {
                        snr_q = dsd_rtl_stream_metrics_hook_snr_qpsk_const_db();
                    }
                    if (snr_c > -50.0 || snr_q > -50.0) {
                        /* Only apply bias when at least one metric is sane */
                        if (snr_q > -50.0 && snr_c > -50.0) {
                            /* Normalize: QPSK typically reads 6 dB higher than C4FM for same quality.
                               Compare (snr_q - 6) vs snr_c to account for this offset. */
                            const double kQpskOffsetDb = 6.0;
                            double normalized_delta = (snr_q - kQpskOffsetDb) - snr_c;
                            uint32_t now_ms = (uint32_t)dsd_time_monotonic_ms();
                            uint32_t dwell_enter_ms = (uint32_t)atomic_load(&g_qpsk_dwell_enter_ms);
                            int in_qpsk_dwell = (state->rf_mod == 1 && dwell_enter_ms != 0
                                                 && (uint32_t)(now_ms - dwell_enter_ms) < 2000U);
                            if (normalized_delta >= 2.0) {
                                want_mod = 1; /* QPSK clearly better (normalized) */
                            } else if (normalized_delta <= -3.0 && !in_qpsk_dwell) {
                                want_mod = 0; /* C4FM clearly better (but not during dwell) */
                            } else {
                                /* Within small margin: if currently QPSK, keep favoring it */
                                if (state->rf_mod == 1) {
                                    want_mod = 1;
                                }
                            }
                        } else if (snr_q > -50.0 && state->rf_mod == 1) {
                            /* Only QPSK SNR available and already on QPSK: prefer QPSK */
                            want_mod = 1;
                        }
                    }
                } while (0);
#endif

                /* Hamming distance-based override: sync pattern match quality is a much
                 * stronger indicator than SNR for modulation correctness. A strong CQPSK
                 * signal on the C4FM path still produces "decent" C4FM SNR, but the
                 * Hamming distance to sync will be poor. Conversely, QPSK on correct
                 * signal achieves ham ≤ 2 reliably. Same applies to GFSK vs C4FM. */
                {
                    /* Decay recent ham values slowly (persist good state) */
                    int ham_c4fm = atomic_load(&g_ham_c4fm_recent);
                    int ham_qpsk = atomic_load(&g_ham_qpsk_recent);
                    int ham_gfsk = atomic_load(&g_ham_gfsk_recent);
                    if (ham_c4fm < 24) {
                        atomic_store(&g_ham_c4fm_recent, ham_c4fm + 1);
                        ham_c4fm++;
                    }
                    if (ham_qpsk < 24) {
                        atomic_store(&g_ham_qpsk_recent, ham_qpsk + 1);
                        ham_qpsk++;
                    }
                    if (ham_gfsk < 24) {
                        atomic_store(&g_ham_gfsk_recent, ham_gfsk + 1);
                        ham_gfsk++;
                    }

                    /* Pick modulation with lowest recent Hamming distance; ties keep current. */
                    int best_mod = want_mod;
                    int best_ham = (want_mod == 1) ? ham_qpsk : (want_mod == 2) ? ham_gfsk : ham_c4fm;
                    if (ham_c4fm < best_ham) {
                        best_ham = ham_c4fm;
                        best_mod = 0;
                    }
                    int qpsk_enabled = (opts->frame_p25p1 == 1 || opts->frame_p25p2 == 1);
                    if (qpsk_enabled && ham_qpsk < best_ham) {
                        best_ham = ham_qpsk;
                        best_mod = 1;
                    }
                    if (ham_gfsk < best_ham) {
                        best_ham = ham_gfsk;
                        best_mod = 2;
                    }

                    /* Strong sync match (ham ≤ 3): use that modulation */
                    if (best_ham <= 3) {
                        want_mod = best_mod;
                    }
                    /* Clear winner with ≥4 ham advantage over current want_mod */
                    else if (best_ham < 24) {
                        int current_ham = (want_mod == 2) ? ham_gfsk : (want_mod == 1) ? ham_qpsk : ham_c4fm;
                        if (current_ham >= 24 || best_ham + 4 <= current_ham) {
                            want_mod = best_mod;
                        }
                    }
                }

                /* Update votes (use hysteresis; be more eager for GFSK to avoid misclassification dwell) */
                if (want_mod == 1) {
                    atomic_fetch_add(&g_vote_qpsk, 1);
                    atomic_store(&g_vote_c4fm, 0);
                    atomic_store(&g_vote_gfsk, 0);
                } else if (want_mod == 2) {
                    atomic_fetch_add(&g_vote_gfsk, 1);
                    atomic_store(&g_vote_qpsk, 0);
                    atomic_store(&g_vote_c4fm, 0);
                } else {
                    atomic_fetch_add(&g_vote_c4fm, 1);
                    atomic_store(&g_vote_qpsk, 0);
                    atomic_store(&g_vote_gfsk, 0);
                }

                int do_switch = -1; /* -1=no-op, else new rf_mod */
                {
                    /*
             * Require 2 consecutive windows for C4FM<->QPSK to prevent flapping on marginal signals.
             * For GFSK (DMR/dPMR/NXDN), permit immediate switch on first qualifying window to minimize
             * misclassification time that can corrupt early bursts and elevate audio errors.
             */
                    /* Slightly increase hysteresis when leaving QPSK to avoid flip-flop */
                    uint32_t now_ms = (uint32_t)dsd_time_monotonic_ms();
                    uint32_t dwell_enter_ms = (uint32_t)atomic_load(&g_qpsk_dwell_enter_ms);
                    int in_qpsk_dwell2 =
                        (state->rf_mod == 1 && dwell_enter_ms != 0 && (uint32_t)(now_ms - dwell_enter_ms) < 2000U);
                    int req_c4_votes = (state->rf_mod == 1) ? (in_qpsk_dwell2 ? 5 : 3) : 2;
                    int vote_qpsk = atomic_load(&g_vote_qpsk);
                    int vote_gfsk = atomic_load(&g_vote_gfsk);
                    int vote_c4fm = atomic_load(&g_vote_c4fm);
                    if (want_mod == 1 && vote_qpsk >= 2 && state->rf_mod != 1) {
                        do_switch = 1;
                    } else if (want_mod == 2 && vote_gfsk >= 1 && state->rf_mod != 2) {
                        do_switch = 2; /* eager switch to GFSK on first vote */
                    } else if (want_mod == 0 && vote_c4fm >= req_c4_votes && state->rf_mod != 0) {
                        do_switch = 0;
                    }
                }
                if (do_switch >= 0) {
                    /* Record entry time when switching into QPSK to add short dwell
                     * that resists immediate fallback to C4FM on marginal signals. */
                    if (do_switch == 1) {
                        atomic_store(&g_qpsk_dwell_enter_ms, (int)(uint32_t)dsd_time_monotonic_ms());
                    } else if (state->rf_mod == 1) {
                        /* Leaving QPSK: clear dwell marker */
                        atomic_store(&g_qpsk_dwell_enter_ms, 0);
                    }
                    state->rf_mod = do_switch;
                    /* Reset Hamming distance trackers so new modulation starts fresh */
                    atomic_store(&g_ham_c4fm_recent, 24);
                    atomic_store(&g_ham_qpsk_recent, 24);
                    atomic_store(&g_ham_gfsk_recent, 24);
                    /* Manual-only DSP: avoid automatic toggling here. */
                }
            } /* end !mod_cli_lock */
        } else {
            lastt++;
        }

        if (state->dibit_buf_p > state->dibit_buf + 900000) {
            state->dibit_buf_p = state->dibit_buf + 200;
        }

        //determine dibit state
#ifdef USE_RTLSDR
        /* Debug: print symbol values when DSD_NEO_DEBUG_SYNC=1 */
        {
            static int sym_count = 0;
            static int pos_count = 0, neg_count = 0;
            static float sym_min = 1e9f, sym_max = -1e9f, sym_sum = 0.0f;
            const dsdneoRuntimeConfig* cfg_dbg = dsd_neo_get_config();
            if (!cfg_dbg) {
                dsd_neo_config_init(opts);
                cfg_dbg = dsd_neo_get_config();
            }
            if (cfg_dbg && cfg_dbg->debug_sync_enable) {
                if (symbol < sym_min) {
                    sym_min = symbol;
                }
                if (symbol > sym_max) {
                    sym_max = symbol;
                }
                sym_sum += symbol;
                if (symbol > 0) {
                    pos_count++;
                } else {
                    neg_count++;
                }
                if (++sym_count >= 4800) {
                    float dc = sym_sum / (float)sym_count;
                    fprintf(stderr, "[SYNC] range:[%.1f,%.1f] dc:%.2f ratio(1:3)=%d:%d\n", sym_min, sym_max, dc,
                            pos_count, neg_count);
                    sym_min = 1e9f;
                    sym_max = -1e9f;
                    sym_sum = 0.0f;
                    pos_count = neg_count = 0;
                    sym_count = 0;
                }
            }
        }
#endif
        /* For CQPSK mode with TED active, use 4-level slicer matching OP25's fsk4_slicer_fb.
         * The CQPSK demod outputs phase values scaled by 4/π, giving symbol levels at ±1, ±3.
         * Using fixed ±2.0 thresholds produces all 4 dibit values needed for P25 sync detection. */
        int cqpsk_4level = 0;
#ifdef USE_RTLSDR
        if (state->rf_mod == 1 && opts->audio_in_type == AUDIO_IN_RTL
            && (opts->frame_p25p1 == 1 || opts->frame_p25p2 == 1)) {
            int dsp_cqpsk = 0, dsp_fll = 0, dsp_ted = 0;
            dsd_rtl_stream_metrics_hook_dsp_get(&dsp_cqpsk, &dsp_fll, &dsp_ted);
            if (dsp_cqpsk && dsp_ted) {
                cqpsk_4level = 1;
            }
        }
#endif
        if (cqpsk_4level) {
            /* 4-level CQPSK slicer with fixed thresholds (same as cqpsk_slice in dsd_dibit.c):
             *   sym >= +2.0  → dibit 1 (symbol +3, phase +135°)
             *   0 <= sym < +2.0 → dibit 0 (symbol +1, phase +45°)
             *   -2.0 <= sym < 0 → dibit 2 (symbol -1, phase -45°)
             *   sym < -2.0  → dibit 3 (symbol -3, phase -135°) */
            float sym = symbol;
            /* Recenter CQPSK symbols using the running min/max midpoint to remove DC bias
               before slicing. This helps keep the fixed ±2.0 thresholds aligned when the
               differential phase output drifts slightly off zero. */
            sym -= state->center;
            int d;
            if (sym >= 2.0f) {
                d = 1;
            } else if (sym >= 0.0f) {
                d = 0;
            } else if (sym >= -2.0f) {
                d = 2;
            } else {
                d = 3;
            }
            /* Debug: log slicer input/output when DSD_NEO_DEBUG_CQPSK=1 */
            {
                static int sample_count = 0;
                static int hist[4] = {0, 0, 0, 0};
                static float sym_sum = 0.0f;
                static float sym_min = 1000.0f, sym_max = -1000.0f;
                const dsdneoRuntimeConfig* cfg_dbg = dsd_neo_get_config();
                if (!cfg_dbg) {
                    dsd_neo_config_init(opts);
                    cfg_dbg = dsd_neo_get_config();
                }
                if (cfg_dbg && cfg_dbg->debug_cqpsk_enable) {
                    hist[d]++;
                    sym_sum += sym;
                    if (sym < sym_min) {
                        sym_min = sym;
                    }
                    if (sym > sym_max) {
                        sym_max = sym;
                    }
                    if (++sample_count >= 4800) {
                        float sym_avg = sym_sum / sample_count;
                        fprintf(stderr,
                                "[SLICER] d0:%.1f%% d1:%.1f%% d2:%.1f%% d3:%.1f%% avg:%.2f range:[%.2f,%.2f] (n=%d)\n",
                                100.0f * hist[0] / sample_count, 100.0f * hist[1] / sample_count,
                                100.0f * hist[2] / sample_count, 100.0f * hist[3] / sample_count, sym_avg, sym_min,
                                sym_max, sample_count);
                        hist[0] = hist[1] = hist[2] = hist[3] = 0;
                        sample_count = 0;
                        sym_sum = 0.0f;
                        sym_min = 1000.0f;
                        sym_max = -1000.0f;
                    }
                }
            }
            *state->dibit_buf_p = d;
            state->dibit_buf_p++;
            dibit = '0' + d;
        } else if (symbol > 0) {
            *state->dibit_buf_p = 1;
            state->dibit_buf_p++;
            dibit = 49; // '1'
        } else {
            *state->dibit_buf_p = 3;
            state->dibit_buf_p++;
            dibit = 51; // '3'
        }

        if (opts->symbol_out_f && dibit != 0) {
            int csymbol;
            if (cqpsk_4level) {
                /* For CQPSK 4-level, dibit is already '0'..'3' */
                csymbol = dibit - '0';
            } else {
                csymbol = 0;
                if (dibit == 49) {
                    csymbol = 1; //1
                }
                if (dibit == 51) {
                    csymbol = 3; //3
                }
            }
            //fprintf (stderr, "%d", dibit);
            fputc(csymbol, opts->symbol_out_f);
        }

        //digitize test for storing dibits in buffer correctly for dmr recovery

        if (state->dmr_payload_p > state->dmr_payload_buf + 900000) {
            state->dmr_payload_p = state->dmr_payload_buf + 200;
        }
        if (state->dmr_reliab_p && state->dmr_reliab_p > state->dmr_reliab_buf + 900000) {
            state->dmr_reliab_p = state->dmr_reliab_buf + 200;
        }

        if (cqpsk_4level) {
            float sym = symbol - state->center; /* recenter for CQPSK thresholding */
            /* Use the same 4-level slicer for DMR payload buffer (CQPSK symbols at ±1, ±3) */
            int d;
            if (sym >= 2.0f) {
                d = 1;
            } else if (sym >= 0.0f) {
                d = 0;
            } else if (sym >= -2.0f) {
                d = 2;
            } else {
                d = 3;
            }
            *state->dmr_payload_p = d;
            /* Reliability computed via dmr_compute_reliability() which handles CQPSK internally */
            if (state->dmr_reliab_p) {
                *state->dmr_reliab_p = dmr_compute_reliability(state, symbol);
                state->dmr_reliab_p++;
            }
        } else {
            if (symbol > state->center) {
                if (symbol > state->umid) {
                    *state->dmr_payload_p = 1; // +3
                } else {
                    *state->dmr_payload_p = 0; // +1
                }
            } else {
                if (symbol < state->lmid) {
                    *state->dmr_payload_p = 3; // -3
                } else {
                    *state->dmr_payload_p = 2; // -1
                }
            }
            /* Reliability computed via dmr_compute_reliability() which handles C4FM/GFSK internally */
            if (state->dmr_reliab_p) {
                *state->dmr_reliab_p = dmr_compute_reliability(state, symbol);
                state->dmr_reliab_p++;
            }
        }

        state->dmr_payload_p++;
        // end digitize and dmr buffer testing

        /* Store dibit into sync window as ASCII 0..3. */
        int sync_dibit = dibit & 0x3;
        *synctest_p = (char)('0' + (sync_dibit & 0x3));
        if (t >= t_max) //works excelent now with short sync patterns, and no issues with large ones!
        {
            for (i = 0; i < t_max; i++) //24
            {
                lbuf2[i] = lbuf[i];
            }
            qsort(lbuf2, t_max, sizeof(float), comp);
            lmin = (lbuf2[1] + lbuf2[2] + lbuf2[3]) / 3.0f;
            lmax = (lbuf2[t_max - 3] + lbuf2[t_max - 2] + lbuf2[t_max - 1]) / 3.0f;

            if (state->rf_mod == 1) {
                state->minbuf[state->midx] = lmin;
                state->maxbuf[state->midx] = lmax;
                if (state->midx == (opts->msize - 1)) //-1
                {
                    state->midx = 0;
                } else {
                    state->midx++;
                }
                lsum = 0.0f;
                for (i = 0; i < opts->msize; i++) {
                    lsum += state->minbuf[i];
                }
                state->min = lsum / (float)opts->msize;
                lsum = 0.0f;
                for (i = 0; i < opts->msize; i++) {
                    lsum += state->maxbuf[i];
                }
                state->max = lsum / (float)opts->msize;
                state->center = ((state->max) + (state->min)) / 2.0f;
                state->maxref = (state->max) * 0.80F;
                state->minref = (state->min) * 0.80F;
            } else {
                state->maxref = state->max;
                state->minref = state->min;
            }

            // Optional SNR-based pre-decode squelch: skip expensive sync search when SNR is low.
            // Falls back to legacy power squelch gating for certain modes.
#ifdef USE_RTLSDR
            {
                const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
                int snr_gate = 0;
                if (cfg && cfg->snr_sql_is_set) {
                    double snr_db = -200.0;
                    if (opts->frame_p25p1 == 1) {
                        snr_db = dsd_rtl_stream_metrics_hook_snr_c4fm_db();
                    } else if (opts->frame_p25p2 == 1) {
                        snr_db = dsd_rtl_stream_metrics_hook_snr_cqpsk_db();
                    } else if (opts->frame_nxdn48 == 1 || opts->frame_nxdn96 == 1 || opts->frame_dpmr == 1
                               || opts->frame_m17 == 1) {
                        snr_db = dsd_rtl_stream_metrics_hook_snr_gfsk_db();
                    }
                    if (snr_db > -150.0 && snr_db < (double)cfg->snr_sql_db) {
                        snr_gate = 1;
                    }
                }
                if (snr_gate) {
                    goto SYNC_TEST_END;
                }
            }
#endif
            // Legacy power-based pre-gate for some GFSK modes when using RTL input
            if (opts->audio_in_type == AUDIO_IN_RTL && opts->rtl_pwr < opts->rtl_squelch_level) {
                if (opts->frame_nxdn48 == 1 || opts->frame_nxdn96 == 1 || opts->frame_dpmr == 1
                    || opts->frame_m17 == 1) {
                    goto SYNC_TEST_END;
                }
            }

            strncpy(synctest, (synctest_p - 23), 24);
            strncpy(synctest20, (synctest_p - 19), 20);
            /* OP25 compatibility: no dibit remapping based on sync pattern.
             * The Costas loop handles phase ambiguity; tuning errors need RF correction. */
            const char* p25_sync_window = synctest;
#ifdef USE_RTLSDR
            /* Debug: print sync pattern when DSD_NEO_DEBUG_SYNC=1 */
            {
                static int debug_count = 0;
                const dsdneoRuntimeConfig* cfg_dbg = dsd_neo_get_config();
                if (!cfg_dbg) {
                    dsd_neo_config_init(opts);
                    cfg_dbg = dsd_neo_get_config();
                }
                int debug_sync = (cfg_dbg && cfg_dbg->debug_sync_enable) ? 1 : 0;
                int debug_cqpsk = (cfg_dbg && cfg_dbg->debug_cqpsk_enable) ? 1 : 0;

                if (debug_sync && (++debug_count % 4800) == 0) {
                    fprintf(stderr, "[SYNC] pattern=%s expect=%s\n", synctest, P25P1_SYNC);
                }
                if (debug_cqpsk && state->rf_mod == 1) {
                    /* Compute Hamming distance of current window vs P25 sync (normal/inverted). */
                    int ham_norm = 0, ham_inv = 0;
                    /* Evaluate alternate dibit remaps to spot polarity/bit-order/rotation issues. */
                    int ham_ident = 0, ham_invert = 0, ham_swap = 0, ham_xor3 = 0, ham_rot = 0;
                    for (int k = 0; k < 24; k++) {
                        int d = (unsigned char)synctest[k];
                        if (d >= '0' && d <= '3') {
                            d -= '0';
                        }
                        /* accept raw 0..3 values too */
                        int expect_n = P25P1_SYNC[k] - '0';
                        int expect_i = INV_P25P1_SYNC[k] - '0';
                        if (d != expect_n) {
                            ham_norm++;
                        }
                        if (d != expect_i) {
                            ham_inv++;
                        }
                        /* Remaps */
                        int d_inv = (d == 0) ? 2 : (d == 1) ? 3 : (d == 2) ? 0 : 1;
                        int d_swap = ((d & 1) << 1) | ((d & 2) >> 1); /* swap bit order */
                        int d_xor3 = d ^ 0x3;                         /* bitwise not in 2-bit space */
                        /* 90° rotation (cyclic remap 0->1->3->2->0) */
                        int d_rot;
                        switch (d & 0x3) {
                            case 0: d_rot = 1; break;
                            case 1: d_rot = 3; break;
                            case 2: d_rot = 0; break;
                            default: d_rot = 2; break; /* d==3 */
                        }
                        if (d != expect_n) {
                            ham_ident++;
                        }
                        if (d_inv != expect_n) {
                            ham_invert++;
                        }
                        if (d_swap != expect_n) {
                            ham_swap++;
                        }
                        if (d_xor3 != expect_n) {
                            ham_xor3++;
                        }
                        if (d_rot != expect_n) {
                            ham_rot++;
                        }
                    }
                    /* Log sparsely to avoid spam; every ~1200 symbols (~0.25s at 4800 sps). */
                    static int dbg_win = 0;
                    if ((++dbg_win % 1200) == 0) {
                        fprintf(stderr,
                                "[SYNCDBG] ham(norm=%d inv=%d ident=%d inv2=%d swap=%d xor3=%d rot=%d) win=%.*s\n",
                                ham_norm, ham_inv, ham_ident, ham_invert, ham_swap, ham_xor3, ham_rot, 24, synctest);
                    }
                }
            }
#endif
            /* Compute C4FM Hamming distance for modulation switching.
             * Track best (lowest) ham between normal and inverted sync patterns.
             * This runs on every window even if we don't get an exact match,
             * so we have a quality metric for C4FM path. */
            if (opts->frame_p25p1 == 1 && !opts->mod_cli_lock) {
                int ham_norm = 0, ham_inv = 0;
                for (int k = 0; k < 24; k++) {
                    int d = (unsigned char)synctest[k] - '0';
                    int expect_n = P25P1_SYNC[k] - '0';
                    int expect_i = INV_P25P1_SYNC[k] - '0';
                    if (d != expect_n) {
                        ham_norm++;
                    }
                    if (d != expect_i) {
                        ham_inv++;
                    }
                }
                int c4fm_ham = (ham_norm < ham_inv) ? ham_norm : ham_inv;
                int ham_c4fm_cur = atomic_load(&g_ham_c4fm_recent);
                if (c4fm_ham < ham_c4fm_cur) {
                    atomic_store(&g_ham_c4fm_recent, c4fm_ham);
                }
            }
            /* Compute CQPSK Hamming distance (with common dibit remaps) for modulation switching.
             * Remaps cover phase rotations/inversions that appear when CQPSK is sliced with a
             * different dibit ordering. This keeps QPSK candidates in the vote even without
             * RTL-specific SNR metrics. */
            if ((opts->frame_p25p1 == 1 || opts->frame_p25p2 == 1) && !opts->mod_cli_lock) {
                int best_qpsk_ham = 24;
                if (opts->frame_p25p1 == 1) {
                    best_qpsk_ham = dsd_qpsk_sync_hamming_with_remaps(synctest, P25P1_SYNC, INV_P25P1_SYNC, 24);
                }
                if (opts->frame_p25p2 == 1) {
                    int ham_p2 = dsd_qpsk_sync_hamming_with_remaps(synctest20, P25P2_SYNC, INV_P25P2_SYNC, 20);
                    /* Scale 20-dibit ham to 24-dibit baseline for fair comparison. */
                    int ham_p2_scaled = (ham_p2 * 24 + 19) / 20;
                    if (ham_p2_scaled < best_qpsk_ham || opts->frame_p25p1 == 0) {
                        best_qpsk_ham = ham_p2_scaled;
                    }
                }
                int ham_qpsk_cur = atomic_load(&g_ham_qpsk_recent);
                if (best_qpsk_ham < ham_qpsk_cur) {
                    atomic_store(&g_ham_qpsk_recent, best_qpsk_ham);
                }
            }
            /* Compute GFSK Hamming distance for modulation switching.
             * Check DMR sync patterns (24-dibit, same length as P25) for fair comparison.
             * Take minimum across voice/data and BS/MS variants. */
            if ((opts->frame_dmr == 1 || opts->frame_nxdn48 == 1 || opts->frame_nxdn96 == 1 || opts->frame_dpmr == 1)
                && !opts->mod_cli_lock) {
                int best_gfsk_ham = 24;
                /* DMR patterns (24 dibits) */
                if (opts->frame_dmr == 1) {
                    const char* dmr_patterns[] = {DMR_BS_DATA_SYNC, DMR_BS_VOICE_SYNC, DMR_MS_DATA_SYNC,
                                                  DMR_MS_VOICE_SYNC};
                    for (int p = 0; p < 4; p++) {
                        int ham = 0;
                        for (int k = 0; k < 24; k++) {
                            int d = (unsigned char)synctest[k] - '0';
                            int expect = dmr_patterns[p][k] - '0';
                            if (d != expect) {
                                ham++;
                            }
                        }
                        if (ham < best_gfsk_ham) {
                            best_gfsk_ham = ham;
                        }
                    }
                }
                /* dPMR patterns (24 dibits for FS1/FS4) */
                if (opts->frame_dpmr == 1) {
                    const char* dpmr_patterns[] = {DPMR_FRAME_SYNC_1, DPMR_FRAME_SYNC_4, INV_DPMR_FRAME_SYNC_1,
                                                   INV_DPMR_FRAME_SYNC_4};
                    for (int p = 0; p < 4; p++) {
                        int ham = 0;
                        for (int k = 0; k < 24; k++) {
                            int d = (unsigned char)synctest[k] - '0';
                            int expect = dpmr_patterns[p][k] - '0';
                            if (d != expect) {
                                ham++;
                            }
                        }
                        if (ham < best_gfsk_ham) {
                            best_gfsk_ham = ham;
                        }
                    }
                }
                /* NXDN uses 10-dibit FSW; scale to 24-dibit equivalent for fair comparison.
                 * ham_scaled = ham_10 * 24 / 10 = ham_10 * 2.4 */
                if (opts->frame_nxdn48 == 1 || opts->frame_nxdn96 == 1) {
                    strncpy(synctest10, (synctest_p - 9), 10);
                    /* Common NXDN FSW patterns */
                    const char* nxdn_patterns[] = {"3131331131", "1313113313"};
                    for (int p = 0; p < 2; p++) {
                        int ham = 0;
                        for (int k = 0; k < 10; k++) {
                            int d = (unsigned char)synctest10[k] - '0';
                            int expect = nxdn_patterns[p][k] - '0';
                            if (d != expect) {
                                ham++;
                            }
                        }
                        /* Scale 10-dibit ham to 24-dibit equivalent */
                        int scaled_ham = (ham * 24 + 9) / 10; /* round up */
                        if (scaled_ham < best_gfsk_ham) {
                            best_gfsk_ham = scaled_ham;
                        }
                    }
                }
                int ham_gfsk_cur = atomic_load(&g_ham_gfsk_recent);
                if (best_gfsk_ham < ham_gfsk_cur) {
                    atomic_store(&g_ham_gfsk_recent, best_gfsk_ham);
                }
            }
            if (opts->frame_p25p1 == 1) {
                if (strcmp(p25_sync_window, P25P1_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    state->dmrburstR = 17;
                    state->payload_algidR = 0;
                    state->dmr_stereo =
                        1; //check to see if this causes dmr data issues later on during mixed sync types
                    sprintf(state->ftype, "P25 Phase 1");
                    if (opts->errorbars == 1) {
                        printFrameSync(opts, state, "+P25p1", synctest_pos + 1, modulation);
                    }
                    state->lastsynctype = DSD_SYNC_P25P1_POS;
                    state->last_cc_sync_time = now;
                    /* Sync-time calibration:
                     * - C4FM: warm-start slicer thresholds
                     * - CQPSK/QPSK: warm-start only center (DC bias) */
                    if (state->rf_mod == 0) {
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 24);
                    } else if (state->rf_mod == 1) {
                        dsd_sync_warm_start_center_outer_only(opts, state, 24);
                    }
                    return DSD_SYNC_P25P1_POS;
                }
                if (strcmp(p25_sync_window, INV_P25P1_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    state->dmrburstR = 17;
                    state->payload_algidR = 0;
                    state->dmr_stereo =
                        1; //check to see if this causes dmr data issues later on during mixed sync types
                    sprintf(state->ftype, "P25 Phase 1");
                    if (opts->errorbars == 1) {
                        printFrameSync(opts, state, "-P25p1 ", synctest_pos + 1, modulation);
                    }
                    state->lastsynctype = DSD_SYNC_P25P1_NEG;
                    state->last_cc_sync_time = now;
                    /* Sync-time calibration:
                     * - C4FM: warm-start slicer thresholds
                     * - CQPSK/QPSK: warm-start only center (DC bias) */
                    if (state->rf_mod == 0) {
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 24);
                    } else if (state->rf_mod == 1) {
                        dsd_sync_warm_start_center_outer_only(opts, state, 24);
                    }
                    return DSD_SYNC_P25P1_NEG;
                }
            }
            /* When DMR/dPMR/NXDN are enabled targets, proactively disable FM AGC/limiter which can
             * distort 2-level/FSK symbol envelopes and elevate early audio errors under marginal SNR.
             * Also force FLL/TED off for FSK paths — but only when we are actually on the FSK/GFSK path. */
#ifdef USE_RTLSDR
            // Automatic DSP toggling disabled by user request.
            // if ((opts->frame_dmr == 1 || opts->frame_dpmr == 1 || opts->frame_nxdn48 == 1 || opts->frame_nxdn96 == 1)
            //     && state->rf_mod == 2) { /* 2 = GFSK/FSK family */
            //     rtl_stream_set_fm_agc(0);
            //     rtl_stream_set_fm_limiter(0);
            //     rtl_stream_toggle_fll(0);
            //     rtl_stream_toggle_ted(0);
            // }
#endif
            if (opts->frame_x2tdma == 1) {
                if ((strcmp(synctest, X2TDMA_BS_DATA_SYNC) == 0) || (strcmp(synctest, X2TDMA_MS_DATA_SYNC) == 0)) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + (lmax)) / 2;
                    state->min = ((state->min) + (lmin)) / 2;
                    if (opts->inverted_x2tdma == 0) {
                        // data frame
                        sprintf(state->ftype, "X2-TDMA");
                        if (opts->errorbars == 1) {
                            printFrameSync(opts, state, "+X2-TDMA ", synctest_pos + 1, modulation);
                        }
                        state->lastsynctype = DSD_SYNC_X2TDMA_DATA_POS;
                        /* Warm-start slicer thresholds for improved decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 24);
                        return DSD_SYNC_X2TDMA_DATA_POS;
                    } else {
                        // inverted voice frame
                        sprintf(state->ftype, "X2-TDMA");
                        if (opts->errorbars == 1) {
                            printFrameSync(opts, state, "-X2-TDMA ", synctest_pos + 1, modulation);
                        }
                        if (state->lastsynctype != DSD_SYNC_X2TDMA_VOICE_NEG) {
                            state->firstframe = 1;
                        }
                        state->lastsynctype = DSD_SYNC_X2TDMA_VOICE_NEG;
                        /* Warm-start slicer thresholds for improved decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 24);
                        return DSD_SYNC_X2TDMA_VOICE_NEG;
                    }
                }
                if ((strcmp(synctest, X2TDMA_BS_VOICE_SYNC) == 0) || (strcmp(synctest, X2TDMA_MS_VOICE_SYNC) == 0)) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    if (opts->inverted_x2tdma == 0) {
                        // voice frame
                        sprintf(state->ftype, "X2-TDMA");
                        if (opts->errorbars == 1) {
                            printFrameSync(opts, state, "+X2-TDMA ", synctest_pos + 1, modulation);
                        }
                        if (state->lastsynctype != DSD_SYNC_X2TDMA_VOICE_POS) {
                            state->firstframe = 1;
                        }
                        state->lastsynctype = DSD_SYNC_X2TDMA_VOICE_POS;
                        /* Warm-start slicer thresholds for improved decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 24);
                        return DSD_SYNC_X2TDMA_VOICE_POS;
                    } else {
                        // inverted data frame
                        sprintf(state->ftype, "X2-TDMA");
                        if (opts->errorbars == 1) {
                            printFrameSync(opts, state, "-X2-TDMA ", synctest_pos + 1, modulation);
                        }
                        state->lastsynctype = DSD_SYNC_X2TDMA_DATA_NEG;
                        /* Warm-start slicer thresholds for improved decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 24);
                        return DSD_SYNC_X2TDMA_DATA_NEG;
                    }
                }
            }
            //YSF sync
            strncpy(synctest20, (synctest_p - 19), 20);
            if (opts->frame_ysf == 1) {
                if (strcmp(synctest20, FUSION_SYNC) == 0) {
                    printFrameSync(opts, state, "+YSF ", synctest_pos + 1, modulation);
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    opts->inverted_ysf = 0;
                    state->lastsynctype = DSD_SYNC_YSF_POS;
                    /* Warm-start slicer thresholds for improved FICH decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 20);
                    return DSD_SYNC_YSF_POS;
                } else if (strcmp(synctest20, INV_FUSION_SYNC) == 0) {
                    printFrameSync(opts, state, "-YSF ", synctest_pos + 1, modulation);
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    opts->inverted_ysf = 1;
                    state->lastsynctype = DSD_SYNC_YSF_NEG;
                    /* Warm-start slicer thresholds for improved FICH decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 20);
                    return DSD_SYNC_YSF_NEG;
                }
            }
            //end YSF sync

            //M17 Sync -- Hamming distance based with auto-polarity detection
            strncpy(synctest16, (synctest_p - 15), 16);
            strncpy(synctest8, (synctest_p - 7), 8);
            if (opts->frame_m17 == 1) {
                /* Compute Hamming distance to all M17 8-dibit patterns.
                 * M17_PRE/M17_PIV are preambles used to auto-detect polarity.
                 * M17_LSF/M17_STR are data frames; their interpretation depends on polarity. */
                int ham_pre = dsd_sync_hamming_distance(synctest8, M17_PRE, 8);
                int ham_piv = dsd_sync_hamming_distance(synctest8, M17_PIV, 8);
                int ham_lsf = dsd_sync_hamming_distance(synctest8, M17_LSF, 8);
                int ham_str = dsd_sync_hamming_distance(synctest8, M17_STR, 8);
                int ham_pkt = dsd_sync_hamming_distance(synctest8, M17_PKT, 8);
                int ham_brt = dsd_sync_hamming_distance(synctest8, M17_BRT, 8);

                /* Threshold for sync acceptance (allow 1 bit error in 8 dibits) */
                const int M17_HAM_THRESH = 1;

                /* Determine effective polarity: user override (-xz) takes precedence,
                 * otherwise use auto-detected polarity from preamble, default to normal. */
                int is_inverted = opts->inverted_m17;
                if (!opts->inverted_m17 && state->m17_polarity == 2) {
                    is_inverted = 1; /* Auto-detected inverted from preamble */
                }

                /* Preamble detection - use to auto-set polarity */
                if (ham_pre <= M17_HAM_THRESH) {
                    /* Normal polarity preamble detected */
                    if (state->m17_polarity != 1) {
                        state->m17_polarity = 1; /* Lock to normal */
                    }
                    printFrameSync(opts, state, "+M17 PREAMBLE", synctest_pos + 1, modulation);
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    state->lastsynctype = DSD_SYNC_M17_PRE_POS;
                    /* Warm-start slicer thresholds for improved decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 8);
                    fprintf(stderr, "\n");
                    return DSD_SYNC_M17_PRE_POS;
                } else if (ham_piv <= M17_HAM_THRESH) {
                    /* Inverted polarity preamble detected */
                    if (state->m17_polarity != 2) {
                        state->m17_polarity = 2; /* Lock to inverted */
                    }
                    printFrameSync(opts, state, "-M17 PREAMBLE", synctest_pos + 1, modulation);
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    state->lastsynctype = DSD_SYNC_M17_PRE_NEG;
                    /* Warm-start slicer thresholds for improved decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 8);
                    fprintf(stderr, "\n");
                    return DSD_SYNC_M17_PRE_NEG;
                }

                /* PKT frame detection */
                if (ham_pkt <= M17_HAM_THRESH && !is_inverted) {
                    printFrameSync(opts, state, "+M17 PKT", synctest_pos + 1, modulation);
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    if (state->lastsynctype == DSD_SYNC_M17_PKT_POS || state->lastsynctype == DSD_SYNC_M17_STR_POS) {
                        state->lastsynctype = DSD_SYNC_M17_PKT_POS;
                        /* Warm-start slicer thresholds for improved decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 8);
                        return DSD_SYNC_M17_PKT_POS;
                    }
                    state->lastsynctype = DSD_SYNC_M17_PKT_POS;
                    fprintf(stderr, "\n");
                } else if (ham_brt <= M17_HAM_THRESH && is_inverted) {
                    /* BRT is inverse of PKT */
                    printFrameSync(opts, state, "-M17 PKT", synctest_pos + 1, modulation);
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    if (state->lastsynctype == DSD_SYNC_M17_PKT_NEG || state->lastsynctype == DSD_SYNC_M17_STR_NEG) {
                        state->lastsynctype = DSD_SYNC_M17_PKT_NEG;
                        /* Warm-start slicer thresholds for improved decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 8);
                        return DSD_SYNC_M17_PKT_NEG;
                    }
                    state->lastsynctype = DSD_SYNC_M17_PKT_NEG;
                    fprintf(stderr, "\n");
                }

                /* STR frame detection - note: M17_STR pattern = inverted M17_LSF
	                 * Normal polarity: STR pattern means stream frame
	                 * Inverted polarity: STR pattern means LSF (because it's the inverse) */
                if (ham_str <= M17_HAM_THRESH) {
                    if (!is_inverted) {
                        printFrameSync(opts, state, "+M17 STR", synctest_pos + 1, modulation);
                        state->carrier = 1;
                        state->offset = synctest_pos;
                        state->max = ((state->max) + lmax) / 2;
                        state->min = ((state->min) + lmin) / 2;
                        if (state->lastsynctype == DSD_SYNC_M17_LSF_POS
                            || state->lastsynctype == DSD_SYNC_M17_STR_POS) {
                            state->lastsynctype = DSD_SYNC_M17_STR_POS;
                            /* Warm-start slicer thresholds for improved decode */
                            dsd_sync_warm_start_thresholds_outer_only(opts, state, 8);
                            return DSD_SYNC_M17_STR_POS;
                        }
                        state->lastsynctype = DSD_SYNC_M17_STR_POS;
                        fprintf(stderr, "\n");
                    } else {
                        /* Inverted: STR pattern is actually LSF */
                        printFrameSync(opts, state, "-M17 LSF", synctest_pos + 1, modulation);
                        state->carrier = 1;
                        state->offset = synctest_pos;
                        state->max = ((state->max) + lmax) / 2;
                        state->min = ((state->min) + lmin) / 2;
                        if (state->lastsynctype == DSD_SYNC_M17_PRE_NEG) {
                            state->lastsynctype = DSD_SYNC_M17_LSF_NEG;
                            /* Warm-start slicer thresholds for improved decode */
                            dsd_sync_warm_start_thresholds_outer_only(opts, state, 8);
                            return DSD_SYNC_M17_LSF_NEG;
                        }
                        state->lastsynctype = DSD_SYNC_M17_LSF_NEG;
                        fprintf(stderr, "\n");
                    }
                }

                /* LSF frame detection - note: M17_LSF pattern = inverted M17_STR
	                 * Normal polarity: LSF pattern means link setup frame
	                 * Inverted polarity: LSF pattern means stream frame */
                if (ham_lsf <= M17_HAM_THRESH) {
                    if (!is_inverted) {
                        printFrameSync(opts, state, "+M17 LSF", synctest_pos + 1, modulation);
                        state->carrier = 1;
                        state->offset = synctest_pos;
                        state->max = ((state->max) + lmax) / 2;
                        state->min = ((state->min) + lmin) / 2;
                        if (state->lastsynctype == DSD_SYNC_M17_PRE_POS) {
                            state->lastsynctype = DSD_SYNC_M17_LSF_POS;
                            /* Warm-start slicer thresholds for improved decode */
                            dsd_sync_warm_start_thresholds_outer_only(opts, state, 8);
                            return DSD_SYNC_M17_LSF_POS;
                        }
                        state->lastsynctype = DSD_SYNC_M17_LSF_POS;
                        fprintf(stderr, "\n");
                    } else {
                        /* Inverted: LSF pattern is actually STR */
                        printFrameSync(opts, state, "-M17 STR", synctest_pos + 1, modulation);
                        state->carrier = 1;
                        state->offset = synctest_pos;
                        state->max = ((state->max) + lmax) / 2;
                        state->min = ((state->min) + lmin) / 2;
                        if (state->lastsynctype == DSD_SYNC_M17_LSF_NEG
                            || state->lastsynctype == DSD_SYNC_M17_STR_NEG) {
                            state->lastsynctype = DSD_SYNC_M17_STR_NEG;
                            /* Warm-start slicer thresholds for improved decode */
                            dsd_sync_warm_start_thresholds_outer_only(opts, state, 8);
                            return DSD_SYNC_M17_STR_NEG;
                        }
                        state->lastsynctype = DSD_SYNC_M17_STR_NEG;
                        fprintf(stderr, "\n");
                    }
                }
            }
            //end M17

            //P25 P2 sync S-ISCH VCH
            /* OP25 compatibility: no dibit remapping for P25P2 either */
            strncpy(synctest20, (synctest_p - 19), 20);
            const char* p25p2_sync_window = synctest20;
            if (opts->frame_p25p2 == 1) {
                if (strcmp(p25p2_sync_window, P25P2_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    opts->inverted_p2 = 0;
                    state->lastsynctype = DSD_SYNC_P25P2_POS;
                    if (opts->errorbars == 1) {
                        printFrameSync(opts, state, "+P25p2", synctest_pos + 1, modulation);
                    }
                    if (state->p2_wacn != 0 && state->p2_cc != 0 && state->p2_sysid != 0) {
                        printFrameInfo(opts, state);
                    } else {
                        fprintf(stderr, "%s", KRED);
                        fprintf(stderr, " P2 Missing Parameters            ");
                        fprintf(stderr, "%s", KNRM);
                    }
                    state->last_cc_sync_time = time(NULL);
                    /* CQPSK/QPSK: warm-start only center (DC bias) */
                    if (state->rf_mod == 1) {
                        dsd_sync_warm_start_center_outer_only(opts, state, 20);
                    }
                    return DSD_SYNC_P25P2_POS;
                }
            }
            if (opts->frame_p25p2 == 1) {
                //S-ISCH VCH
                if (strcmp(p25p2_sync_window, INV_P25P2_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    opts->inverted_p2 = 1;
                    if (opts->errorbars == 1) {
                        printFrameSync(opts, state, "-P25p2", synctest_pos + 1, modulation);
                    }
                    if (state->p2_wacn != 0 && state->p2_cc != 0 && state->p2_sysid != 0) {
                        printFrameInfo(opts, state);
                    } else {
                        fprintf(stderr, "%s", KRED);
                        fprintf(stderr, " P2 Missing Parameters            ");
                        fprintf(stderr, "%s", KNRM);
                    }
                    state->lastsynctype = DSD_SYNC_P25P2_NEG;
                    state->last_cc_sync_time = time(NULL);
                    /* CQPSK/QPSK: warm-start only center (DC bias) */
                    if (state->rf_mod == 1) {
                        dsd_sync_warm_start_center_outer_only(opts, state, 20);
                    }
                    return DSD_SYNC_P25P2_NEG;
                }
            }

            //dPMR sync
            strncpy(synctest, (synctest_p - 23), 24);
            strncpy(synctest12, (synctest_p - 11), 12);
            if (opts->frame_dpmr == 1) {
                if (opts->inverted_dpmr == 0) {
                    if (strcmp(synctest, DPMR_FRAME_SYNC_1) == 0) {
                        //fprintf (stderr, "+dPMR FS1\n");
                    }
                    if (strcmp(synctest12, DPMR_FRAME_SYNC_2) == 0) {
                        //fprintf (stderr, "DPMR_FRAME_SYNC_2\n");
                        state->carrier = 1;
                        state->offset = synctest_pos;
                        state->max = ((state->max) + lmax) / 2;
                        state->min = ((state->min) + lmin) / 2;

                        sprintf(state->ftype, "dPMR ");
                        if (opts->errorbars == 1) {
                            printFrameSync(opts, state, "+dPMR ", synctest_pos + 1, modulation);
                        }
                        state->lastsynctype = DSD_SYNC_DPMR_FS2_POS;
                        /* Warm-start slicer thresholds for improved CCH decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 12);
                        return DSD_SYNC_DPMR_FS2_POS;
                    }
                    if (strcmp(synctest12, DPMR_FRAME_SYNC_3) == 0) {
                        //fprintf (stderr, "+dPMR FS3 \n");
                    }
                    if (strcmp(synctest, DPMR_FRAME_SYNC_4) == 0) {
                        //fprintf (stderr, "+dPMR FS4 \n");
                    }
                }
                if (opts->inverted_dpmr == 1) {
                    if (strcmp(synctest, INV_DPMR_FRAME_SYNC_1) == 0) {
                        //fprintf (stderr, "-dPMR FS1 \n");
                    }
                    if (strcmp(synctest12, INV_DPMR_FRAME_SYNC_2) == 0) {
                        //fprintf (stderr, "INV_DPMR_FRAME_SYNC_2\n");
                        state->carrier = 1;
                        state->offset = synctest_pos;
                        state->max = ((state->max) + lmax) / 2;
                        state->min = ((state->min) + lmin) / 2;

                        sprintf(state->ftype, "dPMR ");
                        if (opts->errorbars == 1) {
                            printFrameSync(opts, state, "-dPMR ", synctest_pos + 1, modulation);
                        }

                        state->lastsynctype = DSD_SYNC_DPMR_FS2_NEG;
                        /* Warm-start slicer thresholds for improved CCH decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 12);
                        return DSD_SYNC_DPMR_FS2_NEG;
                    }
                    if (strcmp(synctest12, INV_DPMR_FRAME_SYNC_3) == 0) {
                        //fprintf (stderr, "-dPMR FS3 \n");
                    }
                    if (strcmp(synctest, INV_DPMR_FRAME_SYNC_4) == 0) {
                        //fprintf (stderr, "-dPMR FS4 \n");
                    }
                }
            }

            //New DMR Sync
            if (opts->frame_dmr == 1) {

                if (strcmp(synctest, DMR_MS_DATA_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    //state->directmode = 0;
                    //fprintf (stderr, "DMR MS Data");
                    // DMR uses 4800 symbols/sec; enforce the symbol timing for the current demod sample rate.
                    dmr_set_symbol_timing(opts, state);

                    // Prefer GFSK demod for DMR unless the user locked demod to another mode.
                    if (!opts->mod_cli_lock || opts->mod_gfsk) {
                        state->rf_mod = 2;
                    }
                    if (opts->inverted_dmr == 0) //opts->inverted_dmr
                    {
                        // data frame
                        sprintf(state->ftype, "DMR MS");
                        if (opts->errorbars == 1) {
                            //printFrameSync (opts, state, "+DMR MS Data", synctest_pos + 1, modulation);
                        }
                        if (state->lastsynctype != DSD_SYNC_DMR_MS_DATA) //33
                        {
                            //state->firstframe = 1;
                        }
                        state->lastsynctype = DSD_SYNC_DMR_MS_DATA; //33
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_DATA; //33
                    } else                           //inverted MS voice frame
                    {
                        sprintf(state->ftype, "DMR MS");
                        state->lastsynctype = DSD_SYNC_DMR_MS_VOICE;
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_VOICE;
                    }
                }

                if (strcmp(synctest, DMR_MS_VOICE_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    //state->directmode = 0;
                    //fprintf (stderr, "DMR MS VOICE\n");
                    // DMR uses 4800 symbols/sec; enforce the symbol timing for the current demod sample rate.
                    dmr_set_symbol_timing(opts, state);

                    // Prefer GFSK demod for DMR unless the user locked demod to another mode.
                    if (!opts->mod_cli_lock || opts->mod_gfsk) {
                        state->rf_mod = 2;
                    }
                    if (opts->inverted_dmr == 0) //opts->inverted_dmr
                    {
                        // voice frame
                        sprintf(state->ftype, "DMR MS");
                        if (opts->errorbars == 1) {
                            //printFrameSync (opts, state, "+DMR MS Voice", synctest_pos + 1, modulation);
                        }
                        if (state->lastsynctype != DSD_SYNC_DMR_MS_VOICE) {
                            //state->firstframe = 1;
                        }
                        state->lastsynctype = DSD_SYNC_DMR_MS_VOICE;
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_VOICE;
                    } else //inverted MS data frame
                    {
                        sprintf(state->ftype, "DMR MS");
                        state->lastsynctype = DSD_SYNC_DMR_MS_DATA;
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_DATA;
                    }
                }

                //if ((strcmp (synctest, DMR_MS_DATA_SYNC) == 0) || (strcmp (synctest, DMR_BS_DATA_SYNC) == 0))
                if (strcmp(synctest, DMR_BS_DATA_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + (lmax)) / 2;
                    state->min = ((state->min) + (lmin)) / 2;
                    state->directmode = 0;
                    // DMR uses 4800 symbols/sec; enforce the symbol timing for the current demod sample rate.
                    dmr_set_symbol_timing(opts, state);

                    // Prefer GFSK demod for DMR unless the user locked demod to another mode.
                    if (!opts->mod_cli_lock || opts->mod_gfsk) {
                        state->rf_mod = 2; /* GFSK */
                    }
                    if (opts->inverted_dmr == 0) {
                        // data frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1) {
                            printFrameSync(opts, state, "+DMR ", synctest_pos + 1, modulation);
                        }
                        state->lastsynctype = DSD_SYNC_DMR_BS_DATA_POS;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_BS_DATA_POS;
                    } else {
                        // inverted voice frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1 && opts->dmr_stereo == 0) {
                            //printFrameSync (opts, state, "-DMR ", synctest_pos + 1, modulation);
                        }
                        if (state->lastsynctype != DSD_SYNC_DMR_BS_VOICE_NEG) {
                            state->firstframe = 1;
                        }
                        state->lastsynctype = DSD_SYNC_DMR_BS_VOICE_NEG;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_BS_VOICE_NEG; //11
                    }
                }
                if (strcmp(synctest, DMR_DIRECT_MODE_TS1_DATA_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + (lmax)) / 2;
                    state->min = ((state->min) + (lmin)) / 2;
                    //state->currentslot = 0;
                    state->directmode = 1; //Direct mode
                    // DMR uses 4800 symbols/sec; enforce the symbol timing for the current demod sample rate.
                    dmr_set_symbol_timing(opts, state);

                    // Prefer GFSK demod for DMR unless the user locked demod to another mode.
                    if (!opts->mod_cli_lock || opts->mod_gfsk) {
                        state->rf_mod = 2;
                    }
                    if (opts->inverted_dmr == 0) {
                        // data frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1) {
                            //printFrameSync (opts, state, "+DMR ", synctest_pos + 1, modulation);
                        }
                        state->lastsynctype = DSD_SYNC_DMR_MS_DATA;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_DATA;
                    } else {
                        // inverted voice frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1) {
                            //printFrameSync (opts, state, "-DMR ", synctest_pos + 1, modulation);
                        }
                        if (state->lastsynctype != DSD_SYNC_DMR_BS_VOICE_NEG) {
                            state->firstframe = 1;
                        }
                        state->lastsynctype = DSD_SYNC_DMR_MS_VOICE;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_VOICE;
                    }
                } /* End if(strcmp (synctest, DMR_DIRECT_MODE_TS1_DATA_SYNC) == 0) */
                if (strcmp(synctest, DMR_DIRECT_MODE_TS2_DATA_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + (lmax)) / 2;
                    state->min = ((state->min) + (lmin)) / 2;
                    //state->currentslot = 1;
                    state->directmode = 1; //Direct mode
                    // DMR uses 4800 symbols/sec; enforce the symbol timing for the current demod sample rate.
                    dmr_set_symbol_timing(opts, state);

                    // Prefer GFSK demod for DMR unless the user locked demod to another mode.
                    if (!opts->mod_cli_lock || opts->mod_gfsk) {
                        state->rf_mod = 2;
                    }
                    if (opts->inverted_dmr == 0) {
                        // data frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1) {
                            //printFrameSync (opts, state, "+DMR ", synctest_pos + 1, modulation);
                        }
                        state->lastsynctype = DSD_SYNC_DMR_MS_DATA;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_DATA;
                    } else {
                        // inverted voice frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1 && opts->dmr_stereo == 0) {
                            //printFrameSync (opts, state, "-DMR ", synctest_pos + 1, modulation);
                        }
                        if (state->lastsynctype != DSD_SYNC_DMR_BS_VOICE_NEG) {
                            state->firstframe = 1;
                        }
                        state->lastsynctype = DSD_SYNC_DMR_MS_VOICE;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_VOICE;
                    }
                } /* End if(strcmp (synctest, DMR_DIRECT_MODE_TS2_DATA_SYNC) == 0) */
                //if((strcmp (synctest, DMR_MS_VOICE_SYNC) == 0) || (strcmp (synctest, DMR_BS_VOICE_SYNC) == 0))
                if (strcmp(synctest, DMR_BS_VOICE_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    state->directmode = 0;
                    // DMR uses 4800 symbols/sec; enforce the symbol timing for the current demod sample rate.
                    dmr_set_symbol_timing(opts, state);

                    // Prefer GFSK demod for DMR unless the user locked demod to another mode.
                    if (!opts->mod_cli_lock || opts->mod_gfsk) {
                        state->rf_mod = 2;
                    }
                    if (opts->inverted_dmr == 0) {
                        // voice frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1 && opts->dmr_stereo == 0) {
                            //printFrameSync (opts, state, "+DMR ", synctest_pos + 1, modulation);
                        }
                        if (state->lastsynctype != DSD_SYNC_DMR_BS_VOICE_POS) {
                            state->firstframe = 1;
                        }
                        state->lastsynctype = DSD_SYNC_DMR_BS_VOICE_POS;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_BS_VOICE_POS;
                    }

                    else {
                        // inverted data frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1) //&& opts->dmr_stereo == 0
                        {
                            printFrameSync(opts, state, "-DMR ", synctest_pos + 1, modulation);
                        }
                        state->lastsynctype = DSD_SYNC_DMR_BS_DATA_NEG;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_BS_DATA_NEG;
                    }
                }
                if (strcmp(synctest, DMR_DIRECT_MODE_TS1_VOICE_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    //state->currentslot = 0;
                    state->directmode = 1; //Direct mode
                    // DMR uses 4800 symbols/sec; enforce the symbol timing for the current demod sample rate.
                    dmr_set_symbol_timing(opts, state);

                    // Prefer GFSK demod for DMR unless the user locked demod to another mode.
                    if (!opts->mod_cli_lock || opts->mod_gfsk) {
                        state->rf_mod = 2;
                    }
                    if (opts->inverted_dmr == 0) //&& opts->dmr_stereo == 1
                    {
                        // voice frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1 && opts->dmr_stereo == 0) {
                            //printFrameSync (opts, state, "+DMR ", synctest_pos + 1, modulation);
                        }
                        if (state->lastsynctype != DSD_SYNC_DMR_BS_VOICE_POS) {
                            state->firstframe = 1;
                        }
                        state->lastsynctype = DSD_SYNC_DMR_MS_VOICE;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_VOICE; //treat Direct Mode same as MS mode for now
                    } else {
                        // inverted data frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1 && opts->dmr_stereo == 0) {
                            //printFrameSync (opts, state, "-DMR ", synctest_pos + 1, modulation);
                        }
                        state->lastsynctype = DSD_SYNC_DMR_MS_DATA;
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_DATA;
                    }
                } /* End if(strcmp (synctest, DMR_DIRECT_MODE_TS1_VOICE_SYNC) == 0) */
                if (strcmp(synctest, DMR_DIRECT_MODE_TS2_VOICE_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    // state->currentslot = 1;
                    state->directmode = 1; //Direct mode
                    // DMR uses 4800 symbols/sec; enforce the symbol timing for the current demod sample rate.
                    dmr_set_symbol_timing(opts, state);

                    // Prefer GFSK demod for DMR unless the user locked demod to another mode.
                    if (!opts->mod_cli_lock || opts->mod_gfsk) {
                        state->rf_mod = 2;
                    }
                    if (opts->inverted_dmr == 0) //&& opts->dmr_stereo == 1
                    {
                        // voice frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1 && opts->dmr_stereo == 0) {
                            //printFrameSync (opts, state, "+DMR ", synctest_pos + 1, modulation);
                        }
                        if (state->lastsynctype != DSD_SYNC_DMR_BS_VOICE_POS) {
                            state->firstframe = 1;
                        }
                        state->lastsynctype = DSD_SYNC_DMR_MS_VOICE;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_VOICE;
                    } else {
                        // inverted data frame
                        sprintf(state->ftype, "DMR ");
                        if (opts->errorbars == 1 && opts->dmr_stereo == 0) {
                            //printFrameSync (opts, state, "-DMR ", synctest_pos + 1, modulation);
                        }
                        state->lastsynctype = DSD_SYNC_DMR_MS_DATA;
                        state->last_cc_sync_time = time(NULL);
                        /* Resample-on-sync: calibrate thresholds and re-digitize CACH */
                        dmr_resample_on_sync(opts, state);
                        return DSD_SYNC_DMR_MS_DATA;
                    }
                } //End if(strcmp (synctest, DMR_DIRECT_MODE_TS2_VOICE_SYNC) == 0)
            } //End if (opts->frame_dmr == 1)

            //end DMR Sync

            //ProVoice and EDACS sync
            if (opts->frame_provoice == 1) {
                strncpy(synctest32, (synctest_p - 31), 32);
                strncpy(synctest48, (synctest_p - 47), 48);
                if ((strcmp(synctest32, PROVOICE_SYNC) == 0) || (strcmp(synctest32, PROVOICE_EA_SYNC) == 0)) {
                    state->last_cc_sync_time = now;
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    sprintf(state->ftype, "ProVoice ");
                    if (opts->errorbars == 1) {
                        printFrameSync(opts, state, "+PV   ", synctest_pos + 1, modulation);
                    }
                    state->lastsynctype = DSD_SYNC_PROVOICE_POS;
                    /* Warm-start slicer thresholds for improved decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 32);
                    return DSD_SYNC_PROVOICE_POS;
                } else if ((strcmp(synctest32, INV_PROVOICE_SYNC) == 0)
                           || (strcmp(synctest32, INV_PROVOICE_EA_SYNC) == 0)) {
                    state->last_cc_sync_time = now;
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    sprintf(state->ftype, "ProVoice ");
                    printFrameSync(opts, state, "-PV   ", synctest_pos + 1, modulation);
                    state->lastsynctype = DSD_SYNC_PROVOICE_NEG;
                    /* Warm-start slicer thresholds for improved decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 32);
                    return DSD_SYNC_PROVOICE_NEG;
                } else if (strcmp(synctest48, EDACS_SYNC) == 0) {
                    state->last_cc_sync_time = time(NULL);
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    printFrameSync(opts, state, "-EDACS", synctest_pos + 1, modulation);
                    state->lastsynctype = DSD_SYNC_EDACS_NEG;
                    /* Warm-start slicer thresholds for improved decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 48);
                    return DSD_SYNC_EDACS_NEG;
                } else if (strcmp(synctest48, INV_EDACS_SYNC) == 0) {
                    state->last_cc_sync_time = time(NULL);
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    printFrameSync(opts, state, "+EDACS", synctest_pos + 1, modulation);
                    state->lastsynctype = DSD_SYNC_EDACS_POS;
                    /* Warm-start slicer thresholds for improved decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 48);
                    return DSD_SYNC_EDACS_POS;
                } else if ((strcmp(synctest48, DOTTING_SEQUENCE_A) == 0)
                           || (strcmp(synctest48, DOTTING_SEQUENCE_B) == 0)) {
                    //only print and execute Dotting Sequence if Trunking and Tuned so we don't get multiple prints on this
                    if (opts->p25_trunk == 1 && opts->p25_is_tuned == 1) {
                        printFrameSync(opts, state, " EDACS  DOTTING SEQUENCE: ", synctest_pos + 1, modulation);
                        dsd_frame_sync_hook_eot_cc(opts, state);
                    }
                }

            }

            else if (opts->frame_dstar == 1) {
                if (strcmp(synctest, DSTAR_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    sprintf(state->ftype, "DSTAR ");
                    if (opts->errorbars == 1) {
                        printFrameSync(opts, state, "+DSTAR VOICE ", synctest_pos + 1, modulation);
                    }
                    state->lastsynctype = DSD_SYNC_DSTAR_VOICE_POS;
                    /* Warm-start slicer thresholds for improved decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 24);
                    return DSD_SYNC_DSTAR_VOICE_POS;
                }
                if (strcmp(synctest, INV_DSTAR_SYNC) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    sprintf(state->ftype, "DSTAR ");
                    if (opts->errorbars == 1) {
                        printFrameSync(opts, state, "-DSTAR VOICE ", synctest_pos + 1, modulation);
                    }
                    state->lastsynctype = DSD_SYNC_DSTAR_VOICE_NEG;
                    /* Warm-start slicer thresholds for improved decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 24);
                    return DSD_SYNC_DSTAR_VOICE_NEG;
                }
                if (strcmp(synctest, DSTAR_HD) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    sprintf(state->ftype, "DSTAR_HD ");
                    if (opts->errorbars == 1) {
                        printFrameSync(opts, state, "+DSTAR HEADER", synctest_pos + 1, modulation);
                    }
                    state->lastsynctype = DSD_SYNC_DSTAR_HD_POS;
                    /* Warm-start slicer thresholds for improved decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 24);
                    return DSD_SYNC_DSTAR_HD_POS;
                }
                if (strcmp(synctest, INV_DSTAR_HD) == 0) {
                    state->carrier = 1;
                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    sprintf(state->ftype, " DSTAR_HD");
                    if (opts->errorbars == 1) {
                        printFrameSync(opts, state, "-DSTAR HEADER", synctest_pos + 1, modulation);
                    }
                    state->lastsynctype = DSD_SYNC_DSTAR_HD_NEG;
                    /* Warm-start slicer thresholds for improved decode */
                    dsd_sync_warm_start_thresholds_outer_only(opts, state, 24);
                    return DSD_SYNC_DSTAR_HD_NEG;
                }

            }

            //NXDN
            else if ((opts->frame_nxdn96 == 1) || (opts->frame_nxdn48 == 1)) {
                strncpy(synctest10, (synctest_p - 9), 10); //FSW only
                if ((strcmp(synctest10, "3131331131")
                     == 0) //this seems to be the most common 'correct' pattern on Type-C
                    || (strcmp(synctest10, "3331331131") == 0) //this one hits on new sync but gives a bad lich code
                    || (strcmp(synctest10, "3131331111") == 0) || (strcmp(synctest10, "3331331111") == 0)
                    || (strcmp(synctest10, "3131311131")
                        == 0) //First few FSW on NXDN48 Type-C seems to hit this for some reason

                ) {

                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    if (state->lastsynctype == DSD_SYNC_NXDN_POS) {
                        state->last_cc_sync_time = now;
                        /* Warm-start slicer thresholds for improved LICH decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 10);
                        return DSD_SYNC_NXDN_POS;
                    }
                    state->lastsynctype = DSD_SYNC_NXDN_POS;
                }

                else if (

                    (strcmp(synctest10, "1313113313") == 0) || (strcmp(synctest10, "1113113313") == 0)
                    || (strcmp(synctest10, "1313113333") == 0) || (strcmp(synctest10, "1113113333") == 0)
                    || (strcmp(synctest10, "1313133313") == 0)

                ) {

                    state->offset = synctest_pos;
                    state->max = ((state->max) + lmax) / 2;
                    state->min = ((state->min) + lmin) / 2;
                    if (state->lastsynctype == DSD_SYNC_NXDN_NEG) {
                        state->last_cc_sync_time = now;
                        /* Warm-start slicer thresholds for improved LICH decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 10);
                        return DSD_SYNC_NXDN_NEG;
                    }
                    state->lastsynctype = DSD_SYNC_NXDN_NEG;
                }
            }

//Provoice Conventional -- Some False Positives due to shortened frame sync pattern, so use squelch if possible
#ifdef PVCONVENTIONAL
            if (opts->frame_provoice == 1) {
                memset(synctest32, 0, sizeof(synctest32));
                strncpy(synctest32, (synctest_p - 31), 16); //short sync grab here on 32
                char pvc_txs[9];                            //string (symbol) value of TX Address
                char pvc_rxs[9];                            //string (symbol) value of RX Address
                uint8_t pvc_txa = 0;                        //actual value of TX Address
                uint8_t pvc_rxa = 0;                        //actual value of RX Address
                strncpy(pvc_txs, (synctest_p - 15), 8);     //copy string value of TX Address
                strncpy(pvc_rxs, (synctest_p - 7), 8);      //copy string value of RX Address
                if ((strcmp(synctest32, INV_PROVOICE_CONV_SHORT) == 0)) {
                    if (state->lastsynctype
                        == DSD_SYNC_PROVOICE_NEG) // mitigate false positives due to short sync pattern
                    {
                        state->carrier = 1;
                        state->offset = synctest_pos;
                        state->max = ((state->max) + lmax) / 2;
                        state->min = ((state->min) + lmin) / 2;
                        sprintf(state->ftype, "ProVoice ");
                        // fprintf (stderr, "Sync Pattern = %s ", synctest32);
                        // fprintf (stderr, "TX = %s ", pvc_txs);
                        // fprintf (stderr, "RX = %s ", pvc_rxs);
                        for (int i = 0; i < 8; i++) {
                            pvc_txa = pvc_txa << 1;
                            pvc_rxa = pvc_rxa << 1;
                            //symbol 1 is binary 1 on inverted
                            //I hate working with strings, has to be a better way to evaluate this
                            memset(pvc_txs, 0, sizeof(pvc_txs));
                            memset(pvc_rxs, 0, sizeof(pvc_rxs));
                            strncpy(pvc_txs, (synctest_p - 15 + i), 1);
                            strncpy(pvc_rxs, (synctest_p - 7 + i), 1);
                            if ((strcmp(pvc_txs, "1") == 0)) {
                                pvc_txa = pvc_txa + 1;
                            }
                            if ((strcmp(pvc_rxs, "1") == 0)) {
                                pvc_rxa = pvc_rxa + 1;
                            }
                        }
                        printFrameSync(opts, state, "-PV_C ", synctest_pos + 1, modulation);
                        fprintf(stderr, "TX: %d ", pvc_txa);
                        fprintf(stderr, "RX: %d ", pvc_rxa);
                        if (pvc_txa == 172) {
                            fprintf(stderr, "ALL CALL ");
                        }
                        state->lastsynctype = DSD_SYNC_PROVOICE_NEG;
                        /* Warm-start slicer thresholds for improved decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 16);
                        return DSD_SYNC_PROVOICE_NEG;
                    }
                    state->lastsynctype = DSD_SYNC_PROVOICE_NEG;
                } else if ((strcmp(synctest32, PROVOICE_CONV_SHORT) == 0)) {
                    if (state->lastsynctype
                        == DSD_SYNC_PROVOICE_POS) // mitigate false positives due to short sync pattern
                    {
                        state->carrier = 1;
                        state->offset = synctest_pos;
                        state->max = ((state->max) + lmax) / 2;
                        state->min = ((state->min) + lmin) / 2;
                        sprintf(state->ftype, "ProVoice ");
                        // fprintf (stderr, "Sync Pattern = %s ", synctest32);
                        // fprintf (stderr, "TX = %s ", pvc_txs);
                        // fprintf (stderr, "RX = %s ", pvc_rxs);
                        for (int i = 0; i < 8; i++) {
                            pvc_txa = pvc_txa << 1;
                            pvc_rxa = pvc_rxa << 1;
                            //symbol 3 is binary 1 on positive
                            //I hate working with strings, has to be a better way to evaluate this
                            memset(pvc_txs, 0, sizeof(pvc_txs));
                            memset(pvc_rxs, 0, sizeof(pvc_rxs));
                            strncpy(pvc_txs, (synctest_p - 15 + i), 1);
                            strncpy(pvc_rxs, (synctest_p - 7 + i), 1);
                            if ((strcmp(pvc_txs, "3") == 0)) {
                                pvc_txa = pvc_txa + 1;
                            }
                            if ((strcmp(pvc_rxs, "3") == 0)) {
                                pvc_rxa = pvc_rxa + 1;
                            }
                        }
                        printFrameSync(opts, state, "+PV_C ", synctest_pos + 1, modulation);
                        fprintf(stderr, "TX: %d ", pvc_txa);
                        fprintf(stderr, "RX: %d ", pvc_rxa);
                        if (pvc_txa == 172) {
                            fprintf(stderr, "ALL CALL ");
                        }
                        state->lastsynctype = DSD_SYNC_PROVOICE_POS;
                        /* Warm-start slicer thresholds for improved decode */
                        dsd_sync_warm_start_thresholds_outer_only(opts, state, 16);
                        return DSD_SYNC_PROVOICE_POS;
                    }
                    state->lastsynctype = DSD_SYNC_PROVOICE_POS;
                }
            }
#endif //End Provoice Conventional

        SYNC_TEST_END:; //do nothing

        } // t >= 10

        if (exitflag == 1) {
            cleanupAndExit(opts, state);
            return DSD_SYNC_NONE;
        }

        if (synctest_pos < 10200) {
            synctest_pos++;
            synctest_p++;

        } else {
            // buffer reset
            synctest_pos = 0;
            synctest_p = synctest_buf;
            dsd_frame_sync_hook_no_carrier(opts, state);
        }

        if (state->lastsynctype != DSD_SYNC_P25P1_NEG) {

            if (synctest_pos >= 1800) {
                if ((opts->errorbars == 1) && (opts->verbose > 1) && (state->carrier == 1)) {
                    fprintf(stderr, "Sync: no sync\n");
                    // fprintf (stderr,"Press CTRL + C to close.\n");
                }

                /* Multi-rate SPS hunting: cycle through common symbol rates when no sync found.
                 * Tries 4800/2400/9600/6000 symbols/s (example SPS @48 kHz: 10/20/5/8).
                 * Only cycle if in auto mode and no carrier detected. */
                if (state->carrier == 0 && !opts->mod_cli_lock) {
                    state->sps_hunt_counter++;
                    /* Cycle every ~3 buffer passes (~0.5 seconds at 4800 baud) */
                    if (state->sps_hunt_counter >= 3) {
                        state->sps_hunt_counter = 0;
                        /* Determine which protocols are enabled to decide SPS options */
                        int has_2400 = (opts->frame_nxdn48 == 1 || opts->frame_dpmr == 1);
                        int has_9600 = (opts->frame_provoice == 1);
                        int has_6000 = (opts->frame_p25p2 == 1 || opts->frame_x2tdma == 1);

                        /* Cycle through symbol rates based on enabled protocols */
                        static const int sym_rate_cycle[] = {4800, 2400, 9600, 6000};
                        int next_idx = (state->sps_hunt_idx + 1) % 4;

                        /* Skip rates for protocols not enabled */
                        for (int tries = 0; tries < 4; tries++) {
                            int sym_rate = sym_rate_cycle[next_idx];
                            int skip = 0;
                            if (sym_rate == 2400 && !has_2400) {
                                skip = 1;
                            }
                            if (sym_rate == 9600 && !has_9600) {
                                skip = 1;
                            }
                            if (sym_rate == 6000 && !has_6000) {
                                skip = 1;
                            }
                            if (!skip) {
                                break;
                            }
                            next_idx = (next_idx + 1) % 4;
                        }

                        if (next_idx != state->sps_hunt_idx) {
                            state->sps_hunt_idx = next_idx;
                            /* Compute SPS from actual demodulator output rate when available (RTL path),
                             * otherwise fall back to WAV interpolator scaling relative to 48 kHz. */
                            int demod_rate = 0;
#ifdef USE_RTLSDR
                            if (opts->audio_in_type == AUDIO_IN_RTL && state->rtl_ctx) {
                                demod_rate = (int)dsd_rtl_stream_metrics_hook_output_rate_hz();
                            }
#endif
                            int interp = opts->wav_interpolator > 0 ? opts->wav_interpolator : 1;
                            if (demod_rate <= 0 && opts->wav_decimator > 0) {
                                demod_rate = opts->wav_decimator * interp;
                            }
                            int sym_rate = sym_rate_cycle[next_idx];
                            state->samplesPerSymbol = dsd_opts_compute_sps_rate(opts, sym_rate, demod_rate);
                            state->symbolCenter = dsd_opts_symbol_center(state->samplesPerSymbol);
                            if (opts->verbose > 1) {
                                fprintf(stderr, "SPS hunt: trying %d sps (sym=%d, demod=%d)\n", state->samplesPerSymbol,
                                        sym_rate, demod_rate);
                            }
                        }
                    }
                }
                // Defensive trunking fallback: if tuned to a P25 VC and voice
                // activity is stale beyond hangtime, consider a safe return to
                // the control channel. Mirror the P25 SM tick's gating so we do
                // not thrash back to CC while a slot still indicates ACTIVE.
                if (opts->p25_trunk == 1 && opts->p25_is_tuned == 1) {
                    double nowm = dsd_time_now_monotonic_s();
                    double dt = 1e9;
                    if (state->last_vc_sync_time_m > 0.0) {
                        dt = nowm - state->last_vc_sync_time_m;
                    } else if (state->last_vc_sync_time != 0) {
                        /* Fall back to wall clock if monotonic stamps are absent */
                        dt = (double)(now - state->last_vc_sync_time);
                    }
                    double dt_since_tune = 1e9;
                    if (state->p25_last_vc_tune_time_m > 0.0) {
                        dt_since_tune = nowm - state->p25_last_vc_tune_time_m;
                    } else if (state->p25_last_vc_tune_time != 0) {
                        dt_since_tune = (double)(now - state->p25_last_vc_tune_time);
                    }
                    // Startup grace after a VC tune to avoid bouncing before PTT/audio
                    const dsdneoRuntimeConfig* cfg_hold = dsd_neo_get_config();
                    if (!cfg_hold) {
                        dsd_neo_config_init(opts);
                        cfg_hold = dsd_neo_get_config();
                    }
                    double vc_grace = cfg_hold ? cfg_hold->p25_vc_grace_s : 0.75;
                    int is_p2_vc = (state->p25_p2_active_slot != -1);
                    // Mirror trunk SM gating: treat jitter ring as activity
                    // only when gated by recent MAC_ACTIVE/PTT on that slot;
                    // after hangtime, ignore stale audio_allowed alone.
                    double ring_hold = cfg_hold ? cfg_hold->p25_ring_hold_s : 0.75;
                    double mac_hold = cfg_hold ? cfg_hold->p25_mac_hold_s : 0.75;
                    double l_dmac = (state->p25_p2_last_mac_active_m[0] > 0.0)
                                        ? (nowm - state->p25_p2_last_mac_active_m[0])
                                        : ((state->p25_p2_last_mac_active[0] != 0)
                                               ? ((double)(now - state->p25_p2_last_mac_active[0]))
                                               : 1e9);
                    double r_dmac = (state->p25_p2_last_mac_active_m[1] > 0.0)
                                        ? (nowm - state->p25_p2_last_mac_active_m[1])
                                        : ((state->p25_p2_last_mac_active[1] != 0)
                                               ? ((double)(now - state->p25_p2_last_mac_active[1]))
                                               : 1e9);
                    int l_ring = (state->p25_p2_audio_ring_count[0] > 0) && (l_dmac <= ring_hold);
                    int r_ring = (state->p25_p2_audio_ring_count[1] > 0) && (r_dmac <= ring_hold);
                    int left_has_audio = state->p25_p2_audio_allowed[0] || l_ring;
                    int right_has_audio = state->p25_p2_audio_allowed[1] || r_ring;
                    if (dt >= opts->trunk_hangtime) {
                        left_has_audio = l_ring;
                        right_has_audio = r_ring;
                    }
                    int left_active = left_has_audio || (l_dmac <= mac_hold);
                    int right_active = right_has_audio || (r_dmac <= mac_hold);
                    int both_slots_idle = (!is_p2_vc) ? 1 : !(left_active || right_active);
                    if (dt >= opts->trunk_hangtime && both_slots_idle && dt_since_tune >= vc_grace) {
                        state->p25_sm_force_release = 1;
                        dsd_frame_sync_hook_p25_sm_on_release(opts, state);
                    }
                }
                dsd_frame_sync_hook_no_carrier(opts, state);

                return (-1);
            }
        }
    }

    return (-1);
}
