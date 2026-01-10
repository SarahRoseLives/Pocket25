// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/init.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/power.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/state_ext.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/dsp/dmr_sync.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/runtime/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mbelib.h>

#ifdef USE_CODEC2
#include <codec2/codec2.h>
#endif

// Small helpers to efficiently set fixed-width strings
static inline void
set_spaces(char* buf, size_t count) {
    memset(buf, ' ', count);
    buf[count] = '\0';
}

static inline void
set_underscores(char* buf, size_t count) {
    memset(buf, '_', count);
    buf[count] = '\0';
}

void
initOpts(dsd_opts* opts) {
    opts->floating_point = 0; //use floating point audio output
    opts->onesymbol = 10;
    opts->mbe_in_file[0] = 0;
    opts->mbe_in_f = NULL;
    opts->errorbars = 1;
    opts->datascope = 0;
    opts->constellation = 0;
    opts->const_gate_qpsk = 0.25f;
    opts->const_gate_other = 0.05f;
    opts->const_norm_mode = 0; // default: radial percentile normalization
    opts->eye_view = 0;
    opts->fsk_hist_view = 0;
    opts->eye_unicode = 1;              //default On for clearer rendering
    opts->eye_color = 1;                //default On when terminal supports color
    opts->show_dsp_panel = 0;           // hide compact DSP panel by default
    opts->show_p25_metrics = 0;         // hide P25 metrics by default
    opts->show_p25_neighbors = 0;       // hide P25 Neighbors by default
    opts->show_p25_iden_plan = 0;       // hide P25 IDEN Plan by default
    opts->show_p25_cc_candidates = 0;   // hide P25 CC Candidates by default
    opts->show_p25_callsign_decode = 0; // hide P25 callsign decode by default (many false positives)
    opts->show_channels = 0;            // hide Channels section by default
    opts->symboltiming = 0;
    opts->verbose = 2;
    opts->p25enc = 0;
    opts->p25lc = 0;
    opts->p25status = 0;
    opts->p25tg = 0;
    opts->scoperate = 15;
    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", "pulse");
    snprintf(opts->audio_out_dev, sizeof opts->audio_out_dev, "%s", "pulse");
    opts->audio_in_fd = -1;
    opts->audio_out_fd = -1;

    opts->split = 0;
    opts->playoffset = 0;
    opts->playoffsetR = 0;
    snprintf(opts->wav_out_dir, sizeof opts->wav_out_dir, "%s", "./WAV");
    opts->mbe_out_dir[0] = 0;
    opts->mbe_out_file[0] = 0;
    opts->mbe_out_fileR[0] = 0; //second slot on a TDMA system
    opts->mbe_out_path[0] = 0;
    opts->mbe_out_f = NULL;
    opts->mbe_out_fR = NULL; //second slot on a TDMA system
    opts->audio_gain = 0;
    opts->audio_gainR = 0;
    opts->audio_gainA = 50.0f; //scale of 1 - 100
    opts->audio_out = 1;
    opts->wav_out_file[0] = 0;
    opts->wav_out_fileR[0] = 0;
    opts->wav_out_file_raw[0] = 0;
    opts->symbol_out_file[0] = 0;
    opts->lrrp_out_file[0] = 0;
    opts->event_out_file[0] = 0;
    //csv import filenames
    opts->group_in_file[0] = 0;
    opts->lcn_in_file[0] = 0;
    opts->chan_in_file[0] = 0;
    opts->key_in_file[0] = 0;
    //end import filenames
    opts->szNumbers[0] = 0;
    opts->symbol_out_f = NULL;
    opts->symbol_out_file_creation_time = time(NULL);
    opts->symbol_out_file_is_auto = 0;
    opts->mbe_out = 0;
    opts->mbe_outR = 0; //second slot on a TDMA system
    opts->wav_out_f = NULL;
    opts->wav_out_fR = NULL;
    opts->wav_out_raw = NULL;

    opts->dmr_stereo_wav = 0;  //flag for per call dmr stereo wav recordings
    opts->static_wav_file = 0; //single static wav file for decoding duration
    //opts->wav_out_fd = -1;
    opts->serial_baud = 115200;
    opts->serial_fd = -1;
    snprintf(opts->serial_dev, sizeof opts->serial_dev, "%s", "/dev/ttyUSB0");
    opts->resume = 0;
    opts->frame_dstar = 1;
    opts->frame_x2tdma = 1;
    opts->frame_p25p1 = 1;
    opts->frame_p25p2 = 1;
    opts->frame_nxdn48 = 0;
    opts->frame_nxdn96 = 0;
    opts->frame_dmr = 1;
    opts->frame_dpmr = 0;
    opts->frame_provoice = 0;
    opts->frame_ysf = 1;
    opts->frame_m17 = 0;
    opts->mod_c4fm = 1;
    opts->mod_qpsk = 0;
    opts->mod_gfsk = 0;
    opts->mod_cli_lock = 0; // by default, allow auto modulation selection
    opts->uvquality = 3;
    opts->inverted_x2tdma = 1; // most transmitter + scanner + sound card combinations show inverted signals for this
    opts->inverted_dmr = 0; // most transmitter + scanner + sound card combinations show non-inverted signals for this
    opts->inverted_m17 = 0; //samples from M17_Education seem to all be positive polarity (same from m17-tools programs)
    opts->ssize = 128;      //36 default, max is 128, much cleaner data decodes on Phase 2 cqpsk at max
    opts->msize = 1024;     //15 default, max is 1024, much cleaner data decodes on Phase 2 cqpsk at max
    opts->playfiles = 0;
    opts->m17encoder = 0;
    opts->m17encoderbrt = 0;
    opts->m17encoderpkt = 0;
    opts->m17decoderip = 0;
    opts->delay = 0;
    opts->use_cosine_filter = 1;
    opts->unmute_encrypted_p25 = 0;
    //all RTL user options -- enabled AGC by default due to weak signal related issues
    opts->rtl_dev_index = 0;  //choose which device we want by index number
    opts->rtl_gain_value = 0; //mid value, 0 - AGC - 0 to 49 acceptable values
    opts->rtl_squelch_level = dB_to_pwr(-110);
    opts->rtl_volume_multiplier =
        2; //sample multiplier; This multiplies the sample value to produce a higher 'inlvl' for the demodulator
    // Generic input volume for non-RTL inputs (Pulse/WAV/TCP/UDP)
    opts->input_volume_multiplier = 1;
    opts->rtl_udp_port =
        0; //set UDP port for RTL remote -- 0 by default, will be making this optional for some external/legacy use cases (edacs-fm, etc)
    opts->rtl_dsp_bw_khz = 48;  // DSP baseband kHz (4,6,8,12,16,24,48). Not tuner IF BW.
    opts->rtlsdr_ppm_error = 0; //initialize ppm with 0 value;
    opts->rtlsdr_center_freq =
        850000000; //set to an initial value (if user is using a channel map, then they won't need to specify anything other than -i rtl if desired)
    opts->rtl_started = 0;
    opts->rtl_needs_restart = 0;
    opts->rtl_pwr = 0;                // mean power approximation level on rtl input signal
    opts->rtl_bias_tee = 0;           // bias tee disabled by default
    opts->rtl_auto_ppm = 0;           // spectrum-based auto PPM disabled by default
    opts->rtl_auto_ppm_snr_db = 0.0f; // use default SNR threshold unless overridden
    //end RTL user options
    opts->pulse_raw_rate_in = 48000;
    opts->pulse_raw_rate_out = 48000; //
    opts->pulse_digi_rate_in = 48000;
    opts->pulse_digi_rate_out = 8000; //
    opts->pulse_raw_in_channels = 1;
    opts->pulse_raw_out_channels = 1;
    opts->pulse_digi_in_channels = 1;  //2
    opts->pulse_digi_out_channels = 2; //new default for AUTO
    memset(opts->pa_input_idx, 0, 100 * sizeof(char));
    memset(opts->pa_output_idx, 0, 100 * sizeof(char));

    opts->wav_sample_rate = 48000; //default value (DSDPlus uses 96000 on raw signal wav files)
    opts->wav_interpolator = 1;    //default factor of 1 on 48000; 2 on 96000; sample rate / decimator
    opts->wav_decimator = 48000;   //maybe for future use?

    snprintf(opts->output_name, sizeof opts->output_name, "%s", "AUTO");
    opts->pulse_flush = 1; //set 0 to flush, 1 for flushed
    opts->use_ncurses_terminal = 0;
    opts->ncurses_compact = 0;
    opts->ncurses_history = 1;
#ifdef LIMAZULUTWEAKS
    opts->ncurses_compact = 1;
#endif
    opts->payload = 0;
    opts->inverted_dpmr = 0;
    opts->dmr_mono = 0;
    opts->dmr_stereo = 1;
    opts->aggressive_framesync = 1;
    /* DMR: strict CRC gating by default (use -F to relax, like other protocols). */
    opts->dmr_crc_relaxed_default = 0;

    opts->audio_in_type = AUDIO_IN_PULSE;
    opts->audio_out_type = 0;

    opts->lrrp_file_output = 0;

    opts->dmr_mute_encL = 1;
    opts->dmr_mute_encR = 1;

    opts->monitor_input_audio = 0; //enable with -8
    opts->analog_only = 0;         //only turned on with -fA

    opts->inverted_p2 = 0;
    opts->p2counter = 0;

    opts->call_alert = 0; //call alert beeper for ncurses

    //rigctl options
    opts->use_rigctl = 0;
    opts->rigctl_sockfd = DSD_INVALID_SOCKET;
    opts->rigctlportno = 4532; //TCP Port Number; GQRX - 7356; SDR++ - 4532
    snprintf(opts->rigctlhostname, sizeof opts->rigctlhostname, "%s", "localhost");

    //UDP Socket Blaster Audio
    opts->udp_sockfd = DSD_INVALID_SOCKET;
    opts->udp_sockfdA = DSD_INVALID_SOCKET;
    opts->udp_portno = 23456; //default port, same os OP25's sockaudio.py
    snprintf(opts->udp_hostname, sizeof opts->udp_hostname, "%s", "127.0.0.1");

    //M17 UDP Port and hostname
    opts->m17_use_ip = 0;                    //if enabled, open UDP and broadcast IP frame
    opts->m17_portno = 17000;                //default is 17000
    opts->m17_udp_sock = DSD_INVALID_SOCKET; //actual UDP socket for M17 to send to
    snprintf(opts->m17_hostname, sizeof opts->m17_hostname, "%s", "127.0.0.1");

    //tcp input options
    opts->tcp_sockfd = DSD_INVALID_SOCKET;
    opts->tcp_portno = 7355; //default favored by SDR++
    snprintf(opts->tcp_hostname, sizeof opts->tcp_hostname, "%s", "localhost");

    // rtl_tcp defaults
    opts->rtltcp_enabled = 0;
    opts->rtltcp_portno = 1234;
    snprintf(opts->rtltcp_hostname, sizeof opts->rtltcp_hostname, "%s", "127.0.0.1");
    opts->rtltcp_autotune = 0; // default off; enable via CLI --rtltcp-autotune or env

    // UDP direct input defaults
    opts->udp_in_sockfd = DSD_INVALID_SOCKET;
    opts->udp_in_portno = 7355;
    opts->udp_in_bindaddr[0] = '\0';
    opts->udp_in_ctx = NULL;
    opts->udp_in_packets = 0ULL;
    opts->udp_in_bytes = 0ULL;
    opts->udp_in_drops = 0ULL;

    opts->p25_trunk = 0;                  //0 disabled, 1 is enabled
    opts->trunk_enable = opts->p25_trunk; // keep alias in sync
    opts->p25_is_tuned = 0;               //set to 1 if currently on VC, set back to 0 on carrier drop
    // Default hangtime aligned with OP25 (2s) while still releasing promptly after calls.
    opts->trunk_hangtime = 2.0f;

    opts->scanner_mode = 0; //0 disabled, 1 is enabled
    opts->trunk_cli_seen = 0;

    //reverse mute
    opts->reverse_mute = 0;

    //setmod bandwidth
    opts->setmod_bw = 0; //default to 0 - off

    //DMR Location Area - DMRLA B***S***
    opts->dmr_dmrla_is_set = 0;
    opts->dmr_dmrla_n = 0;

    //DMR Late Entry
    opts->dmr_le = 1; //re-enabled again

    //Trunking - Use Group List as Allow List
    opts->trunk_use_allow_list = 0; //disabled by default

    //Trunking - Tune Group Calls
    opts->trunk_tune_group_calls = 1; //enabled by default

    //Trunking - Tune Private Calls
    opts->trunk_tune_private_calls = 1; //enabled by default

    //Trunking - Tune Data Calls
    opts->trunk_tune_data_calls = 0; //disabled by default

    //Trunking - Tune Encrypted Calls (P25 only on applicable grants with svc opts)
    opts->trunk_tune_enc_calls = 1; //enabled by default

    //P25 LCW explicit retune (format 0x44)
    opts->p25_lcw_retune = 0; //disabled by default

    opts->dPMR_next_part_of_superframe = 0;

    opts->slot_preference = 2;
    //hardset slots to synthesize
    opts->slot1_on = 1;
    opts->slot2_on = 1;

    //enable filter options
    opts->use_lpf = 0;
    opts->use_hpf = 1;
    opts->use_pbf = 1;
    opts->use_hpf_d = 1;

    //dsp structured file
    opts->dsp_out_file[0] = 0;
    opts->use_dsp_output = 0;

    //Use P25p1 heuristics
    opts->use_heuristics = 0;

    //DMR TIII heuristic LCN fill (opt-in)
    opts->dmr_t3_heuristic_fill = 0;

    // P25P2 soft-decision RS erasure marking (enabled by default)
    opts->p25_p2_soft_erasure = 1;

    // P25P1 soft-decision FEC for voice (enabled by default)
    opts->p25_p1_soft_voice = 1;

    // Low input level warning defaults
    opts->input_warn_db = -40.0;        // warn if below -40 dBFS
    opts->input_warn_cooldown_sec = 10; // rate-limit warnings
    opts->last_input_warn_time = 0;

    // P25 SM unified follower config (CLI-mirrored; values <=0 mean unset)
    opts->p25_vc_grace_s = 0.0;
    opts->p25_min_follow_dwell_s = 0.0;
    opts->p25_grant_voice_to_s = 0.0;
    opts->p25_retune_backoff_s = 0.0;
    opts->p25_force_release_extra_s = 0.0;
    opts->p25_force_release_margin_s = 0.0;
    opts->p25_p1_err_hold_pct = 0.0;
    opts->p25_p1_err_hold_s = 0.0;

} //initopts

static void*
aligned_alloc_64(size_t size) {
    return dsd_aligned_alloc(64, size);
}

void
initState(dsd_state* state) {

    int i, j;
    // state->testcounter = 0;
    state->last_dibit = 0;

    for (int ext_i = 0; ext_i < DSD_STATE_EXT_MAX; ext_i++) {
        state->state_ext[ext_i] = NULL;
        state->state_ext_cleanup[ext_i] = NULL;
    }

    state->dibit_buf = aligned_alloc_64(sizeof(int) * 1000000);
    state->dibit_buf_p = state->dibit_buf + 200;
    memset(state->dibit_buf, 0, sizeof(int) * 200);
    //dmr buffer -- double check this set up
    state->dmr_payload_buf = aligned_alloc_64(sizeof(int) * 1000000);
    state->dmr_payload_p = state->dmr_payload_buf + 200;
    memset(state->dmr_payload_buf, 0, sizeof(int) * 200);
    memset(state->dmr_stereo_payload, 1, sizeof(int) * 144);
    //dmr buffer end

    // Symbol history buffer for resample-on-sync (SDRTrunk-style)
    // Note: Buffer stores symbols (one per dibit decision), not raw audio samples
    state->dmr_sample_history_size = DMR_SAMPLE_HISTORY_SIZE; // ~427ms at 4800 sym/s
    state->dmr_sample_history = aligned_alloc_64(sizeof(float) * state->dmr_sample_history_size);
    if (state->dmr_sample_history) {
        memset(state->dmr_sample_history, 0, sizeof(float) * state->dmr_sample_history_size);
    }
    state->dmr_sample_history_head = 0;
    state->dmr_sample_history_count = 0;

    state->repeat = 0;

    // RTL-SDR stream context (initialized to NULL; lifecycle managed by caller)
    state->rtl_ctx = NULL;

    //Bitmap Filtering Options
    state->audio_smoothing = 0;

    memset(state->audio_out_temp_buf, 0.0f, sizeof(state->audio_out_temp_buf));
    memset(state->audio_out_temp_bufR, 0.0f, sizeof(state->audio_out_temp_bufR));

    //set float temp buffer to baseline
    memset(state->f_l, 0.0f, sizeof(state->f_l));
    memset(state->f_r, 0.0f, sizeof(state->f_r));

    //set float temp buffer to baseline
    memset(state->f_l4, 0.0f, sizeof(state->f_l4));
    memset(state->f_r4, 0.0f, sizeof(state->f_r4));

    //zero out the short sample storage buffers
    memset(state->s_l, 0, sizeof(state->s_l));
    memset(state->s_r, 0, sizeof(state->s_r));
    memset(state->s_l4, 0, sizeof(state->s_l4));
    memset(state->s_r4, 0, sizeof(state->s_r4));

    memset(state->s_lu, 0, sizeof(state->s_lu));
    memset(state->s_ru, 0, sizeof(state->s_ru));
    memset(state->s_l4u, 0, sizeof(state->s_l4u));
    memset(state->s_r4u, 0, sizeof(state->s_r4u));

    state->audio_out_buf = aligned_alloc_64(sizeof(short) * 1000000);
    state->audio_out_bufR = aligned_alloc_64(sizeof(short) * 1000000);
    memset(state->audio_out_buf, 0, 100 * sizeof(short));
    memset(state->audio_out_bufR, 0, 100 * sizeof(short));
    //analog/raw signal audio buffers
    state->analog_sample_counter = 0; //when it reaches 960, then dump the raw/analog audio signal and reset
    memset(state->analog_out_f, 0, sizeof(state->analog_out_f));
    memset(state->analog_out, 0, sizeof(state->analog_out));
    state->audio_out_buf_p = state->audio_out_buf + 100;
    state->audio_out_buf_pR = state->audio_out_bufR + 100;
    state->audio_out_float_buf = aligned_alloc_64(sizeof(float) * 1000000);
    state->audio_out_float_bufR = aligned_alloc_64(sizeof(float) * 1000000);
    memset(state->audio_out_float_buf, 0, 100 * sizeof(float));
    memset(state->audio_out_float_bufR, 0, 100 * sizeof(float));
    state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
    state->audio_out_float_buf_pR = state->audio_out_float_bufR + 100;
    state->audio_out_idx = 0;
    state->audio_out_idx2 = 0;
    state->audio_out_idxR = 0;
    state->audio_out_idx2R = 0;
    state->audio_out_temp_buf_p = state->audio_out_temp_buf;
    state->audio_out_temp_buf_pR = state->audio_out_temp_bufR;
    //state->wav_out_bytes = 0;
    state->center = 0;
    state->jitter = -1;
    state->synctype = DSD_SYNC_NONE;
    state->min = -15000;
    state->max = 15000;
    state->lmid = 0;
    state->umid = 0;
    state->minref = -12000;
    state->maxref = 12000;
    state->lastsample = 0;
    for (i = 0; i < 128; i++) {
        state->sbuf[i] = 0;
    }
    state->sidx = 0;
    for (i = 0; i < 1024; i++) {
        state->maxbuf[i] = 15000;
    }
    for (i = 0; i < 1024; i++) {
        state->minbuf[i] = -15000;
    }
    state->midx = 0;
    state->err_str[0] = '\0';
    state->err_strR[0] = '\0';
    set_spaces(state->fsubtype, 14);
    set_spaces(state->ftype, 13);
    state->symbolcnt = 0;
    state->symbolc = 0; //
    state->rf_mod = 0;
    state->lastsynctype = DSD_SYNC_NONE;
    state->lastp25type = 0;
    state->offset = 0;
    state->carrier = 0;
    for (i = 0; i < 25; i++) {
        for (j = 0; j < 16; j++) {
            state->tg[i][j] = 48;
        }
    }
    state->tgcount = 0;
    state->lasttg = 0;
    state->lastsrc = 0;
    state->lasttgR = 0;
    state->lastsrcR = 0;
    state->gi[0] = -1;
    state->gi[1] = -1;
    state->eh_index = 0;
    state->eh_slot = 2;
    state->nac = 0;
    state->errs = 0;
    state->errs2 = 0;
    state->mbe_file_type = -1;
    state->optind = 0;
    state->numtdulc = 0;
    state->firstframe = 0;
    state->slot1light[0] = '\0';
    state->slot2light[0] = '\0';
    state->aout_gain = 25.0f;
    state->aout_gainR = 25.0f;
    state->aout_gainA = 0.0f; //use purely as a display or internal value, no user setting
    memset(state->aout_max_buf, 0, sizeof(float) * 200);
    state->aout_max_buf_p = state->aout_max_buf;
    state->aout_max_buf_idx = 0;

    memset(state->aout_max_bufR, 0, sizeof(float) * 200);
    state->aout_max_buf_pR = state->aout_max_bufR;
    state->aout_max_buf_idxR = 0;

    state->samplesPerSymbol = 10;
    state->symbolCenter = 4;
    set_underscores(state->algid, 8);
    set_underscores(state->keyid, 16);
    state->currentslot = 0;
    state->cur_mp = malloc(sizeof(mbe_parms));
    state->prev_mp = malloc(sizeof(mbe_parms));
    state->prev_mp_enhanced = malloc(sizeof(mbe_parms));

    state->cur_mp2 = malloc(sizeof(mbe_parms));
    state->prev_mp2 = malloc(sizeof(mbe_parms));
    state->prev_mp_enhanced2 = malloc(sizeof(mbe_parms));

    mbe_initMbeParms(state->cur_mp, state->prev_mp, state->prev_mp_enhanced);
    mbe_initMbeParms(state->cur_mp2, state->prev_mp2, state->prev_mp_enhanced2);
    state->p25kid = 0;

    // Initialize P25 neighbor/candidate UI helpers
    state->p25_nb_count = 0;
    for (int i2 = 0; i2 < 32; i2++) {
        state->p25_nb_freq[i2] = 0;
        state->p25_nb_last_seen[i2] = 0;
    }
    // Clear P25 call flags
    state->p25_call_emergency[0] = state->p25_call_emergency[1] = 0;
    state->p25_call_priority[0] = state->p25_call_priority[1] = 0;

    // Initialize P25 Phase 1 metrics counters (also reset on retune)
    state->p25_p1_fec_ok = 0;
    state->p25_p1_fec_err = 0;
    state->p25_p1_voice_fec_ok = 0;
    state->p25_p1_voice_fec_err = 0;

    state->debug_audio_errors = 0;
    state->debug_audio_errorsR = 0;
    state->debug_header_errors = 0;
    state->debug_header_critical_errors = 0;
    state->debug_mode = 0;

    state->nxdn_last_ran = -1;
    state->nxdn_last_rid = 0;
    state->nxdn_last_tg = 0;
    state->nxdn_cipher_type = 0;
    state->nxdn_key = 0;
    state->nxdn_call_type[0] = '\0';
    state->payload_miN = 0;

    state->dpmr_color_code = -1;

    state->payload_mi = 0;
    state->payload_miR = 0;
    state->payload_mfid = 0;
    state->payload_mfidR = 0;
    state->payload_algid = 0;
    state->payload_algidR = 0;
    state->payload_keyid = 0;
    state->payload_keyidR = 0;

    //init P2 ESS_B fragments and 4V counter
    for (short i = 0; i < 4; i++) {
        state->ess_b[0][i] = 0;
        state->ess_b[1][i] = 0;
    }
    state->fourv_counter[0] = 0;
    state->fourv_counter[1] = 0;
    state->voice_counter[0] = 0;
    state->voice_counter[1] = 0;

    state->K = 0;
    state->R = 0;
    state->RR = 0;
    state->H = 0;
    state->K1 = 0;
    state->K2 = 0;
    state->K3 = 0;
    state->K4 = 0;
    state->M = 0; //force key priority over settings from fid/so

    state->dmr_stereo = 0; //1, or 0?
    state->dmrburstL = 17; //initialize at higher value than possible
    state->dmrburstR = 17; //17 in char array is set for ERR
    state->dmr_so = 0;
    state->dmr_soR = 0;
    state->dmr_fid = 0;
    state->dmr_fidR = 0;
    state->dmr_flco = 0;
    state->dmr_flcoR = 0;
    state->dmr_ms_mode = 0;

    state->HYTL = 0;
    state->HYTR = 0;
    state->DMRvcL = 0;
    state->DMRvcR = 0;
    state->dropL = 256;
    state->dropR = 256;

    state->tyt_ap = 0;
    state->tyt_bp = 0;
    state->tyt_ep = 0;
    state->retevis_ap = 0;

    state->ken_sc = 0;
    state->any_bp = 0;
    state->straight_ks = 0;
    state->straight_mod = 0;

    //ks array storage and counters
    memset(state->ks_octetL, 0, sizeof(state->ks_octetL));
    memset(state->ks_octetR, 0, sizeof(state->ks_octetR));
    memset(state->ks_bitstreamL, 0, sizeof(state->ks_bitstreamL));
    memset(state->ks_bitstreamR, 0, sizeof(state->ks_bitstreamR));
    state->octet_counter = 0;
    state->bit_counterL = 0;
    state->bit_counterR = 0;

    memset(state->static_ks_bits, 0, sizeof(state->static_ks_bits));
    memset(state->static_ks_counter, 0, sizeof(state->static_ks_counter));

    //AES Specific Variables
    memset(state->aes_key, 0, sizeof(state->aes_key));
    memset(state->aes_iv, 0, sizeof(state->aes_iv));
    memset(state->aes_ivR, 0, sizeof(state->aes_ivR));
    memset(state->A1, 0, sizeof(state->A1));
    memset(state->A2, 0, sizeof(state->A2));
    memset(state->A3, 0, sizeof(state->A3));
    memset(state->A4, 0, sizeof(state->A4));
    memset(state->aes_key_loaded, 0, sizeof(state->aes_key_loaded));

    //xl specific, we need to know if the ESS is from HDU, or from LDU2
    state->xl_is_hdu = 0;

    //NXDN, when a new IV has arrived
    state->nxdn_new_iv = 0;

    state->p25vc = 0;
    state->payload_miP = 0;
    state->payload_miN = 0;

    //initialize dmr data header source
    state->dmr_lrrp_source[0] = 0;
    state->dmr_lrrp_source[1] = 0;
    state->dmr_lrrp_target[0] = 0;
    state->dmr_lrrp_target[1] = 0;

    //initialize data header bits
    state->data_header_blocks[0] = 1; //initialize with 1, otherwise we may end up segfaulting when no/bad data header
    state->data_header_blocks[1] = 1; //when trying to fill the superframe and 0-1 blocks give us an overflow
    state->data_header_padding[0] = 0;
    state->data_header_padding[1] = 0;
    state->data_header_format[0] = 7;
    state->data_header_format[1] = 7;
    state->data_header_sap[0] = 0;
    state->data_header_sap[1] = 0;
    state->data_block_counter[0] = 1;
    state->data_block_counter[1] = 1;
    state->data_p_head[0] = 0;
    state->data_p_head[1] = 0;
    state->data_block_poc[0] = 0;
    state->data_block_poc[1] = 0;
    state->data_byte_ctr[0] = 0;
    state->data_byte_ctr[1] = 0;
    state->data_ks_start[0] = 0;
    state->data_ks_start[1] = 0;

    /* menu overlay is now fully async and nonblocking; no demod gating needed */

    state->dmr_encL = 0;
    state->dmr_encR = 0;

    //P2 variables
    state->p2_wacn = 0;
    state->p2_sysid = 0;
    state->p2_cc = 0;
    state->p2_siteid = 0;
    state->p2_rfssid = 0;
    state->p2_hardset = 0;
    state->p2_is_lcch = 0;
    // P25p2 RS metrics
    state->p25_p2_rs_facch_ok = 0;
    state->p25_p2_rs_facch_err = 0;
    state->p25_p2_rs_facch_corr = 0;
    state->p25_p2_rs_sacch_ok = 0;
    state->p25_p2_rs_sacch_err = 0;
    state->p25_p2_rs_sacch_corr = 0;
    state->p25_p2_rs_ess_ok = 0;
    state->p25_p2_rs_ess_err = 0;
    state->p25_p2_rs_ess_corr = 0;
    state->p25_p2_enc_lo_early = 0;
    state->p25_p2_enc_pending[0] = 0;
    state->p25_p2_enc_pending[1] = 0;
    state->p25_p2_enc_pending_ttg[0] = 0;
    state->p25_p2_enc_pending_ttg[1] = 0;
    state->p25_cc_is_tdma =
        2; //init on 2, TSBK NET_STS will set 0, TDMA NET_STS will set 1. //used to determine if we need to change symbol rate when cc hunting

    //experimental symbol file capture read throttle
    state->symbol_throttle = 100; //throttle speed
    state->use_throttle = 0;      //only use throttle if set to 1

    state->p2_scramble_offset = 0;
    state->p2_vch_chan_num = 0;

    //p25 iden_up values
    state->p25_chan_iden = 0;
    for (int i = 0; i < 16; i++) {
        state->p25_chan_type[i] = 0;
        state->p25_trans_off[i] = 0;
        state->p25_chan_spac[i] = 0;
        state->p25_base_freq[i] = 0;
    }

    //values displayed in ncurses terminal
    state->p25_cc_freq = 0;
    state->p25_vc_freq[0] = 0;
    state->p25_vc_freq[1] = 0;

    // Initialize P25 regroup/patch tracking
    state->p25_patch_count = 0;
    for (int p = 0; p < 8; p++) {
        state->p25_patch_sgid[p] = 0;
        state->p25_patch_is_patch[p] = 0;
        state->p25_patch_active[p] = 0;
        state->p25_patch_last_update[p] = 0;
        state->p25_patch_wgid_count[p] = 0;
        state->p25_patch_wuid_count[p] = 0;
        for (int q = 0; q < 8; q++) {
            state->p25_patch_wgid[p][q] = 0;
            state->p25_patch_wuid[p][q] = 0;
        }
        state->p25_patch_key[p] = 0;
        state->p25_patch_alg[p] = 0;
        state->p25_patch_ssn[p] = 0;
    }

    //edacs - may need to make these user configurable instead for stability on non-ea systems
    state->ea_mode = -1; //init on -1, 0 is standard, 1 is ea
    state->edacs_vc_call_type = 0;
    state->esk_mask = 0x0; //esk mask value
    state->edacs_site_id = 0;
    state->edacs_sys_id = 0;
    state->edacs_area_code = 0;
    state->edacs_lcn_count = 0;
    state->edacs_cc_lcn = 0;
    state->edacs_vc_lcn = 0;
    state->edacs_tuned_lcn = -1;
    state->edacs_a_bits = 4;   //  Agency Significant Bits
    state->edacs_f_bits = 4;   //   Fleet Significant Bits
    state->edacs_s_bits = 3;   //Subfleet Significant Bits
    state->edacs_a_shift = 7;  //Calculated Shift for A Bits
    state->edacs_f_shift = 3;  //Calculated Shift for F Bits
    state->edacs_a_mask = 0xF; //Calculated Mask for A Bits
    state->edacs_f_mask = 0xF; //Calculated Mask for F Bits
    state->edacs_s_mask = 0x7; //Calculated Mask for S Bits

    //trunking
    memset(state->trunk_lcn_freq, 0, sizeof(state->trunk_lcn_freq));
    memset(state->trunk_chan_map, 0, sizeof(state->trunk_chan_map));
    state->group_tally = 0;
    state->lcn_freq_count = 0; //number of frequncies imported as an enumerated lcn list
    state->lcn_freq_roll = 0;  //needs reset if sync is found?
    state->last_cc_sync_time = time(NULL);
    state->last_vc_sync_time = time(NULL);
    state->last_active_time = time(NULL);
    state->last_t3_tune_time = time(NULL);
    state->is_con_plus = 0;

    //dmr trunking/ncurses stuff
    state->dmr_rest_channel = -1; //init on -1
    state->dmr_mfid = -1;         //
    state->dmr_cc_lpcn = 0;
    state->tg_hold = 0;

    //new nxdn stuff
    state->nxdn_part_of_frame = 0;
    state->nxdn_ran = 0;
    state->nxdn_sf = 0;
    memset(state->nxdn_sacch_frame_segcrc, 1, sizeof(state->nxdn_sacch_frame_segcrc)); //init on 1, bad CRC all
    state->nxdn_sacch_non_superframe = TRUE;
    memset(state->nxdn_sacch_frame_segment, 1, sizeof(state->nxdn_sacch_frame_segment));
    state->nxdn_alias_block_number = 0;
    memset(state->nxdn_alias_block_segment, 0, sizeof(state->nxdn_alias_block_segment));

    //site/srv/cch info
    state->nxdn_location_site_code = 0;
    state->nxdn_location_sys_code = 0;
    set_spaces(state->nxdn_location_category, 1);

    //channel access information
    state->nxdn_rcn = 0;
    state->nxdn_base_freq = 0;
    state->nxdn_step = 0;
    state->nxdn_bw = 0;

    //multi-key array
    memset(state->rkey_array, 0, sizeof(state->rkey_array));
    state->keyloader = 0; //keyloader off

    //Remus DMR End Call Alert Beep
    state->dmr_end_alert[0] = 0;
    state->dmr_end_alert[1] = 0;

    state->dmr_branding[0] = '\0';
    state->dmr_branding_sub[0] = '\0';
    state->dmr_site_parms[0] = '\0';

    //initialize unified dmr pdu 'superframe'
    memset(state->dmr_pdu_sf, 0, sizeof(state->dmr_pdu_sf));
    memset(state->data_header_valid, 0, sizeof(state->data_header_valid));

    //initialize cap+ bits and block num storage
    memset(state->cap_plus_csbk_bits, 0, sizeof(state->cap_plus_csbk_bits));
    memset(state->cap_plus_block_num, 0, sizeof(state->cap_plus_block_num));

    //init confirmed data individual block crc as invalid
    memset(state->data_block_crc_valid, 0, sizeof(state->data_block_crc_valid));

    //dmr slco stuff
    memset(state->dmr_cach_fragment, 1, sizeof(state->dmr_cach_fragment));
    state->dmr_cach_counter = 0;

    //embedded signalling
    memset(state->dmr_embedded_signalling, 0, sizeof(state->dmr_embedded_signalling));

    //dmr talker alias new/fixed stuff
    memset(state->dmr_alias_format, 0, sizeof(state->dmr_alias_format));
    memset(state->dmr_alias_block_len, 0, sizeof(state->dmr_alias_block_len));
    memset(state->dmr_alias_char_size, 0, sizeof(state->dmr_alias_char_size));
    memset(state->dmr_alias_block_segment, 0, sizeof(state->dmr_alias_block_segment));
    memset(state->dmr_embedded_gps, 0, sizeof(state->dmr_embedded_gps));
    memset(state->dmr_lrrp_gps, 0, sizeof(state->dmr_lrrp_gps));
    memset(state->active_channel, 0, sizeof(state->active_channel));

    //Generic Talker Alias String
    memset(state->generic_talker_alias, 0, sizeof(state->generic_talker_alias));
    state->generic_talker_alias_src[0] = 0;
    state->generic_talker_alias_src[1] = 0;

    //REMUS! multi-purpose call_string
    set_spaces(state->call_string[0], 21);
    set_spaces(state->call_string[1], 21);

    //late entry mi fragments
    memset(state->late_entry_mi_fragment, 0, sizeof(state->late_entry_mi_fragment));

    initialize_p25_heuristics(&state->p25_heuristics);
    initialize_p25_heuristics(&state->inv_p25_heuristics);

    state->dPMRVoiceFS2Frame.CalledIDOk = 0;
    state->dPMRVoiceFS2Frame.CallingIDOk = 0;
    memset(state->dPMRVoiceFS2Frame.CalledID, 0, 8);
    memset(state->dPMRVoiceFS2Frame.CallingID, 0, 8);
    memset(state->dPMRVoiceFS2Frame.Version, 0, 8);

    set_spaces(state->dpmr_caller_id, 6);
    set_spaces(state->dpmr_target_id, 6);

    //YSF Fusion Call Strings
    set_spaces(state->ysf_tgt, 10); //10 spaces
    set_spaces(state->ysf_src, 10); //10 spaces
    set_spaces(state->ysf_upl, 10); //10 spaces
    set_spaces(state->ysf_dnl, 10); //10 spaces
    set_spaces(state->ysf_rm1, 5);  //5 spaces
    set_spaces(state->ysf_rm2, 5);  //5 spaces
    set_spaces(state->ysf_rm3, 5);  //5 spaces
    set_spaces(state->ysf_rm4, 5);  //5 spaces
    memset(state->ysf_txt, 0, sizeof(state->ysf_txt));
    state->ysf_dt = 9;
    state->ysf_fi = 9;
    state->ysf_cm = 9;

    //DSTAR Call Strings
    set_spaces(state->dstar_rpt1, 8); //8 spaces
    set_spaces(state->dstar_rpt2, 8); //8 spaces
    set_spaces(state->dstar_dst, 8);  //8 spaces
    set_spaces(state->dstar_src, 8);  //8 spaces
    set_spaces(state->dstar_txt, 8);  //8 spaces
    set_spaces(state->dstar_gps, 8);  //8 spaces

    //M17 Storage
    memset(state->m17_lsf, 0, sizeof(state->m17_lsf));
    memset(state->m17_pkt, 0, sizeof(state->m17_pkt));
    state->m17_pbc_ct = 0;
    state->m17_str_dt = 9;

    //misc str storage
    //  sprintf (state->str50a, "%s", "");
    memset(state->str50b, 0, 50 * sizeof(char));
    memset(state->str50c, 0, 50 * sizeof(char));
    memset(state->m17sms, 0, 800 * sizeof(char));
    state->m17dat[0] = '\0';

    state->m17_dst = 0;
    state->m17_src = 0;
    state->m17_can = 0;      //can value that was decoded from signal
    state->m17_can_en = -1;  //can value supplied to the encoding side
    state->m17_rate = 48000; //sampling rate for audio input
    state->m17_vox = 0;      //vox mode enabled on M17 encoder
    memset(state->m17_dst_csd, 0, sizeof(state->m17_dst_csd));
    memset(state->m17_src_csd, 0, sizeof(state->m17_src_csd));
    state->m17_dst_str[0] = '\0';
    state->m17_src_str[0] = '\0';

    state->m17_enc = 0;
    state->m17_enc_st = 0;
    state->m17encoder_tx = 0;
    state->m17encoder_eot = 0;
    memset(state->m17_meta, 0, sizeof(state->m17_meta));

#ifdef USE_CODEC2
    state->codec2_3200 = codec2_create(CODEC2_MODE_3200);
    state->codec2_1600 = codec2_create(CODEC2_MODE_1600);
#endif

    state->dmr_color_code = 16;
    state->dmr_t3_syscode = 0;

    // Allocate per-slot event history (2 slots)
    state->event_history_s = calloc(2, sizeof(Event_History_I));

    //debug
    //  fprintf (stderr, "allocated size of event history struct: %ld bytes; \n", 600 * sizeof(Event_History));

    if (state->event_history_s == NULL) {
        LOG_ERROR("memory allocation failure! \n");
    }

    //initialize event history items (0 to 255)
    for (uint8_t i = 0; i < 2; i++) {
        init_event_history(&state->event_history_s[i], 0, 255);
    }

    // Initialize transient UI toast message state
    state->ui_msg[0] = '\0';
    state->ui_msg_expire = 0;

} //init_state

void
freeState(dsd_state* state) {
    if (!state) {
        return;
    }

    dsd_state_ext_free_all(state);

#ifdef USE_CODEC2
    if (state->codec2_3200) {
        codec2_destroy(state->codec2_3200);
        state->codec2_3200 = NULL;
    }
    if (state->codec2_1600) {
        codec2_destroy(state->codec2_1600);
        state->codec2_1600 = NULL;
    }
#endif

    free(state->cur_mp);
    state->cur_mp = NULL;
    free(state->prev_mp);
    state->prev_mp = NULL;
    free(state->prev_mp_enhanced);
    state->prev_mp_enhanced = NULL;

    free(state->cur_mp2);
    state->cur_mp2 = NULL;
    free(state->prev_mp2);
    state->prev_mp2 = NULL;
    free(state->prev_mp_enhanced2);
    state->prev_mp_enhanced2 = NULL;

    free(state->event_history_s);
    state->event_history_s = NULL;

    dsd_aligned_free(state->audio_out_buf);
    state->audio_out_buf = NULL;
    state->audio_out_buf_p = NULL;

    dsd_aligned_free(state->audio_out_bufR);
    state->audio_out_bufR = NULL;
    state->audio_out_buf_pR = NULL;

    dsd_aligned_free(state->audio_out_float_buf);
    state->audio_out_float_buf = NULL;
    state->audio_out_float_buf_p = NULL;

    dsd_aligned_free(state->audio_out_float_bufR);
    state->audio_out_float_bufR = NULL;
    state->audio_out_float_buf_pR = NULL;

    dsd_aligned_free(state->dmr_sample_history);
    state->dmr_sample_history = NULL;
    state->dmr_sample_history_size = 0;
    state->dmr_sample_history_head = 0;
    state->dmr_sample_history_count = 0;

    dsd_aligned_free(state->dibit_buf);
    state->dibit_buf = NULL;
    state->dibit_buf_p = NULL;

    dsd_aligned_free(state->dmr_payload_buf);
    state->dmr_payload_buf = NULL;
    state->dmr_payload_p = NULL;

    free(state->dmr_reliab_buf);
    state->dmr_reliab_buf = NULL;
    state->dmr_reliab_p = NULL;
}
