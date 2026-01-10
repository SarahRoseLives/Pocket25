// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*
 * ncurses_p25_display.c
 * P25 protocol display functions for ncurses UI
 */

#include <dsd-neo/ui/ncurses_p25_display.h>

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/protocol/p25/p25_sm_watchdog.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>
#include <dsd-neo/ui/ncurses_internal.h>
#include <dsd-neo/ui/ui_prims.h>

#include <dsd-neo/platform/curses_compat.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Alias for shared last synctype tracking (defined in ncurses_utils.c) */
#define lls ncurses_last_synctype

int
ui_is_iden_channel(const dsd_state* state, int ch16, long int freq) {
    if (!state || ch16 <= 0 || ch16 >= 65535) {
        return 0;
    }
    // Suppress IDEN classification when not on a P25 system
    int synctype = state->synctype;
    int is_p25p1 = DSD_SYNC_IS_P25P1(synctype);
    int is_p25p2 = DSD_SYNC_IS_P25P2(synctype);
    if (!(is_p25p1 || is_p25p2)) {
        return 0;
    }
    int iden = (ch16 >> 12) & 0xF;
    if (iden < 0 || iden > 15) {
        return 0;
    }
    long base = state->p25_base_freq[iden];
    long spac = state->p25_chan_spac[iden];
    if (base == 0 || spac == 0) {
        return 0;
    }
    static const int slots_per_carrier[16] = {1, 1, 1, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
    int type = state->p25_chan_type[iden] & 0xF;
    int denom = 1;
    if ((state->p25_chan_tdma[iden] & 0x1) != 0) {
        if (type < 0 || type > 15) {
            return 0;
        }
        denom = slots_per_carrier[type];
        if (denom <= 0) {
            return 0;
        }
    } else if (state->p25_cc_is_tdma == 1) {
        denom = 2; // conservative fallback (matches compute path)
    }
    int raw = ch16 & 0xFFF;
    int step = raw / denom;
    long int calc = (base * 5) + (step * spac * 125);
    return (calc == freq) ? 1 : 0;
}

int
ui_match_iden_channel(const dsd_state* state, int ch16, long int freq, int* out_iden) {
    if (!state) {
        return 0;
    }
    if (!ui_is_iden_channel(state, ch16, freq)) {
        return 0;
    }
    int iden = (ch16 >> 12) & 0xF;
    if (out_iden) {
        *out_iden = iden;
    }
    return 1;
}

int
compute_p25p1_voice_avg_err(const dsd_state* s, double* out_avg) {
    int len = s->p25_p1_voice_err_hist_len;
    if (len <= 0) {
        return 0;
    }
    double avg = (double)s->p25_p1_voice_err_hist_sum / (double)len;
    if (out_avg) {
        *out_avg = avg;
    }
    return 1;
}

int
compute_p25p2_voice_avg_err(const dsd_state* s, int slot, double* out_avg) {
    if (slot < 0 || slot > 1) {
        return 0;
    }
    int len = s->p25_p2_voice_err_hist_len;
    if (len <= 0) {
        return 0;
    }
    double avg = (double)s->p25_p2_voice_err_hist_sum[slot] / (double)len;
    if (out_avg) {
        *out_avg = avg;
    }
    return 1;
}

int
ui_print_p25_metrics(const dsd_opts* opts, const dsd_state* state) {
    /* opts used below for filter status */
    if (!state) {
        return 0;
    }
    int lines = 0;
    int is_p25p1 = DSD_SYNC_IS_P25P1(lls);
    int is_p25p2 = DSD_SYNC_IS_P25P2(lls);

    if (is_p25p1 || is_p25p2) {
        /* Current vs previous sync types (helps spot stuck transitions) */
        int cur = lls;
        int prev = state->lastsynctype;
        const char* cur_s = dsd_synctype_to_string(cur);
        const char* prev_s = dsd_synctype_to_string(prev);
        printw("| Sync: cur:%s(%d) prev:%s(%d)\n", cur_s, cur, prev_s, prev);
        lines++;

        /* P25p1 voice error snapshot (IMBE ECC) + moving average */
        double avgv = 0.0;
        if (compute_p25p1_voice_avg_err(state, &avgv)) {
            printw("| P1 Voice: ERR [%X][%X] Avg BER:%4.1f%%\n", state->errs & 0xF, state->errs2 & 0xF, avgv);
        } else {
            printw("| P1: ERR [%X][%X]\n", state->errs & 0xF, state->errs2 & 0xF);
        }
        lines++;

        /* P1 CC FEC/CRC16 health (TSBK/MDPU headers; not voice) */
        unsigned int ok = state->p25_p1_fec_ok;
        unsigned int err = state->p25_p1_fec_err;
        unsigned int tot = ok + err;
        if (tot > 0) {
            double okpct = (100.0 * (double)ok) / (double)tot;
            printw("| P1 CC FEC: %u/%u (ok:%4.1f%%)\n", ok, err, okpct);
            lines++;
        }

        /* P1 voice/header RS health (HDU/LDU/TDULC; not IMBE ECC) */
        if (is_p25p1) {
            unsigned int vok = state->p25_p1_voice_fec_ok;
            unsigned int verr = state->p25_p1_voice_fec_err;
            unsigned int vtot = vok + verr;
            if (vtot > 0) {
                double okpct = (100.0 * (double)vok) / (double)vtot;
                printw("| P1 Voice FEC: %u/%u (ok:%4.1f%%)\n", vok, verr, okpct);
                lines++;
            }
        }

        /* P1 voice header health (HDU/LDU/TDULC protection; accumulates since last reset/retune) */
        if (is_p25p1) {
            unsigned int hdr_fix = state->debug_header_errors;
            unsigned int hdr_crit = state->debug_header_critical_errors;
            if (hdr_fix || hdr_crit) {
                printw("| P1 Hdr: fixed:%u crit:%u\n", hdr_fix, hdr_crit);
                lines++;
            }
        }

        /* P1 voice error distribution (percentiles) */
        if (state->p25_p1_voice_err_hist_len > 0) {
            double p50 = 0.0, p95 = 0.0;
            int n = state->p25_p1_voice_err_hist_len;
            /* Only the first <len> entries are used by the ring */
            if (compute_percentiles_u8(state->p25_p1_voice_err_hist, n, &p50, &p95)) {
                printw("| P1 Voice: P50/P95: %4.1f/%4.1f%%\n", p50, p95);
                lines++;
            }
        }
    }

    if (is_p25p2 || (is_p25p1 && opts && opts->p25_trunk == 1)) {
        /* P25p2 voice average BER (per slot) */
        double avgl = 0.0, avgr = 0.0;
        int hasl = compute_p25p2_voice_avg_err(state, 0, &avgl);
        int hasr = compute_p25p2_voice_avg_err(state, 1, &avgr);
        if (hasl || hasr) {
            if (hasl && hasr) {
                printw("| P2 Voice: Avg BER - S1:%4.1f%%, S2:%4.1f%%\n", avgl, avgr);
            } else if (hasl) {
                printw("| P2 Voice: Avg BER - S1:%4.1f%%\n", avgl);
            } else {
                printw("| P2 Voice: Avg BER - S2:%4.1f%%\n", avgr);
            }
            lines++;
        }

        /* P2 voice percentiles (per slot) */
        if (state->p25_p2_voice_err_hist_len > 0) {
            double l50 = 0.0, l95 = 0.0, r50 = 0.0, r95 = 0.0;
            int n = state->p25_p2_voice_err_hist_len;
            int have_any = 0;
            if (compute_percentiles_u8(state->p25_p2_voice_err_hist[0], n, &l50, &l95)) {
                have_any = 1;
            }
            if (compute_percentiles_u8(state->p25_p2_voice_err_hist[1], n, &r50, &r95)) {
                have_any = 1;
            }
            if (have_any) {
                printw("| P2 Voice: P50/P95 - S1:%4.1f/%4.1f%% S2:%4.1f/%4.1f%%\n", l50, l95, r50, r95);
                lines++;
            }
        }

        /* Condensed P25p2 RS summary line (only if any counters are non-zero) */
        if ((state->p25_p2_rs_facch_ok | state->p25_p2_rs_facch_err | state->p25_p2_rs_sacch_ok
             | state->p25_p2_rs_sacch_err | state->p25_p2_rs_ess_ok | state->p25_p2_rs_ess_err)
            != 0) {
            printw("| P2 RS: FACCH %u/%u SACCH %u/%u ESS %u/%u\n", state->p25_p2_rs_facch_ok,
                   state->p25_p2_rs_facch_err, state->p25_p2_rs_sacch_ok, state->p25_p2_rs_sacch_err,
                   state->p25_p2_rs_ess_ok, state->p25_p2_rs_ess_err);
            lines++;

            /* Average corrections per accepted block (gives quality beyond pass/fail) */
            if (state->p25_p2_rs_facch_ok || state->p25_p2_rs_sacch_ok || state->p25_p2_rs_ess_ok) {
                double fac = 0.0, sac = 0.0, ess = 0.0;
                if (state->p25_p2_rs_facch_ok) {
                    fac = (double)state->p25_p2_rs_facch_corr / (double)state->p25_p2_rs_facch_ok;
                }
                if (state->p25_p2_rs_sacch_ok) {
                    sac = (double)state->p25_p2_rs_sacch_corr / (double)state->p25_p2_rs_sacch_ok;
                }
                if (state->p25_p2_rs_ess_ok) {
                    ess = (double)state->p25_p2_rs_ess_corr / (double)state->p25_p2_rs_ess_ok;
                }
                printw("| P2 RS avg corr: FACCH %4.1f SACCH %4.1f ESS %4.1f\n", fac, sac, ess);
                lines++;
            }
        }
    }

    /* Trunking state-machine counters and IDEN trust summary (trunking only) */
    if (opts && opts->p25_trunk == 1) {
        const dsd_trunk_cc_candidates* cc_candidates = dsd_trunk_cc_candidates_peek(state);
        const unsigned int cc_added = cc_candidates ? cc_candidates->added : 0;
        const unsigned int cc_used = cc_candidates ? cc_candidates->used : 0;
        const int cc_count =
            (cc_candidates && cc_candidates->count > 0 && cc_candidates->count <= DSD_TRUNK_CC_CANDIDATES_MAX)
                ? cc_candidates->count
                : 0;

        /* SM counters + concise mode */
        const char* sm_mode;
        switch (state->p25_sm_mode) {
            case DSD_P25_SM_MODE_ON_CC: sm_mode = "CC"; break;
            case DSD_P25_SM_MODE_ON_VC: sm_mode = "VC"; break;
            case DSD_P25_SM_MODE_HANG: sm_mode = "HANG"; break;
            case DSD_P25_SM_MODE_HUNTING: sm_mode = "HUNT"; break;
            case DSD_P25_SM_MODE_ARMED: sm_mode = "ARM"; break;
            case DSD_P25_SM_MODE_FOLLOW: sm_mode = "FOL"; break;
            case DSD_P25_SM_MODE_RETURNING: sm_mode = "RET"; break;
            default: sm_mode = "?"; break;
        }
        printw("| SM: mode:%s tunes %u rel %u/%u; CC cands add:%u used:%u count:%d\n", sm_mode,
               state->p25_sm_tune_count, state->p25_sm_release_count, state->p25_sm_cc_return_count, cc_added, cc_used,
               cc_count);
        lines++;

        /* CC/VC frequency snapshot (best-effort) */
        long cc = (state->trunk_cc_freq != 0) ? state->trunk_cc_freq : state->p25_cc_freq;
        long vc = ui_guess_active_vc_freq(state);
        char cc_buf[48];
        char vc_buf[48];
        if (cc != 0) {
            snprintf(cc_buf, sizeof cc_buf, "%.6lf MHz", (double)cc / 1000000.0);
        } else {
            snprintf(cc_buf, sizeof cc_buf, "-");
        }
        if (vc != 0) {
            snprintf(vc_buf, sizeof vc_buf, "%.6lf MHz", (double)vc / 1000000.0);
        } else {
            snprintf(vc_buf, sizeof vc_buf, "-");
        }
        printw("| CC/VC: CC:%s VC:%s\n", cc_buf, vc_buf);
        lines++;

        /* Time since last SM release (if any) */
        if (state->p25_sm_last_release_time != 0) {
            time_t now = time(NULL);
            double dt_rel = (double)(now - state->p25_sm_last_release_time);
            printw("| SM Last: release d=%4.1fs\n", dt_rel);
            lines++;
        }

        /* Last SM reason/tag (from SM internal status logs) */
        if (state->p25_sm_last_reason[0] != '\0' && state->p25_sm_last_reason_time != 0) {
            time_t now = time(NULL);
            double dt_tag = (double)(now - state->p25_sm_last_reason_time);
            printw("| SM Last: %s d=%4.1fs\n", state->p25_sm_last_reason, dt_tag);
            lines++;
        }

        /* Recent SM tags (up to 3 most recent) */
        if (state->p25_sm_tag_count > 0) {
            time_t now = time(NULL);
            ui_print_lborder_green();
            addstr(" SM Tags: ");
            int shown = 0;
            for (int k = 0; k < state->p25_sm_tag_count && k < 3; k++) {
                int idx = (state->p25_sm_tag_head - 1 - k) % 8;
                if (idx < 0) {
                    idx += 8;
                }
                const char* t = state->p25_sm_tags[idx];
                double dt = (double)(now - state->p25_sm_tag_time[idx]);
                if (shown > 0) {
                    addstr(" | ");
                }
                printw("%s(%0.1fs)", t[0] ? t : "-", dt);
                shown++;
            }
            addch('\n');
            lines++;
        }

        /* SM Path: compress recent tags into coarse transitions (oldest→newest) */
        if (state->p25_sm_tag_count > 0) {
            char path[64] = {0};
            int n = state->p25_sm_tag_count;
            if (n > 6) {
                n = 6;
            }
            int wrote = 0;
            for (int k = n - 1; k >= 0; k--) {
                int idx = (state->p25_sm_tag_head - 1 - k) % 8;
                if (idx < 0) {
                    idx += 8;
                }
                const char* t = state->p25_sm_tags[idx];
                char sym = 0;
                if (strstr(t, "after-tune") != NULL) {
                    sym = 'V';
                } else if (strstr(t, "after-release") != NULL) {
                    sym = 'R';
                } else if (strstr(t, "release-") != NULL) {
                    sym = 'H'; /* hold/delayed/gated */
                } else if (strstr(t, "after-neigh") != NULL) {
                    sym = 'N';
                } else if (strstr(t, "tick") != NULL) {
                    sym = 'T';
                } else {
                    sym = '?';
                }
                int m = (int)strlen(path);
                if (m + 3 < (int)sizeof(path)) {
                    if (wrote > 0) {
                        path[m++] = '\xE2'; /* UTF-8 right arrow '→' */
                        path[m++] = '\x86';
                        path[m++] = '\x92';
                    }
                    path[m++] = sym;
                    path[m] = '\0';
                    wrote++;
                }
            }
            ui_print_lborder_green();
            addstr(" SM Path: ");
            addstr(path[0] ? path : "-");
            addch('\n');
            lines++;
        }

        /* IDEN trust summary */
        int iden_total = 0, iden_conf = 0;
        for (int i = 0; i < 16; i++) {
            uint8_t t = state->p25_iden_trust[i];
            if (t > 0) {
                iden_total++;
                if (t >= 2) {
                    iden_conf++;
                }
            }
        }
        if (iden_total > 0) {
            printw("| IDENs: %d total (%d confirmed)\n", iden_total, iden_conf);
            lines++;
        }

        /* CC mode hint (TDMA vs FDMA) */
        if (state->p25_cc_freq != 0 || state->trunk_cc_freq != 0) {
            printw("| CC: %s\n", state->p25_cc_is_tdma ? "TDMA" : "FDMA");
            lines++;
        }
    }

    /* P2 slot and jitter ring status (when on a P2 channel) */
    if (is_p25p2) {
        int act = state->p25_p2_active_slot;
        int lfill = state->p25_p2_audio_ring_count[0];
        int rfill = state->p25_p2_audio_ring_count[1];
        if (lfill < 0) {
            lfill = 0;
        }
        if (lfill > 3) {
            lfill = 3;
        }
        if (rfill < 0) {
            rfill = 0;
        }
        if (rfill > 3) {
            rfill = 3;
        }
        printw("| P2 slot: %s; jitter S1:%d/3 S2:%d/3\n", (act == 0) ? "1" : (act == 1) ? "2" : "-", lfill, rfill);
        lines++;

        // SM Gate introspection: show the conditions that can hold release
        // Left/right: audio_allowed, ring_count, delta since last MAC_ACTIVE,
        // and the computed active flags used by the trunk SM tick logic.
        time_t now = time(NULL);
        double l_dmac =
            (state->p25_p2_last_mac_active[0] != 0) ? (double)(now - state->p25_p2_last_mac_active[0]) : -1.0;
        double r_dmac =
            (state->p25_p2_last_mac_active[1] != 0) ? (double)(now - state->p25_p2_last_mac_active[1]) : -1.0;
        double dt = (state->last_vc_sync_time != 0) ? (double)(now - state->last_vc_sync_time) : -1.0;
        double dt_tune = (state->p25_last_vc_tune_time != 0) ? (double)(now - state->p25_last_vc_tune_time) : -1.0;
        // Compute the same per-slot activity booleans as in the SM tick
        double ring_hold = 0.75; // seconds; default
        double mac_hold = 3.0;   // seconds; default
        {
            const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
            if (cfg) {
                ring_hold = cfg->p25_ring_hold_s;
                mac_hold = cfg->p25_mac_hold_s;
            }
        }
        // After hangtime, ignore stale audio_allowed alone; require ring gated by MAC recency
        int l_ring = (state->p25_p2_audio_ring_count[0] > 0) && (l_dmac >= 0.0) && (l_dmac <= ring_hold);
        int r_ring = (state->p25_p2_audio_ring_count[1] > 0) && (r_dmac >= 0.0) && (r_dmac <= ring_hold);
        int l_has = state->p25_p2_audio_allowed[0] || l_ring;
        int r_has = state->p25_p2_audio_allowed[1] || r_ring;
        if (opts && dt >= opts->trunk_hangtime) {
            l_has = l_ring;
            r_has = r_ring;
        }
        int l_act = l_has;
        int r_act = r_has;
        if (l_dmac >= 0.0 && l_dmac <= mac_hold) {
            l_act = 1;
        }
        if (r_dmac >= 0.0 && r_dmac <= mac_hold) {
            r_act = 1;
        }
        printw("| SM Gate: L[a=%d rc=%d dMAC=%4.1fs act=%d]  R[a=%d rc=%d dMAC=%4.1fs act=%d]  dt=%4.1fs tune=%4.1fs\n",
               state->p25_p2_audio_allowed[0] ? 1 : 0, state->p25_p2_audio_ring_count[0], l_dmac, l_act,
               state->p25_p2_audio_allowed[1] ? 1 : 0, state->p25_p2_audio_ring_count[1], r_dmac, r_act, dt, dt_tune);
        lines++;
    }

    /* Additional Phase 1 state-machine diagnostics (timers/flags) */
    if (is_p25p1 && opts && opts->p25_trunk == 1) {
        time_t now = time(NULL);
        double nowm = dsd_time_now_monotonic_s();
        double dt_cc = (state->last_cc_sync_time_m > 0.0)
                           ? (nowm - state->last_cc_sync_time_m)
                           : ((state->last_cc_sync_time != 0) ? (double)(now - state->last_cc_sync_time) : -1.0);
        double dt_vc = (state->last_vc_sync_time_m > 0.0)
                           ? (nowm - state->last_vc_sync_time_m)
                           : ((state->last_vc_sync_time != 0) ? (double)(now - state->last_vc_sync_time) : -1.0);
        double dt_tune =
            (state->p25_last_vc_tune_time_m > 0.0)
                ? (nowm - state->p25_last_vc_tune_time_m)
                : ((state->p25_last_vc_tune_time != 0) ? (double)(now - state->p25_last_vc_tune_time) : -1.0);
        double tdu_age = (state->p25_p1_last_tdu_m > 0.0)
                             ? (nowm - state->p25_p1_last_tdu_m)
                             : ((state->p25_p1_last_tdu != 0) ? (double)(now - state->p25_p1_last_tdu) : -1.0);
        printw("| SM Timers: dCC=%4.1fs dVC=%4.1fs dTune=%4.1fs TDU_age=%4.1fs\n", dt_cc, dt_vc, dt_tune, tdu_age);
        lines++;

        // Show lightweight flags/policy that affect tune/release behavior
        int tuned = (opts->p25_is_tuned == 1 || opts->trunk_is_tuned == 1) ? 1 : 0;
        int tick = p25_sm_in_tick();
        unsigned int hold = state->tg_hold;
        printw("| SM Flags: tuned:%d force_rel:%d tick:%d hold:%s\n", tuned, state->p25_sm_force_release ? 1 : 0, tick,
               (hold != 0) ? "on" : "-");
        lines++;

        // Compact policy summary for quick sanity checks
        const char* pol_data = (opts->trunk_tune_data_calls == 1) ? "on" : "off";
        const char* pol_priv = (opts->trunk_tune_private_calls == 1) ? "on" : "off";
        const char* pol_enc = (opts->trunk_tune_enc_calls == 1) ? "follow" : "lockout";
        printw("| Policy: data:%s priv:%s enc:%s hang:%.1fs\n", pol_data, pol_priv, pol_enc, opts->trunk_hangtime);
        lines++;
    }

    /* P1 DUID histogram (since last reset/tune) */
    unsigned int du_sum = state->p25_p1_duid_hdu + state->p25_p1_duid_ldu1 + state->p25_p1_duid_ldu2
                          + state->p25_p1_duid_tdu + state->p25_p1_duid_tdulc + state->p25_p1_duid_tsbk
                          + state->p25_p1_duid_mpdu;
    if (du_sum > 0) {
        printw("| P1 DUID: HDU %u LDU1 %u LDU2 %u TDU %u TDULC %u TSBK %u MPDU %u\n", state->p25_p1_duid_hdu,
               state->p25_p1_duid_ldu1, state->p25_p1_duid_ldu2, state->p25_p1_duid_tdu, state->p25_p1_duid_tdulc,
               state->p25_p1_duid_tsbk, state->p25_p1_duid_mpdu);
        lines++;
    }

    return lines;
}

void
ui_print_p25_cc_candidates(const dsd_opts* opts, const dsd_state* state) {
    if (!opts || !state) {
        return;
    }
    if (opts->p25_trunk != 1) {
        return;
    }
    const dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_peek(state);
    const int count = (cc && cc->count > 0 && cc->count <= DSD_TRUNK_CC_CANDIDATES_MAX) ? cc->count : 0;
    if (count <= 0) {
        ui_print_lborder_green();
        addstr(" (none)\n");
        return;
    }
    int rows = 0, cols = 80;
    getmaxyx(stdscr, rows, cols);
    if (cols < 1 || rows < 1) {
        cols = 80;
    }
    int shown = 0;
    int line_used = 0;
    for (int i = 0; i < count; i++) {
        long f = cc->candidates[i];
        if (f == 0) {
            continue;
        }
        char buf[64];
        int is_next = (count > 0) ? (i == (cc->idx % count)) : 0;
        int m = snprintf(buf, sizeof buf, "%c%.6lf MHz", is_next ? '>' : ' ', (double)f / 1000000.0);
        if (m < 0) {
            m = 0;
        }
        int sep = (line_used == 0) ? 0 : 4;
        int left_border = (line_used == 0) ? 2 : 0;
        if ((left_border + line_used + sep + m) > cols) {
            if (line_used > 0) {
                addch('\n');
            }
            line_used = 0;
        }
        if (line_used == 0) {
            ui_print_lborder_green();
            addch(' ');
        } else {
            addstr("    ");
        }
        addnstr(buf, m);
        line_used += ((line_used == 0) ? 0 : sep) + m;
        shown++;
    }
    if (shown > 0 && line_used > 0) {
        addch('\n');
    }
}

void
ui_print_p25_neighbors(const dsd_opts* opts, const dsd_state* state) {
    if (!opts || !state) {
        return;
    }
    if (state->p25_nb_count <= 0) {
        ui_print_lborder_green();
        addstr(" (none)\n");
        return;
    }
    // Build index list and sort by last_seen desc (selection sort; small n)
    int idxs[32];
    int n = 0;
    for (int i = 0; i < state->p25_nb_count && i < 32; i++) {
        if (state->p25_nb_freq[i] != 0) {
            idxs[n++] = i;
        }
    }
    for (int i = 0; i < n; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++) {
            if (state->p25_nb_last_seen[idxs[j]] > state->p25_nb_last_seen[idxs[best]]) {
                best = j;
            }
        }
        if (best != i) {
            int tmp = idxs[i];
            idxs[i] = idxs[best];
            idxs[best] = tmp;
        }
    }
    int rows = 0, cols = 80;
    getmaxyx(stdscr, rows, cols);
    if (cols < 1 || rows < 1) {
        cols = 80;
    }
    int shown = 0;
    int line_used = 0;
    time_t now = time(NULL);
    for (int i = 0; i < n && shown < 20; i++) {
        int k = idxs[i];
        long f = state->p25_nb_freq[k];
        long age = (long)((state->p25_nb_last_seen[k] != 0) ? (now - state->p25_nb_last_seen[k]) : 0);
        if (age < 0) {
            age = 0;
        }
        int is_cc = (f == state->p25_cc_freq);
        int in_cands = 0;
        const dsd_trunk_cc_candidates* cc = dsd_trunk_cc_candidates_peek(state);
        const int cand_count = (cc && cc->count > 0 && cc->count <= DSD_TRUNK_CC_CANDIDATES_MAX) ? cc->count : 0;
        for (int c = 0; c < cand_count; c++) {
            if (cc->candidates[c] == f) {
                in_cands = 1;
                break;
            }
        }
        char buf[80];
        int m = snprintf(buf, sizeof buf, "%.6lf MHz%s%s age:%lds", (double)f / 1000000.0, is_cc ? " [CC]" : "",
                         in_cands ? " [C]" : "", age);
        if (m < 0) {
            m = 0;
        }
        int sep = (line_used == 0) ? 0 : 4;
        int left_border = (line_used == 0) ? 2 : 0;
        if ((left_border + line_used + sep + m) > cols) {
            if (line_used > 0) {
                addch('\n');
            }
            line_used = 0;
        }
        if (line_used == 0) {
            ui_print_lborder_green();
            addch(' ');
        } else {
            addstr("    ");
        }
        addnstr(buf, m);
        line_used += ((line_used == 0) ? 0 : sep) + m;
        shown++;
    }
    if (shown > 0 && line_used > 0) {
        addch('\n');
    }
}

void
ui_print_p25_iden_plan(const dsd_opts* opts, const dsd_state* state) {
    UNUSED(opts);
    if (!state) {
        return;
    }
    int any = 0;
    for (int id = 0; id < 16; id++) {
        if (state->p25_base_freq[id] || state->p25_chan_spac[id] || state->p25_iden_trust[id]) {
            any = 1;
            break;
        }
    }
    if (!any) {
        ui_print_lborder_green();
        addstr(" (none)\n");
        return;
    }
    for (int id = 0; id < 16; id++) {
        long base = state->p25_base_freq[id];
        long spac = state->p25_chan_spac[id];
        int type = state->p25_chan_type[id] & 0xF;
        int tdma = (state->p25_chan_tdma[id] & 0x1) ? 1 : 0;
        int trust = state->p25_iden_trust[id];
        if (base == 0 && spac == 0 && trust == 0) {
            continue;
        }
        double base_mhz = (double)(base * 5) / 1000000.0;   // base*5 Hz
        double spac_mhz = (double)(spac * 125) / 1000000.0; // spac*125 Hz
        attr_t saved_attrs = 0;
        short saved_pair = 0;
        attr_get(&saved_attrs, &saved_pair, NULL);
        attron(COLOR_PAIR(ui_iden_color_pair(id)));
        ui_print_lborder_green();
        addch(' ');
        printw("IDEN %d: %s type:%d base:%.6lfMHz spac:%.6lfMHz off:%d trust:%s", id, tdma ? "TDMA" : "FDMA", type,
               base_mhz, spac_mhz, state->p25_trans_off[id],
               (trust >= 2)   ? "ok"
               : (trust == 1) ? "prov"
                              : "-");
        if (state->p25_iden_wacn[id] || state->p25_iden_sysid[id]) {
            printw(" W:%05llX S:%03llX", state->p25_iden_wacn[id], state->p25_iden_sysid[id]);
        }
        if (state->p25_iden_rfss[id] || state->p25_iden_site[id]) {
            printw(" R:%lld I:%lld", state->p25_iden_rfss[id], state->p25_iden_site[id]);
        }
        addch('\n');
        attr_set(saved_attrs, saved_pair, NULL);
    }
}

long int
ui_guess_active_vc_freq(const dsd_state* state) {
    if (!state) {
        return 0;
    }
    // Prefer protocol-agnostic trunk alias when available
    if (state->trunk_vc_freq[0] != 0) {
        return state->trunk_vc_freq[0];
    }
    // Fallback to P25-specific field
    if (state->p25_vc_freq[0] != 0) {
        return state->p25_vc_freq[0];
    }
    // Parse any active channel strings for a channel/LCN and map via trunk_chan_map
    for (int i = 0; i < 31; i++) {
        const char* s = state->active_channel[i];
        if (!s || s[0] == '\0') {
            continue;
        }
        const char* p = strstr(s, "Ch:");
        if (!p) {
            continue;
        }
        p += 3; // skip "Ch:"
        while (*p == ' ') {
            p++;
        }
        // Capture up to 6 hex/dec digits
        char tok[8] = {0};
        int t = 0;
        while (*p && t < 6) {
            char c = *p;
            int is_hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
            if (!is_hex) {
                break;
            }
            tok[t++] = c;
            p++;
        }
        if (t == 0) {
            continue;
        }
        // Try hex channel index first (P25), then decimal (DMR/NXDN)
        char* endp = NULL;
        long ch_hex = strtol(tok, &endp, 16);
        if (endp && *endp == '\0' && ch_hex > 0 && ch_hex < 65535) {
            long int f = state->trunk_chan_map[ch_hex];
            if (f != 0) {
                return f;
            }
        }
        long ch_dec = strtol(tok, &endp, 10);
        if (endp && *endp == '\0' && ch_dec > 0 && ch_dec < 65535) {
            long int f = state->trunk_chan_map[ch_dec];
            if (f != 0) {
                return f;
            }
        }
    }
    return 0;
}
