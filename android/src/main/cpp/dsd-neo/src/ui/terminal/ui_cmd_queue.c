// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* UI → Demod command queue (SPSC, bounded) */

#include <dsd-neo/runtime/telemetry.h>
#include <dsd-neo/ui/ui_async.h>
#include <dsd-neo/ui/ui_cmd.h>
#include <dsd-neo/ui/ui_cmd_dispatch.h>

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/crypto/dmr_keystream.h>
#include <dsd-neo/engine/frame_processing.h>
#include <dsd-neo/io/control.h>
#include <dsd-neo/io/rigctl_client.h>
#include <dsd-neo/io/tcp_input.h>
#include <dsd-neo/io/udp_input.h>
#include <dsd-neo/platform/atomic_compat.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/platform/threading.h>
#include <dsd-neo/protocol/dmr/dmr.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/exitflag.h>
#include <dsd-neo/runtime/freq_parse.h>
#include <dsd-neo/runtime/log.h>
#include <dsd-neo/ui/menu_services.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sndfile.h>

#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>
#include <dsd-neo/ui/ui_dsp_cmd.h>
#endif

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define UI_CMD_Q_CAP 128

_Static_assert(sizeof(dsdneoUserConfig) <= sizeof(((struct UiCmd*)0)->data),
               "UiCmd payload too small for dsdneoUserConfig");

static struct UiCmd g_q[UI_CMD_Q_CAP];
static size_t g_head = 0; // pop index
static size_t g_tail = 0; // push index
static dsd_mutex_t g_mu;
static atomic_int g_mu_init = 0;
static atomic_int g_overflow = 0;
static atomic_int g_overflow_warn_gate = 0;

static void
ensure_mu_init(void) {
    int expected = 0;
    if (atomic_compare_exchange_strong(&g_mu_init, &expected, 1)) {
        dsd_mutex_init(&g_mu);
    }
}

// Dispatch commands via per-domain registries
static int
ui_cmd_dispatch(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    const struct UiCmdReg* regs[] = {ui_actions_audio, ui_actions_radio, ui_actions_trunk, ui_actions_logging, NULL};
    for (int i = 0; regs[i] != NULL; ++i) {
        for (const struct UiCmdReg* r = regs[i]; r && r->fn != NULL; ++r) {
            if (r->id == c->id) {
                return r->fn(opts, state, c);
            }
        }
    }
    return 0; // not handled
}

static inline int
q_is_full_unlocked(void) {
    return ((g_tail + 1) % UI_CMD_Q_CAP) == g_head;
}

static inline int
q_is_empty_unlocked(void) {
    return g_head == g_tail;
}

int
ui_post_cmd(int cmd_id, const void* payload, size_t payload_sz) {
    if (payload_sz > sizeof(g_q[0].data)) {
        payload_sz = sizeof(g_q[0].data);
    }
    ensure_mu_init();
    dsd_mutex_lock(&g_mu);
    if (q_is_full_unlocked()) {
        // Drop the oldest command (advance head) and warn once per burst
        g_head = (g_head + 1) % UI_CMD_Q_CAP;
        atomic_fetch_add(&g_overflow, 1);
        if (atomic_exchange(&g_overflow_warn_gate, 1) == 0) {
            LOG_WARNING("ui_cmd_queue: overflow; dropping oldest command(s).\n");
        }
    }
    struct UiCmd* c = &g_q[g_tail];
    c->id = cmd_id;
    c->n = payload_sz;
    if (payload_sz && payload) {
        memcpy(c->data, payload, payload_sz);
    }
    g_tail = (g_tail + 1) % UI_CMD_Q_CAP;
    dsd_mutex_unlock(&g_mu);
    return 0;
}

static void
apply_cmd(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    // Try dispatch table first; fall back to legacy switch.
    if (ui_cmd_dispatch(opts, state, c)) {
        return;
    }
    switch (c->id) {
        case UI_CMD_QUIT: {
            exitflag = 1;
            break;
        }
        case UI_CMD_FORCE_PRIV_TOGGLE: {
            if (state->M == 1 || state->M == 0x21) {
                state->M = 0;
            } else {
                state->M = 1;
            }
            break;
        }
        case UI_CMD_FORCE_RC4_TOGGLE: {
            if (state->M == 1 || state->M == 0x21) {
                state->M = 0;
            } else {
                state->M = 0x21;
            }
            break;
        }

        case UI_CMD_TOGGLE_COMPACT: {
            opts->ncurses_compact = opts->ncurses_compact ? 0 : 1;
            break;
        }
        case UI_CMD_HISTORY_CYCLE: {
            opts->ncurses_history++;
            opts->ncurses_history %= 3;
            break;
        }
        case UI_CMD_SLOT1_TOGGLE: {
            if (opts->slot1_on == 1) {
                opts->slot1_on = 0;
                if (opts->slot_preference == 0) {
                    opts->slot_preference = 2;
                }
                state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
                state->audio_out_buf_p = state->audio_out_buf + 100;
                memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
                memset(state->audio_out_buf, 0, 100 * sizeof(short));
                state->audio_out_idx2 = 0;
                state->audio_out_idx = 0;
            } else {
                opts->slot1_on = 1;
            }
            break;
        }
        case UI_CMD_SLOT2_TOGGLE: {
            if (opts->slot2_on == 1) {
                opts->slot2_on = 0;
                opts->slot_preference = 0;
                state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
                state->audio_out_buf_pR = state->audio_out_bufR + 100;
                memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
                memset(state->audio_out_bufR, 0, 100 * sizeof(short));
                state->audio_out_idx2R = 0;
                state->audio_out_idxR = 0;
            } else {
                opts->slot2_on = 1;
            }
            break;
        }
        case UI_CMD_SLOT_PREF_CYCLE: {
            if (opts->slot_preference == 0 || opts->slot_preference == 1) {
                opts->slot_preference++;
            } else {
                opts->slot_preference = 0;
            }
            break;
        }

        case UI_CMD_PAYLOAD_TOGGLE: {
            opts->payload = opts->payload ? 0 : 1;
            break;
        }
        case UI_CMD_P25_GA_TOGGLE: {
            opts->show_p25_group_affiliations = opts->show_p25_group_affiliations ? 0 : 1;
            if (state) {
                snprintf(state->ui_msg, sizeof state->ui_msg, "P25 Group Affiliation: %s",
                         opts->show_p25_group_affiliations ? "On" : "Off");
                state->ui_msg_expire = time(NULL) + 3;
            }
            break;
        }

        case UI_CMD_LPF_TOGGLE: opts->use_lpf = opts->use_lpf ? 0 : 1; break;
        case UI_CMD_HPF_TOGGLE: opts->use_hpf = opts->use_hpf ? 0 : 1; break;
        case UI_CMD_PBF_TOGGLE: opts->use_pbf = opts->use_pbf ? 0 : 1; break;
        case UI_CMD_HPF_D_TOGGLE: opts->use_hpf_d = opts->use_hpf_d ? 0 : 1; break;
        case UI_CMD_AGGR_SYNC_TOGGLE: opts->aggressive_framesync = opts->aggressive_framesync ? 0 : 1; break;
        case UI_CMD_CALL_ALERT_TOGGLE: opts->call_alert = opts->call_alert ? 0 : 1; break;

        case UI_CMD_CONST_TOGGLE: {
            if (opts->audio_in_type == AUDIO_IN_RTL) {
                opts->constellation = opts->constellation ? 0 : 1;
            }
            break;
        }
        case UI_CMD_CONST_NORM_TOGGLE: {
            if (opts->audio_in_type == AUDIO_IN_RTL && opts->constellation == 1) {
                opts->const_norm_mode = (opts->const_norm_mode == 0) ? 1 : 0;
            }
            break;
        }
        case UI_CMD_CONST_GATE_DELTA: {
            if (opts->audio_in_type == AUDIO_IN_RTL && opts->constellation == 1) {
                float d = 0.0f;
                if (c->n >= (int)sizeof(float)) {
                    memcpy(&d, c->data, sizeof(float));
                }
                float* g = (opts->mod_qpsk == 1) ? &opts->const_gate_qpsk : &opts->const_gate_other;
                *g += d;
                if (*g < 0.0f) {
                    *g = 0.0f;
                }
                if (*g > 0.90f) {
                    *g = 0.90f;
                }
            }
            break;
        }
        case UI_CMD_EYE_TOGGLE: {
            if (opts->audio_in_type == AUDIO_IN_RTL) {
                opts->eye_view = opts->eye_view ? 0 : 1;
            }
            break;
        }
        case UI_CMD_EYE_UNICODE_TOGGLE: {
            if (opts->audio_in_type == AUDIO_IN_RTL && opts->eye_view == 1) {
                opts->eye_unicode = opts->eye_unicode ? 0 : 1;
            }
            break;
        }
        case UI_CMD_EYE_COLOR_TOGGLE: {
            if (opts->audio_in_type == AUDIO_IN_RTL && opts->eye_view == 1) {
                opts->eye_color = opts->eye_color ? 0 : 1;
            }
            break;
        }
        case UI_CMD_FSK_HIST_TOGGLE: {
            if (opts->audio_in_type == AUDIO_IN_RTL) {
                opts->fsk_hist_view = opts->fsk_hist_view ? 0 : 1;
            }
            break;
        }
        case UI_CMD_SPECTRUM_TOGGLE: {
            if (opts->audio_in_type == AUDIO_IN_RTL) {
                opts->spectrum_view = opts->spectrum_view ? 0 : 1;
            }
            break;
        }
        case UI_CMD_SPEC_SIZE_DELTA: {
            int32_t d = 0;
            if (c->n >= (int)sizeof(int32_t)) {
                memcpy(&d, c->data, sizeof(int32_t));
            }
            if (opts->audio_in_type == AUDIO_IN_RTL && opts->spectrum_view == 1) {
#ifdef USE_RTLSDR
                int n = rtl_stream_spectrum_get_size();
                int want = n + d;
                if (want < 64) {
                    want = 64;
                }
                if (want > 1024) {
                    want = 1024;
                }
                if (want != n) {
                    (void)rtl_stream_spectrum_set_size(want);
                }
#else
                (void)d;
#endif
            }
            break;
        }

        case UI_CMD_DMR_RESET: {
            // Reset site params/strings and NXDN fields similar to handler
            state->dmr_rest_channel = -1;
            state->dmr_mfid = -1;
            snprintf(state->dmr_branding_sub, sizeof state->dmr_branding_sub, "%s", "");
            snprintf(state->dmr_branding, sizeof state->dmr_branding, "%s", "");
            snprintf(state->dmr_site_parms, sizeof state->dmr_site_parms, "%s", "");
            opts->dmr_dmrla_is_set = 0;
            opts->dmr_dmrla_n = 0;
            state->nxdn_location_site_code = 0;
            state->nxdn_location_sys_code = 0;
            snprintf(state->nxdn_location_category, sizeof state->nxdn_location_category, "%s", " ");
            state->nxdn_last_ran = -1;
            state->nxdn_ran = 0;
            state->nxdn_rcn = 0;
            state->nxdn_base_freq = 0;
            state->nxdn_step = 0;
            state->nxdn_bw = 0;
            break;
        }
        case UI_CMD_TCP_CONNECT_AUDIO: {
            // Attempt TCP audio connect (cross-platform)
            opts->tcp_sockfd = Connect(opts->tcp_hostname, opts->tcp_portno);
            if (opts->tcp_sockfd != 0) {
                // reset audio input stream
                if (opts->audio_in_type == AUDIO_IN_PULSE) {
                    closeAudioInput(opts);
                }
                // Close existing TCP context if any
                if (opts->tcp_in_ctx) {
                    tcp_input_close(opts->tcp_in_ctx);
                    opts->tcp_in_ctx = NULL;
                }
                opts->tcp_in_ctx = tcp_input_open(opts->tcp_sockfd, opts->wav_sample_rate);
                if (opts->tcp_in_ctx != NULL) {
                    LOG_INFO("TCP Socket Connected Successfully.\n");
                    opts->audio_in_type = AUDIO_IN_TCP; // TCP PCM16LE
                } else {
                    LOG_ERROR("Error, couldn't open TCP audio input\n");
                    dsd_socket_close(opts->tcp_sockfd);
                    opts->tcp_sockfd = 0;
                }
            } else {
                LOG_ERROR("TCP Socket Connection Error.\n");
            }
            break;
        }
        case UI_CMD_RIGCTL_CONNECT: {
            // Use same/last specified host for TCP audio for rigctl connect
            memcpy(opts->rigctlhostname, opts->tcp_hostname, sizeof(opts->rigctlhostname));
            opts->rigctl_sockfd = Connect(opts->rigctlhostname, opts->rigctlportno);
            opts->use_rigctl = (opts->rigctl_sockfd != 0) ? 1 : 0;
            break;
        }
        case UI_CMD_RETURN_CC: {
            if (opts->p25_trunk == 1 && (state->trunk_cc_freq != 0 || state->p25_cc_freq != 0)) {
                // extra safeguards due to sync issues with NXDN
                memset(state->nxdn_sacch_frame_segment, 1, sizeof(state->nxdn_sacch_frame_segment));
                memset(state->nxdn_sacch_frame_segcrc, 1, sizeof(state->nxdn_sacch_frame_segcrc));
                memset(state->active_channel, 0, sizeof(state->active_channel));
                dmr_reset_blocks(opts, state);
                state->lasttg = state->lasttgR = 0;
                state->lastsrc = state->lastsrcR = 0;
                state->payload_algid = state->payload_algidR = 0;
                state->payload_keyid = state->payload_keyidR = 0;
                state->payload_mi = state->payload_miR = state->payload_miP = state->payload_miN = 0;
                opts->p25_is_tuned = 0;
                opts->trunk_is_tuned = 0;
                state->p25_vc_freq[0] = state->p25_vc_freq[1] = 0;
                state->trunk_vc_freq[0] = state->trunk_vc_freq[1] = 0;
                {
                    long f = (state->trunk_cc_freq != 0) ? state->trunk_cc_freq : state->p25_cc_freq;
                    io_control_set_freq(opts, state, f);
                }
                state->last_cc_sync_time = time(NULL);
                state->last_cc_sync_time_m = dsd_time_now_monotonic_s();
                // Set symbol timing dynamically based on CC type and actual demod rate
                int sym_rate = (state->p25_cc_is_tdma == 1) ? 6000 : 4800;
                int demod_rate = 0;
#ifdef USE_RTLSDR
                if (opts->audio_in_type == AUDIO_IN_RTL && state->rtl_ctx) {
                    demod_rate = (int)rtl_stream_output_rate(state->rtl_ctx);
                }
#endif
                state->samplesPerSymbol = dsd_opts_compute_sps_rate(opts, sym_rate, demod_rate);
                state->symbolCenter = dsd_opts_symbol_center(state->samplesPerSymbol);
                LOG_INFO("User Activated Return to CC\n");
            }
            break;
        }
        case UI_CMD_SIM_NOCAR: {
            state->last_cc_sync_time = 0;
            state->last_vc_sync_time = 0;
            state->last_vc_sync_time_m = 0.0;
            noCarrier(opts, state);
            break;
        }

        case UI_CMD_LOCKOUT_SLOT: {
            uint8_t slot = 0;
            if (c->n >= 1) {
                memcpy(&slot, c->data, 1);
            }
            if (opts->frame_provoice == 1) {
                break; // not supported in ProVoice
            }
            int tg = (slot == 0) ? state->lasttg : state->lasttgR;
            if (tg == 0) {
                break;
            }
            // Append to group list as LOCKOUT
            state->group_array[state->group_tally].groupNumber = tg;
            snprintf(state->group_array[state->group_tally].groupMode,
                     sizeof state->group_array[state->group_tally].groupMode, "%s", "B");
            snprintf(state->group_array[state->group_tally].groupName,
                     sizeof state->group_array[state->group_tally].groupName, "%s", "LOCKOUT");
            state->group_tally++;

            // Event echo
            int eh_slot = (slot == 0) ? 0 : 1;
            snprintf(state->event_history_s[eh_slot].Event_History_Items[0].internal_str,
                     sizeof state->event_history_s[eh_slot].Event_History_Items[0].internal_str,
                     "Target: %d; has been locked out; User Lock Out.", tg);
            watchdog_event_current(opts, state, eh_slot);
            snprintf(state->call_string[eh_slot], sizeof state->call_string[eh_slot], "%s",
                     "                     "); // 21 spaces

            // Persist to group file if available
            if (opts->group_in_file[0] != 0) {
                FILE* pFile = fopen(opts->group_in_file, "a");
                if (pFile != NULL) {
                    int alg = (slot == 0) ? state->payload_algid : state->payload_algidR;
                    fprintf(pFile, "%d,B,LOCKOUT,%02X\n", tg, alg);
                    fclose(pFile);
                }
            }

            // Extra safeguards and resets
            memset(state->nxdn_sacch_frame_segment, 1, sizeof(state->nxdn_sacch_frame_segment));
            memset(state->nxdn_sacch_frame_segcrc, 1, sizeof(state->nxdn_sacch_frame_segcrc));
            memset(state->active_channel, 0, sizeof(state->active_channel));
            dmr_reset_blocks(opts, state);
            state->lasttg = 0;
            state->lasttgR = 0;
            state->lastsrc = 0;
            state->lastsrcR = 0;
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

            // Retune to CC via io/control API
            if (opts->p25_trunk == 1) {
                noCarrier(opts, state);
                long f = (state->trunk_cc_freq != 0) ? state->trunk_cc_freq : state->p25_cc_freq;
                io_control_set_freq(opts, state, f);
                state->trunk_cc_freq = f;
            }
            state->last_cc_sync_time = time(NULL);
            // Set symbol timing dynamically based on CC type and actual demod rate
            if (state->p25_cc_is_tdma == 0) {
                int demod_rate_tune = 0;
#ifdef USE_RTLSDR
                if (opts->audio_in_type == AUDIO_IN_RTL && state->rtl_ctx) {
                    demod_rate_tune = (int)rtl_stream_output_rate(state->rtl_ctx);
                }
#endif
                state->samplesPerSymbol = dsd_opts_compute_sps_rate(opts, 4800, demod_rate_tune);
                state->symbolCenter = dsd_opts_symbol_center(state->samplesPerSymbol);
            }
            break;
        }

        case UI_CMD_M17_TX_TOGGLE: {
            if (opts->m17encoder == 1) {
                state->m17encoder_tx = (state->m17encoder_tx == 0) ? 1 : 0;
                if (state->m17encoder_tx == 0) {
                    state->m17encoder_eot = 1;
                }
            }
            break;
        }

        case UI_CMD_PROVOICE_ESK_TOGGLE: {
            if (opts->frame_provoice == 1) {
                state->esk_mask = (state->esk_mask == 0) ? 0xA0 : 0;
            }
            break;
        }

        case UI_CMD_PROVOICE_MODE_TOGGLE: {
            if (opts->frame_provoice == 1) {
                state->ea_mode = (state->ea_mode == 0) ? 1 : 0;
                // reset critical state similar to legacy handler
                state->edacs_site_id = 0;
                state->edacs_lcn_count = 0;
                state->edacs_cc_lcn = 0;
                state->edacs_vc_lcn = 0;
                state->edacs_tuned_lcn = -1;
                state->edacs_vc_call_type = 0;
                state->p25_cc_freq = 0;
                state->trunk_cc_freq = 0;
                opts->p25_is_tuned = 0;
                state->lasttg = 0;
                state->lastsrc = 0;
            }
            break;
        }
        case UI_CMD_CHANNEL_CYCLE: {
            if (opts->use_rigctl == 1 || opts->audio_in_type == AUDIO_IN_RTL) {
                memset(state->nxdn_sacch_frame_segment, 1, sizeof(state->nxdn_sacch_frame_segment));
                memset(state->nxdn_sacch_frame_segcrc, 1, sizeof(state->nxdn_sacch_frame_segcrc));
                memset(state->active_channel, 0, sizeof(state->active_channel));
                dmr_reset_blocks(opts, state);
                state->lasttg = state->lasttgR = 0;
                state->lastsrc = state->lastsrcR = 0;
                state->payload_algid = state->payload_algidR = 0;
                state->payload_keyid = state->payload_keyidR = 0;
                state->payload_mi = state->payload_miR = state->payload_miP = state->payload_miN = 0;
                opts->p25_is_tuned = 0;
                opts->trunk_is_tuned = 0;
                state->p25_vc_freq[0] = state->p25_vc_freq[1] = 0;
                if (opts->p25_prefer_candidates == 1) {
                    long cand = 0;
                    if (p25_sm_next_cc_candidate(state, &cand)) {
                        io_control_set_freq(opts, state, cand);
                        LOG_INFO("Candidate Cycle: tuning to %.06lf MHz\n", (double)cand / 1000000);
                        state->last_cc_sync_time = time(NULL);
                        state->last_cc_sync_time_m = dsd_time_now_monotonic_s();
                        break;
                    }
                }
                if (state->lcn_freq_roll >= state->lcn_freq_count) {
                    state->lcn_freq_roll = 0;
                }
                if (state->lcn_freq_roll != 0) {
                    if (state->trunk_lcn_freq[state->lcn_freq_roll - 1]
                        == state->trunk_lcn_freq[state->lcn_freq_roll]) {
                        state->lcn_freq_roll++;
                        if (state->lcn_freq_roll >= state->lcn_freq_count) {
                            state->lcn_freq_roll = 0;
                        }
                    }
                }
                if (state->trunk_lcn_freq[state->lcn_freq_roll] != 0) {
                    io_control_set_freq(opts, state, state->trunk_lcn_freq[state->lcn_freq_roll]);
                    LOG_INFO("Channel Cycle: tuning to %.06lf MHz\n",
                             (double)state->trunk_lcn_freq[state->lcn_freq_roll] / 1000000);
                }
                state->lcn_freq_roll++;
                state->last_cc_sync_time = time(NULL);
                state->last_cc_sync_time_m = dsd_time_now_monotonic_s();
                // Set symbol timing dynamically based on CC type and actual demod rate
                int sym_rate_roll = (state->p25_cc_is_tdma == 1) ? 6000 : 4800;
                int demod_rate_roll = 0;
#ifdef USE_RTLSDR
                if (opts->audio_in_type == AUDIO_IN_RTL && state->rtl_ctx) {
                    demod_rate_roll = (int)rtl_stream_output_rate(state->rtl_ctx);
                }
#endif
                state->samplesPerSymbol = dsd_opts_compute_sps_rate(opts, sym_rate_roll, demod_rate_roll);
                state->symbolCenter = dsd_opts_symbol_center(state->samplesPerSymbol);
            }
            break;
        }
        case UI_CMD_SYMCAP_SAVE: {
            char timestr[7];
            char datestr[9];
            getTime_buf(timestr);
            getDate_buf(datestr);
            snprintf(opts->symbol_out_file, sizeof opts->symbol_out_file, "%s_%s_dibit_capture.bin", datestr, timestr);
            openSymbolOutFile(opts, state);
            if (state && state->event_history_s) {
                state->event_history_s[0].Event_History_Items[0].color_pair = 4;
                char event_str[2000] = {0};
                snprintf(event_str, sizeof event_str, "DSD-neo Dibit Capture File Started: %s;", opts->symbol_out_file);
                watchdog_event_datacall(opts, state, 0xFFFFFF, 0xFFFFFF, event_str, 0);
                state->lastsrc = 0;
                watchdog_event_history(opts, state, 0);
                watchdog_event_current(opts, state, 0);
            }
            opts->symbol_out_file_creation_time = time(NULL);
            opts->symbol_out_file_is_auto = 1;
            break;
        }
        case UI_CMD_SYMCAP_STOP: {
            if (opts->symbol_out_f) {
                closeSymbolOutFile(opts, state);
                snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", opts->symbol_out_file);
                if (state && state->event_history_s) {
                    state->event_history_s[0].Event_History_Items[0].color_pair = 4;
                    char event_str[2000] = {0};
                    snprintf(event_str, sizeof event_str, "DSD-neo Dibit Capture File  Closed: %s;",
                             opts->symbol_out_file);
                    watchdog_event_datacall(opts, state, 0xFFFFFF, 0xFFFFFF, event_str, 0);
                    state->lastsrc = 0;
                    watchdog_event_history(opts, state, 0);
                    watchdog_event_current(opts, state, 0);
                }
            }
            opts->symbol_out_file_is_auto = 0;
            break;
        }
        case UI_CMD_REPLAY_LAST: {
            struct stat sb;
            if (stat(opts->audio_in_dev, &sb) != 0) {
                LOG_ERROR("Error, couldn't open %s\n", opts->audio_in_dev);
                break;
            }
            if (S_ISREG(sb.st_mode)) {
                opts->symbolfile = fopen(opts->audio_in_dev, "r");
                if (opts->symbolfile) {
                    opts->audio_in_type = AUDIO_IN_SYMBOL_BIN;
                }
            }
            break;
        }
        case UI_CMD_WAV_START: {
            char wav_file_directory[1024] = {0};
            snprintf(wav_file_directory, sizeof wav_file_directory, "%s", opts->wav_out_dir);
            struct stat st;
            if (stat(wav_file_directory, &st) == -1) {
                LOG_NOTICE("%s wav file directory does not exist\n", wav_file_directory);
                LOG_NOTICE("Creating directory %s to save decoded wav files\n", wav_file_directory);
                dsd_mkdir(wav_file_directory, 0700);
            }
            LOG_NOTICE("Per Call Wav File Enabled to Directory: %s\n", opts->wav_out_dir);
            srand((unsigned)time(NULL));
            opts->wav_out_f = open_wav_file(opts->wav_out_dir, opts->wav_out_file, 8000, 0);
            opts->wav_out_fR = open_wav_file(opts->wav_out_dir, opts->wav_out_fileR, 8000, 0);
            opts->dmr_stereo_wav = 1;
            break;
        }
        case UI_CMD_WAV_STOP: {
            opts->wav_out_f = close_and_rename_wav_file(opts->wav_out_f, opts->wav_out_file, opts->wav_out_dir,
                                                        state ? &state->event_history_s[0] : NULL);
            opts->wav_out_fR = close_and_rename_wav_file(opts->wav_out_fR, opts->wav_out_fileR, opts->wav_out_dir,
                                                         state ? &state->event_history_s[1] : NULL);
            opts->wav_out_file[0] = 0;
            opts->wav_out_fileR[0] = 0;
            opts->dmr_stereo_wav = 0;
            break;
        }
        case UI_CMD_STOP_PLAYBACK: {
            if (opts->symbolfile != NULL) {
                if (opts->audio_in_type == AUDIO_IN_SYMBOL_BIN) {
                    fclose(opts->symbolfile);
                }
                opts->symbolfile = NULL;
            }
            if (opts->audio_in_type == AUDIO_IN_WAV && opts->audio_in_file) {
                sf_close(opts->audio_in_file);
                opts->audio_in_file = NULL;
            }
            if (opts->audio_out_type == 0) {
                opts->audio_in_type = AUDIO_IN_PULSE;
                if (openAudioInput(opts) != 0) {
                    LOG_ERROR("UI: failed to open PulseAudio input\n");
                }
            } else {
                opts->audio_in_type = AUDIO_IN_STDIN;
            }
            break;
        }

        case UI_CMD_CRC_RELAX_TOGGLE: {
            if (opts) {
                svc_toggle_crc_relax(opts);
            }
            break;
        }
        case UI_CMD_LCW_RETUNE_TOGGLE: {
            if (opts) {
                svc_toggle_lcw_retune(opts);
            }
            break;
        }
        case UI_CMD_P25_CC_CAND_TOGGLE: {
            if (opts) {
                opts->p25_prefer_candidates = opts->p25_prefer_candidates ? 0 : 1;
            }
            break;
        }
        case UI_CMD_REVERSE_MUTE_TOGGLE: {
            if (opts) {
                svc_toggle_reverse_mute(opts);
            }
            break;
        }
        case UI_CMD_CONFIG_APPLY: {
            if (opts && state && c->n >= sizeof(dsdneoUserConfig)) {
                dsdneoUserConfig cfg;
                char old_audio_in_dev[sizeof opts->audio_in_dev];
                char old_audio_out_dev[sizeof opts->audio_out_dev];
                int old_audio_in_type = opts->audio_in_type;
                int old_audio_out_type = opts->audio_out_type;

                snprintf(old_audio_in_dev, sizeof old_audio_in_dev, "%s", opts->audio_in_dev);
                snprintf(old_audio_out_dev, sizeof old_audio_out_dev, "%s", opts->audio_out_dev);

                memcpy(&cfg, c->data, sizeof cfg);
                dsd_apply_user_config_to_opts(&cfg, opts, state);

                /* Tighten runtime behavior when applying configs mid-run by
                 * restarting or retuning backends that are already active and
                 * whose configuration has changed. This mirrors startup flows
                 * while avoiding cross-backend hot-switches. */

#ifdef USE_RTLSDR
                if (cfg.has_input && (cfg.input_source == DSDCFG_INPUT_RTL || cfg.input_source == DSDCFG_INPUT_RTLTCP)
                    && old_audio_in_type == AUDIO_IN_RTL && opts->audio_in_type == AUDIO_IN_RTL
                    && strncmp(old_audio_in_dev, opts->audio_in_dev, sizeof old_audio_in_dev) != 0) {
                    if (cfg.input_source == DSDCFG_INPUT_RTL) {
                        if (cfg.rtl_device >= 0) {
                            opts->rtl_dev_index = cfg.rtl_device;
                        }
                        if (cfg.rtl_freq[0]) {
                            uint32_t hz = dsd_parse_freq_hz(cfg.rtl_freq);
                            if (hz > 0) {
                                opts->rtlsdr_center_freq = hz;
                            }
                        }
                        if (cfg.rtl_ppm) {
                            opts->rtlsdr_ppm_error = cfg.rtl_ppm;
                        }
                        if (cfg.rtl_bw_khz) {
                            opts->rtl_dsp_bw_khz = cfg.rtl_bw_khz;
                        }
                        if (cfg.rtl_sql) {
                            double sql = (double)cfg.rtl_sql;
                            if (sql > 1.0) {
                                sql /= (32768.0 * 32768.0);
                            }
                            opts->rtl_squelch_level = sql;
                            rtl_stream_set_channel_squelch((float)sql);
                        }
                        if (cfg.rtl_gain) {
                            opts->rtl_gain_value = cfg.rtl_gain;
                        }
                        if (cfg.rtl_volume) {
                            opts->rtl_volume_multiplier = cfg.rtl_volume;
                        }
                        opts->rtltcp_enabled = 0;
                    } else { // DSDCFG_INPUT_RTLTCP
                        if (cfg.rtltcp_host[0]) {
                            snprintf(opts->rtltcp_hostname, sizeof opts->rtltcp_hostname, "%s", cfg.rtltcp_host);
                        }
                        if (cfg.rtltcp_port) {
                            opts->rtltcp_portno = cfg.rtltcp_port;
                        }
                        if (cfg.rtl_freq[0]) {
                            uint32_t hz = dsd_parse_freq_hz(cfg.rtl_freq);
                            if (hz > 0) {
                                opts->rtlsdr_center_freq = hz;
                            }
                        }
                        if (cfg.rtl_ppm) {
                            opts->rtlsdr_ppm_error = cfg.rtl_ppm;
                        }
                        if (cfg.rtl_bw_khz) {
                            opts->rtl_dsp_bw_khz = cfg.rtl_bw_khz;
                        }
                        if (cfg.rtl_sql) {
                            double sql = (double)cfg.rtl_sql;
                            if (sql > 1.0) {
                                sql /= (32768.0 * 32768.0);
                            }
                            opts->rtl_squelch_level = sql;
                            rtl_stream_set_channel_squelch((float)sql);
                        }
                        if (cfg.rtl_gain) {
                            opts->rtl_gain_value = cfg.rtl_gain;
                        }
                        if (cfg.rtl_volume) {
                            opts->rtl_volume_multiplier = cfg.rtl_volume;
                        }
                        opts->rtltcp_enabled = 1;
                    }
                    (void)svc_rtl_restart(opts, state);
                }
#endif

                if (cfg.has_input && cfg.input_source == DSDCFG_INPUT_TCP && old_audio_in_type == AUDIO_IN_TCP
                    && strncmp(old_audio_in_dev, "tcp", 3) == 0 && strncmp(opts->audio_in_dev, "tcp", 3) == 0
                    && strncmp(old_audio_in_dev, opts->audio_in_dev, sizeof old_audio_in_dev) != 0) {
                    if (cfg.tcp_host[0]) {
                        snprintf(opts->tcp_hostname, sizeof opts->tcp_hostname, "%s", cfg.tcp_host);
                    }
                    if (cfg.tcp_port) {
                        opts->tcp_portno = cfg.tcp_port;
                    }
                    if (opts->tcp_in_ctx) {
                        tcp_input_close(opts->tcp_in_ctx);
                        opts->tcp_in_ctx = NULL;
                    }
                    if (opts->tcp_sockfd != 0) {
                        dsd_socket_close(opts->tcp_sockfd);
                        opts->tcp_sockfd = 0;
                    }
                    if (svc_tcp_connect_audio(opts, opts->tcp_hostname, opts->tcp_portno) != 0) {
                        LOG_ERROR("Config: failed to reconnect TCP audio %s:%d\n", opts->tcp_hostname,
                                  opts->tcp_portno);
                    }
                }

                if (cfg.has_input && cfg.input_source == DSDCFG_INPUT_UDP && old_audio_in_type == AUDIO_IN_UDP
                    && strncmp(old_audio_in_dev, "udp", 3) == 0 && strncmp(opts->audio_in_dev, "udp", 3) == 0
                    && strncmp(old_audio_in_dev, opts->audio_in_dev, sizeof old_audio_in_dev) != 0) {
                    if (cfg.udp_addr[0]) {
                        snprintf(opts->udp_in_bindaddr, sizeof opts->udp_in_bindaddr, "%s", cfg.udp_addr);
                    }
                    if (cfg.udp_port) {
                        opts->udp_in_portno = cfg.udp_port;
                    }
                    if (opts->udp_in_ctx) {
                        udp_input_stop(opts);
                    }
                    const char* bindaddr = opts->udp_in_bindaddr[0] ? opts->udp_in_bindaddr : "127.0.0.1";
                    int port = opts->udp_in_portno ? opts->udp_in_portno : 7355;
                    if (udp_input_start(opts, bindaddr, port, opts->wav_sample_rate) != 0) {
                        LOG_ERROR("Config: failed to restart UDP input %s:%d\n", bindaddr, port);
                    }
                }

                if (cfg.has_input && cfg.input_source == DSDCFG_INPUT_FILE && old_audio_in_type == AUDIO_IN_WAV
                    && strncmp(old_audio_in_dev, opts->audio_in_dev, sizeof old_audio_in_dev) != 0) {
                    if (opts->audio_in_file) {
                        sf_close(opts->audio_in_file);
                        opts->audio_in_file = NULL;
                    }
                    if (opts->audio_in_file_info) {
                        free(opts->audio_in_file_info);
                        opts->audio_in_file_info = NULL;
                    }
                    opts->audio_in_file_info = calloc(1, sizeof(SF_INFO));
                    if (!opts->audio_in_file_info) {
                        LOG_ERROR("Config: failed to allocate SF_INFO for file input\n");
                    } else {
                        opts->audio_in_file_info->samplerate = opts->wav_sample_rate;
                        opts->audio_in_file_info->channels = 1;
                        opts->audio_in_file_info->seekable = 0;
                        opts->audio_in_file_info->format = SF_FORMAT_RAW | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE;
                        opts->audio_in_file = sf_open(opts->audio_in_dev, SFM_READ, opts->audio_in_file_info);
                        if (!opts->audio_in_file) {
                            LOG_ERROR("Config: failed to open file input %s: %s\n", opts->audio_in_dev,
                                      sf_strerror(NULL));
                        } else {
                            opts->audio_in_type = AUDIO_IN_WAV;
                        }
                    }
                }

                if (cfg.has_input && cfg.input_source == DSDCFG_INPUT_PULSE && old_audio_in_type == AUDIO_IN_PULSE
                    && opts->audio_in_type == AUDIO_IN_PULSE) {
                    if (strncmp(old_audio_in_dev, opts->audio_in_dev, sizeof old_audio_in_dev) != 0
                        || strncmp(old_audio_in_dev, "pulse", 5) != 0) {
                        closeAudioInput(opts);
                        if (strncmp(opts->audio_in_dev, "pulse", 5) == 0 && opts->audio_in_dev[5] == ':'
                            && opts->audio_in_dev[6] != '\0') {
                            char tmp[128] = {0};
                            snprintf(tmp, sizeof tmp, "%s", opts->audio_in_dev + 6);
                            parse_audio_input_string(opts, tmp);
                        } else {
                            opts->pa_input_idx[0] = '\0';
                        }
                        if (openAudioInput(opts) != 0) {
                            LOG_ERROR("Config: failed to open PulseAudio input\n");
                        }
                    }
                }

                if (cfg.has_output && cfg.output_backend == DSDCFG_OUTPUT_PULSE && old_audio_out_type == 0
                    && opts->audio_out_type == 0) {
                    if (strncmp(old_audio_out_dev, opts->audio_out_dev, sizeof old_audio_out_dev) != 0
                        || strncmp(old_audio_out_dev, "pulse", 5) != 0) {
                        closeAudioOutput(opts);
                        if (strncmp(opts->audio_out_dev, "pulse", 5) == 0 && opts->audio_out_dev[5] == ':'
                            && opts->audio_out_dev[6] != '\0') {
                            char tmp[128] = {0};
                            snprintf(tmp, sizeof tmp, "%s", opts->audio_out_dev + 6);
                            parse_audio_output_string(opts, tmp);
                        } else {
                            opts->pa_output_idx[0] = '\0';
                        }
                        if (openAudioOutput(opts) != 0) {
                            LOG_ERROR("Config: failed to open PulseAudio output\n");
                        }
                    }
                }
            }
            break;
        }
        case UI_CMD_DMR_LE_TOGGLE: {
            if (opts) {
                svc_toggle_dmr_le(opts);
            }
            break;
        }
        case UI_CMD_ALL_MUTES_TOGGLE: {
            if (opts) {
                svc_toggle_all_mutes(opts);
            }
            break;
        }
        case UI_CMD_INV_X2_TOGGLE: {
            if (opts) {
                svc_toggle_inv_x2(opts);
            }
            break;
        }
        case UI_CMD_INV_DMR_TOGGLE: {
            if (opts) {
                svc_toggle_inv_dmr(opts);
            }
            break;
        }
        case UI_CMD_INV_DPMR_TOGGLE: {
            if (opts) {
                svc_toggle_inv_dpmr(opts);
            }
            break;
        }
        case UI_CMD_INV_M17_TOGGLE: {
            if (opts) {
                svc_toggle_inv_m17(opts);
            }
            break;
        }
        case UI_CMD_WAV_STATIC_OPEN: {
            if (opts && state && c->n > 0) {
                char path[1024] = {0};
                size_t n = c->n < sizeof(path) ? c->n : sizeof(path) - 1;
                memcpy(path, c->data, n);
                path[n] = '\0';
                svc_open_static_wav(opts, state, path);
            }
            break;
        }
        case UI_CMD_WAV_RAW_OPEN: {
            if (opts && state && c->n > 0) {
                char path[1024] = {0};
                size_t n = c->n < sizeof(path) ? c->n : sizeof(path) - 1;
                memcpy(path, c->data, n);
                path[n] = '\0';
                svc_open_raw_wav(opts, state, path);
            }
            break;
        }
        case UI_CMD_DSP_OUT_SET: {
            if (opts && c->n > 0) {
                char name[256] = {0};
                size_t n = c->n < sizeof(name) ? c->n : sizeof(name) - 1;
                memcpy(name, c->data, n);
                name[n] = '\0';
                svc_set_dsp_output_file(opts, name);
            }
            break;
        }
        case UI_CMD_SYMCAP_OPEN: {
            if (opts && state && c->n > 0) {
                char path[1024] = {0};
                size_t n = c->n < sizeof(path) ? c->n : sizeof(path) - 1;
                memcpy(path, c->data, n);
                path[n] = '\0';
                svc_open_symbol_out(opts, state, path);
            }
            break;
        }
        case UI_CMD_SYMBOL_IN_OPEN: {
            if (opts && state && c->n > 0) {
                char path[1024] = {0};
                size_t n = c->n < sizeof(path) ? c->n : sizeof(path) - 1;
                memcpy(path, c->data, n);
                path[n] = '\0';
                svc_open_symbol_in(opts, state, path);
            }
            break;
        }
        case UI_CMD_INPUT_WAV_SET: {
            if (opts && c->n > 0) {
                size_t n = c->n < sizeof(opts->audio_in_dev) ? c->n : sizeof(opts->audio_in_dev) - 1;
                memcpy(opts->audio_in_dev, c->data, n);
                opts->audio_in_dev[n] = '\0';
                opts->audio_in_type = AUDIO_IN_WAV;
            }
            break;
        }
        case UI_CMD_INPUT_SYM_STREAM_SET: {
            if (opts && c->n > 0) {
                size_t n = c->n < sizeof(opts->audio_in_dev) ? c->n : sizeof(opts->audio_in_dev) - 1;
                memcpy(opts->audio_in_dev, c->data, n);
                opts->audio_in_dev[n] = '\0';
                opts->audio_in_type = AUDIO_IN_SYMBOL_FLT;
            }
            break;
        }
        case UI_CMD_INPUT_SET_PULSE: {
            if (opts) {
                snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", "pulse");
                opts->audio_in_type = AUDIO_IN_PULSE;
            }
            break;
        }
        case UI_CMD_UDP_OUT_CFG: {
            if (opts && c->n >= (int)(256 + sizeof(int32_t))) {
                char host[256] = {0};
                memcpy(host, c->data, 255);
                int32_t port = 0;
                memcpy(&port, c->data + 256, sizeof(int32_t));
                svc_udp_output_config(opts, state, host, port);
            }
            break;
        }
        case UI_CMD_TCP_CONNECT_AUDIO_CFG: {
            if (opts && c->n >= (int)(256 + sizeof(int32_t))) {
                char host[256] = {0};
                memcpy(host, c->data, 255);
                int32_t port = 0;
                memcpy(&port, c->data + 256, sizeof(int32_t));
                svc_tcp_connect_audio(opts, host, port);
            }
            break;
        }
        case UI_CMD_RIGCTL_CONNECT_CFG: {
            if (opts && c->n >= (int)(256 + sizeof(int32_t))) {
                char host[256] = {0};
                memcpy(host, c->data, 255);
                int32_t port = 0;
                memcpy(&port, c->data + 256, sizeof(int32_t));
                svc_rigctl_connect(opts, host, port);
            }
            break;
        }
        case UI_CMD_UDP_INPUT_CFG: {
            if (opts && c->n >= (int)(256 + sizeof(int32_t))) {
                char bind[256] = {0};
                memcpy(bind, c->data, 255);
                int32_t port = 0;
                memcpy(&port, c->data + 256, sizeof(int32_t));
                snprintf(opts->udp_in_bindaddr, sizeof opts->udp_in_bindaddr, "%s", bind);
                opts->udp_in_portno = port;
                snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", "udp");
                opts->audio_in_type = AUDIO_IN_UDP;
            }
            break;
        }
#ifdef USE_RTLSDR
        case UI_CMD_RTL_ENABLE_INPUT: {
            if (opts && state) {
                svc_rtl_enable_input(opts, state);
            }
            break;
        }
        case UI_CMD_RTL_RESTART: {
            if (opts && state) {
                svc_rtl_restart(opts, state);
            }
            break;
        }
        case UI_CMD_RTL_SET_DEV: {
            if (opts && state && c->n >= (int)sizeof(int32_t)) {
                int32_t v = 0;
                memcpy(&v, c->data, sizeof v);
                svc_rtl_set_dev_index(opts, state, v);
            }
            break;
        }
        case UI_CMD_RTL_SET_FREQ: {
            if (opts && state && c->n >= (int)sizeof(int32_t)) {
                int32_t v = 0;
                memcpy(&v, c->data, sizeof v);
                svc_rtl_set_freq(opts, state, (uint32_t)v);
            }
            break;
        }
        case UI_CMD_RTL_SET_GAIN: {
            if (opts && state && c->n >= (int)sizeof(int32_t)) {
                int32_t v = 0;
                memcpy(&v, c->data, sizeof v);
                svc_rtl_set_gain(opts, state, v);
            }
            break;
        }
        case UI_CMD_RTL_SET_PPM: {
            if (opts && c->n >= (int)sizeof(int32_t)) {
                int32_t v = 0;
                memcpy(&v, c->data, sizeof v);
                svc_rtl_set_ppm(opts, v);
            }
            break;
        }
        case UI_CMD_RTL_SET_BW: {
            if (opts && state && c->n >= (int)sizeof(int32_t)) {
                int32_t v = 0;
                memcpy(&v, c->data, sizeof v);
                svc_rtl_set_bandwidth(opts, state, v);
            }
            break;
        }
        case UI_CMD_RTL_SET_SQL_DB: {
            if (opts && c->n >= (int)sizeof(double)) {
                double d = 0.0;
                memcpy(&d, c->data, sizeof d);
                svc_rtl_set_sql_db(opts, d);
            }
            break;
        }
        case UI_CMD_RTL_SET_VOL_MULT: {
            if (opts && c->n >= (int)sizeof(int32_t)) {
                int32_t v = 0;
                memcpy(&v, c->data, sizeof v);
                svc_rtl_set_volume_mult(opts, v);
            }
            break;
        }
        case UI_CMD_RTL_SET_BIAS_TEE: {
            if (opts && state && c->n >= (int)sizeof(int32_t)) {
                int32_t on = 0;
                memcpy(&on, c->data, sizeof on);
                svc_rtl_set_bias_tee(opts, state, on);
            }
            break;
        }
        case UI_CMD_RTLTCP_SET_AUTOTUNE: {
            if (opts && state && c->n >= (int)sizeof(int32_t)) {
                int32_t on = 0;
                memcpy(&on, c->data, sizeof on);
                svc_rtltcp_set_autotune(opts, state, on);
            }
            break;
        }
        case UI_CMD_RTL_SET_AUTO_PPM: {
            if (opts && state && c->n >= (int)sizeof(int32_t)) {
                int32_t on = 0;
                memcpy(&on, c->data, sizeof on);
                svc_rtl_set_auto_ppm(opts, state, on);
            }
            break;
        }
#endif /* USE_RTLSDR */
        case UI_CMD_RIGCTL_SET_MOD_BW: {
            if (opts && c->n >= (int)sizeof(int32_t)) {
                int32_t hz = 0;
                memcpy(&hz, c->data, sizeof hz);
                svc_set_rigctl_setmod_bw(opts, hz);
            }
            break;
        }
        case UI_CMD_TG_HOLD_SET: {
            if (state && c->n >= (int)sizeof(uint32_t)) {
                uint32_t tg = 0;
                memcpy(&tg, c->data, sizeof tg);
                svc_set_tg_hold(state, tg);
            }
            break;
        }
        case UI_CMD_HANGTIME_SET: {
            if (opts && c->n >= (int)sizeof(double)) {
                double s = 0.0;
                memcpy(&s, c->data, sizeof s);
                svc_set_hangtime(opts, s);
            }
            break;
        }
        case UI_CMD_SLOT_PREF_SET: {
            if (opts && c->n >= (int)sizeof(int32_t)) {
                int32_t p = 0;
                memcpy(&p, c->data, sizeof p);
                svc_set_slot_pref(opts, p);
            }
            break;
        }
        case UI_CMD_SLOTS_ONOFF_SET: {
            if (opts && c->n >= (int)sizeof(int32_t)) {
                int32_t m = 0;
                memcpy(&m, c->data, sizeof m);
                svc_set_slots_onoff(opts, m);
            }
            break;
        }
        case UI_CMD_PULSE_OUT_SET: {
            if (opts && c->n > 0) {
                char name[256] = {0};
                size_t n = c->n < sizeof(name) ? c->n : sizeof(name) - 1;
                memcpy(name, c->data, n);
                name[n] = '\0';
                svc_set_pulse_output(opts, name);
            }
            break;
        }
        case UI_CMD_PULSE_IN_SET: {
            if (opts && c->n > 0) {
                char name[256] = {0};
                size_t n = c->n < sizeof(name) ? c->n : sizeof(name) - 1;
                memcpy(name, c->data, n);
                name[n] = '\0';
                svc_set_pulse_input(opts, name);
            }
            break;
        }

        case UI_CMD_LRRP_SET_HOME: {
            if (opts) {
                svc_lrrp_set_home(opts);
            }
            break;
        }
        case UI_CMD_LRRP_SET_DSDP: {
            if (opts) {
                svc_lrrp_set_dsdp(opts);
            }
            break;
        }
        case UI_CMD_LRRP_SET_CUSTOM: {
            if (opts && c->n > 0) {
                char path[1024] = {0};
                size_t n = c->n < sizeof(path) ? c->n : sizeof(path) - 1;
                memcpy(path, c->data, n);
                path[n] = '\0';
                svc_lrrp_set_custom(opts, path);
            }
            break;
        }
        case UI_CMD_LRRP_DISABLE: {
            if (opts) {
                svc_lrrp_disable(opts);
            }
            break;
        }
        case UI_CMD_P25_P2_PARAMS_SET: {
            if (state && c->n >= (int)(sizeof(uint64_t) * 3)) {
                struct {
                    uint64_t w;
                    uint64_t s;
                    uint64_t n;
                } p = {0};

                memcpy(&p, c->data, sizeof p);
                svc_set_p2_params(state, p.w, p.s, p.n);
            }
            break;
        }
        case UI_CMD_UI_SHOW_DSP_PANEL_TOGGLE: opts->show_dsp_panel = opts->show_dsp_panel ? 0 : 1; break;
        case UI_CMD_UI_SHOW_P25_METRICS_TOGGLE: opts->show_p25_metrics = opts->show_p25_metrics ? 0 : 1; break;
        case UI_CMD_UI_SHOW_P25_AFFIL_TOGGLE: opts->show_p25_affiliations = opts->show_p25_affiliations ? 0 : 1; break;
        case UI_CMD_UI_SHOW_P25_NEIGHBORS_TOGGLE: opts->show_p25_neighbors = opts->show_p25_neighbors ? 0 : 1; break;
        case UI_CMD_UI_SHOW_P25_IDEN_TOGGLE: opts->show_p25_iden_plan = opts->show_p25_iden_plan ? 0 : 1; break;
        case UI_CMD_UI_SHOW_P25_CCC_TOGGLE: opts->show_p25_cc_candidates = opts->show_p25_cc_candidates ? 0 : 1; break;
        case UI_CMD_UI_SHOW_CHANNELS_TOGGLE: opts->show_channels = opts->show_channels ? 0 : 1; break;
        case UI_CMD_UI_SHOW_P25_CALLSIGN_TOGGLE:
            opts->show_p25_callsign_decode = opts->show_p25_callsign_decode ? 0 : 1;
            break;
        case UI_CMD_KEY_BASIC_SET: {
            if (state && opts && c->n >= (int)sizeof(uint32_t)) {
                uint32_t v = 0;
                memcpy(&v, c->data, sizeof v);
                state->K = v;
                state->keyloader = 0;
                state->payload_keyid = state->payload_keyidR = 0;
                opts->dmr_mute_encL = opts->dmr_mute_encR = 0;
            }
            break;
        }
        case UI_CMD_KEY_SCRAMBLER_SET: {
            if (state && opts && c->n >= (int)sizeof(uint32_t)) {
                uint32_t v = 0;
                memcpy(&v, c->data, sizeof v);
                state->R = v;
                state->keyloader = 0;
                state->payload_keyid = state->payload_keyidR = 0;
                opts->dmr_mute_encL = opts->dmr_mute_encR = 0;
            }
            break;
        }
        case UI_CMD_KEY_RC4DES_SET: {
            if (state && opts && c->n >= (int)sizeof(uint64_t)) {
                uint64_t v = 0;
                memcpy(&v, c->data, sizeof v);
                state->R = v;
                state->RR = v;
                state->keyloader = 0;
                state->payload_keyid = state->payload_keyidR = 0;
                opts->dmr_mute_encL = opts->dmr_mute_encR = 0;
            }
            break;
        }
        case UI_CMD_KEY_HYTERA_SET: {
            if (state && opts && c->n >= (int)(sizeof(uint64_t) * 5)) {
                struct {
                    uint64_t H, K1, K2, K3, K4;
                } p;

                memcpy(&p, c->data, sizeof p);
                state->H = p.H;
                state->K1 = p.K1;
                state->K2 = p.K2;
                state->K3 = p.K3;
                state->K4 = p.K4;
                state->keyloader = 0;
                opts->dmr_mute_encL = opts->dmr_mute_encR = 0;
            }
            break;
        }
        case UI_CMD_KEY_AES_SET: {
            if (state && opts && c->n >= (int)(sizeof(uint64_t) * 4)) {
                struct {
                    uint64_t K1, K2, K3, K4;
                } p;

                memcpy(&p, c->data, sizeof p);
                state->K1 = p.K1;
                state->K2 = p.K2;
                state->K3 = p.K3;
                state->K4 = p.K4;
                memset(state->A1, 0, sizeof(state->A1));
                memset(state->A2, 0, sizeof(state->A2));
                memset(state->A3, 0, sizeof(state->A3));
                memset(state->A4, 0, sizeof(state->A4));
                state->keyloader = 0;
                opts->dmr_mute_encL = opts->dmr_mute_encR = 0;
            }
            break;
        }
        case UI_CMD_KEY_TYT_AP_SET: {
            if (state && c->n > 0) {
                char s[256];
                size_t n = (c->n < sizeof s - 1) ? c->n : sizeof s - 1;
                memcpy(s, c->data, n);
                s[n] = '\0';
                tyt_ap_pc4_keystream_creation(state, s);
            }
            break;
        }
        case UI_CMD_KEY_RETEVIS_RC2_SET: {
            if (state && c->n > 0) {
                char s[256];
                size_t n = (c->n < sizeof s - 1) ? c->n : sizeof s - 1;
                memcpy(s, c->data, n);
                s[n] = '\0';
                retevis_rc2_keystream_creation(state, s);
            }
            break;
        }
        case UI_CMD_KEY_TYT_EP_SET: {
            if (state && c->n > 0) {
                char s[256];
                size_t n = (c->n < sizeof s - 1) ? c->n : sizeof s - 1;
                memcpy(s, c->data, n);
                s[n] = '\0';
                tyt_ep_aes_keystream_creation(state, s);
            }
            break;
        }
        case UI_CMD_KEY_KEN_SCR_SET: {
            if (state && c->n > 0) {
                char s[128];
                size_t n = (c->n < sizeof s - 1) ? c->n : sizeof s - 1;
                memcpy(s, c->data, n);
                s[n] = '\0';
                ken_dmr_scrambler_keystream_creation(state, s);
            }
            break;
        }
        case UI_CMD_KEY_ANYTONE_BP_SET: {
            if (state && c->n > 0) {
                char s[128];
                size_t n = (c->n < sizeof s - 1) ? c->n : sizeof s - 1;
                memcpy(s, c->data, n);
                s[n] = '\0';
                anytone_bp_keystream_creation(state, s);
            }
            break;
        }
        case UI_CMD_KEY_XOR_SET: {
            if (state && c->n > 0) {
                char s[256];
                size_t n = (c->n < sizeof s - 1) ? c->n : sizeof s - 1;
                memcpy(s, c->data, n);
                s[n] = '\0';
                straight_mod_xor_keystream_creation(state, s);
            }
            break;
        }
        case UI_CMD_M17_USER_DATA_SET: {
            if (state && c->n > 0) {
                size_t n = (c->n < sizeof(state->m17dat) - 1) ? c->n : sizeof(state->m17dat) - 1;
                memcpy(state->m17dat, c->data, n);
                state->m17dat[n] = '\0';
            }
            break;
        }
        case UI_CMD_IMPORT_CHANNEL_MAP: {
            if (opts && state && c->n > 0) {
                char path[1024] = {0};
                size_t n = c->n < sizeof(path) ? c->n : sizeof(path) - 1;
                memcpy(path, c->data, n);
                path[n] = '\0';
                svc_import_channel_map(opts, state, path);
            }
            break;
        }
        case UI_CMD_IMPORT_GROUP_LIST: {
            if (opts && state && c->n > 0) {
                char path[1024] = {0};
                size_t n = c->n < sizeof(path) ? c->n : sizeof(path) - 1;
                memcpy(path, c->data, n);
                path[n] = '\0';
                svc_import_group_list(opts, state, path);
            }
            break;
        }
        case UI_CMD_IMPORT_KEYS_DEC: {
            if (opts && state && c->n > 0) {
                char path[1024] = {0};
                size_t n = c->n < sizeof(path) ? c->n : sizeof(path) - 1;
                memcpy(path, c->data, n);
                path[n] = '\0';
                svc_import_keys_dec(opts, state, path);
            }
            break;
        }
        case UI_CMD_IMPORT_KEYS_HEX: {
            if (opts && state && c->n > 0) {
                char path[1024] = {0};
                size_t n = c->n < sizeof(path) ? c->n : sizeof(path) - 1;
                memcpy(path, c->data, n);
                path[n] = '\0';
                svc_import_keys_hex(opts, state, path);
            }
            break;
        }
#ifdef USE_RTLSDR
        case UI_CMD_DSP_OP: {
            UiDspPayload p = {0};
            if (c->n >= (int)sizeof(UiDspPayload)) {
                memcpy(&p, c->data, sizeof p);
            }
            switch (p.op) {
                case UI_DSP_OP_TOGGLE_CQ: {
                    int cq = 0, f = 0, t = 0;
                    rtl_stream_dsp_get(&cq, &f, &t);
                    rtl_stream_toggle_cqpsk(cq ? 0 : 1);
                    break;
                }
                case UI_DSP_OP_TOGGLE_FLL: {
                    int cq = 0, f = 0, t = 0;
                    rtl_stream_dsp_get(&cq, &f, &t);
                    rtl_stream_toggle_fll(f ? 0 : 1);
                    break;
                }
                case UI_DSP_OP_TOGGLE_TED: {
                    int cq = 0, f = 0, t = 0;
                    rtl_stream_dsp_get(&cq, &f, &t);
                    rtl_stream_toggle_ted(t ? 0 : 1);
                    break;
                }
                case UI_DSP_OP_TOGGLE_IQBAL: {
                    int on = rtl_stream_get_iq_balance();
                    int new_on = on ? 0 : 1;
                    rtl_stream_toggle_iq_balance(new_on);
                    dsd_setenv("DSD_NEO_IQ_BALANCE", new_on ? "1" : "0", 1);
                    break;
                }
                case UI_DSP_OP_IQ_DC_TOGGLE: {
                    int k = 0;
                    int on = rtl_stream_get_iq_dc(&k);
                    int new_on = on ? 0 : 1;
                    rtl_stream_set_iq_dc(new_on, -1);
                    dsd_setenv("DSD_NEO_IQ_DC_BLOCK", new_on ? "1" : "0", 1);
                    break;
                }
                case UI_DSP_OP_IQ_DC_K_DELTA: {
                    int k = 0;
                    (void)rtl_stream_get_iq_dc(&k);
                    int nk = k + p.a;
                    rtl_stream_set_iq_dc(-1, nk);
                    break;
                }
                case UI_DSP_OP_TED_GAIN_SET: {
                    /* p.a is gain in milli-units (e.g., 25 = 0.025) */
                    int g_milli = p.a;
                    if (g_milli < 10) {
                        g_milli = 10; /* min 0.01 */
                    }
                    if (g_milli > 500) {
                        g_milli = 500; /* max 0.5 */
                    }
                    float g = (float)g_milli * 0.001f;
                    rtl_stream_set_ted_gain(g);
                    break;
                }
                case UI_DSP_OP_C4FM_CLK_CYCLE: {
                    int mode = rtl_stream_get_c4fm_clk();
                    mode = (mode + 1) % 3;
                    rtl_stream_set_c4fm_clk(mode);
                    break;
                }
                case UI_DSP_OP_C4FM_CLK_SYNC_TOGGLE: {
                    int en = rtl_stream_get_c4fm_clk_sync();
                    rtl_stream_set_c4fm_clk_sync(en ? 0 : 1);
                    break;
                }
                case UI_DSP_OP_FM_AGC_TOGGLE: {
                    int on = rtl_stream_get_fm_agc();
                    rtl_stream_set_fm_agc(on ? 0 : 1);
                    break;
                }

                case UI_DSP_OP_FM_LIMITER_TOGGLE: {
                    int on = rtl_stream_get_fm_limiter();
                    rtl_stream_set_fm_limiter(on ? 0 : 1);
                    break;
                }
                case UI_DSP_OP_FM_AGC_TARGET_DELTA: {
                    float tgt = 0.0f;
                    rtl_stream_get_fm_agc_params(&tgt, NULL, NULL, NULL);
                    float nt = tgt + ((float)p.a * 0.01f);
                    if (nt < 0.05f) {
                        nt = 0.05f;
                    }
                    if (nt > 2.5f) {
                        nt = 2.5f;
                    }
                    rtl_stream_set_fm_agc_params(nt, -1.0f, -1.0f, -1.0f);
                    break;
                }
                case UI_DSP_OP_FM_AGC_MIN_DELTA: {
                    float mn = 0.0f;
                    rtl_stream_get_fm_agc_params(NULL, &mn, NULL, NULL);
                    float nm = mn + ((float)p.a * 0.01f);
                    if (nm < 0.0f) {
                        nm = 0.0f;
                    }
                    if (nm > 1.0f) {
                        nm = 1.0f;
                    }
                    rtl_stream_set_fm_agc_params(-1.0f, nm, -1.0f, -1.0f);
                    break;
                }
                case UI_DSP_OP_FM_AGC_ATTACK_DELTA: {
                    float au = 0.0f;
                    rtl_stream_get_fm_agc_params(NULL, NULL, &au, NULL);
                    float na = au + ((float)p.a * 0.01f);
                    if (na < 0.0f) {
                        na = 0.0f;
                    }
                    if (na > 1.0f) {
                        na = 1.0f;
                    }
                    rtl_stream_set_fm_agc_params(-1.0f, -1.0f, na, -1.0f);
                    break;
                }
                case UI_DSP_OP_FM_AGC_DECAY_DELTA: {
                    float ad = 0.0f;
                    rtl_stream_get_fm_agc_params(NULL, NULL, NULL, &ad);
                    float nd = ad + ((float)p.a * 0.01f);
                    if (nd < 0.0f) {
                        nd = 0.0f;
                    }
                    if (nd > 1.0f) {
                        nd = 1.0f;
                    }
                    rtl_stream_set_fm_agc_params(-1.0f, -1.0f, -1.0f, nd);
                    break;
                }
                case UI_DSP_OP_TUNER_AUTOGAIN_TOGGLE: {
                    int on = rtl_stream_get_tuner_autogain();
                    rtl_stream_set_tuner_autogain(on ? 0 : 1);
                    break;
                }
                default: break;
            }
            break;
        }
#endif
        default: break;
    }
}

int
ui_drain_cmds(dsd_opts* opts, dsd_state* state) {
    int n_applied = 0;
    ensure_mu_init();
    for (;;) {
        struct UiCmd cmd;
        int have = 0;
        dsd_mutex_lock(&g_mu);
        if (!q_is_empty_unlocked()) {
            cmd = g_q[g_head];
            g_head = (g_head + 1) % UI_CMD_Q_CAP;
            have = 1;
        }
        // Reset overflow warning gate when queue has space again
        if (((g_tail + 1) % UI_CMD_Q_CAP) != g_head) {
            atomic_store(&g_overflow_warn_gate, 0);
        }
        dsd_mutex_unlock(&g_mu);
        if (!have) {
            break;
        }
        apply_cmd(opts, state, &cmd);
        // After applying a command, publish updated snapshots so the UI can
        // render consistent opts/state without racing live structures.
        ui_publish_opts_snapshot(opts);
        if (state) {
            ui_publish_snapshot(state);
        }
        n_applied++;
    }
    return n_applied;
}
