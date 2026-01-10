// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/csv_import.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/power.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/io/control.h>
#include <dsd-neo/io/rigctl_client.h>
#include <dsd-neo/io/rtl_stream_c.h>
#include <dsd-neo/io/tcp_input.h>
#include <dsd-neo/io/udp_socket_connect.h>
#include <dsd-neo/platform/file_compat.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/ui/menu_services.h>

#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

int
svc_toggle_all_mutes(dsd_opts* opts) {
    if (!opts) {
        return -1;
    }
    opts->unmute_encrypted_p25 = !opts->unmute_encrypted_p25;
    opts->dmr_mute_encL = !opts->dmr_mute_encL;
    opts->dmr_mute_encR = !opts->dmr_mute_encR;
    return 0;
}

int
svc_toggle_call_alert(dsd_opts* opts) {
    if (!opts) {
        return -1;
    }
    opts->call_alert = !opts->call_alert;
    return 0;
}

int
svc_enable_per_call_wav(dsd_opts* opts, dsd_state* state) {
    (void)state;
    if (!opts) {
        return -1;
    }
    char wav_file_directory[1024];
    snprintf(wav_file_directory, sizeof wav_file_directory, "%s", opts->wav_out_dir);
    struct stat st;
    if (stat(wav_file_directory, &st) == -1) {
        LOG_NOTICE("%s wav file directory does not exist\n", wav_file_directory);
        LOG_NOTICE("Creating directory %s to save decoded wav files\n", wav_file_directory);
        dsd_mkdir(wav_file_directory, 0700);
    }
    fprintf(stderr, "\n Per Call Wav File Enabled to Directory: %s;.\n", opts->wav_out_dir);
    srand((unsigned)time(NULL));
    opts->wav_out_f = open_wav_file(opts->wav_out_dir, opts->wav_out_file, 8000, 0);
    opts->wav_out_fR = open_wav_file(opts->wav_out_dir, opts->wav_out_fileR, 8000, 0);
    opts->dmr_stereo_wav = 1;
    return (opts->wav_out_f && opts->wav_out_fR) ? 0 : -1;
}

int
svc_open_symbol_out(dsd_opts* opts, dsd_state* state, const char* filename) {
    if (!opts || !state || !filename || !*filename) {
        return -1;
    }
    snprintf(opts->symbol_out_file, sizeof opts->symbol_out_file, "%s", filename);
    openSymbolOutFile(opts, state);
    return 0;
}

int
svc_open_symbol_in(dsd_opts* opts, dsd_state* state, const char* filename) {
    (void)state;
    if (!opts || !filename || !*filename) {
        return -1;
    }
    opts->symbolfile = fopen(filename, "r");
    if (!opts->symbolfile) {
        LOG_ERROR("Error, couldn't open %s\n", filename);
        return -1;
    }
    dsd_stat_t sb;
    if (dsd_fstat(dsd_fileno(opts->symbolfile), &sb) != 0) {
        LOG_ERROR("Error, couldn't stat %s\n", filename);
        fclose(opts->symbolfile);
        opts->symbolfile = NULL;
        return -1;
    }
    if (!S_ISREG(sb.st_mode)) {
        LOG_ERROR("Error, %s is not a regular file\n", filename);
        fclose(opts->symbolfile);
        opts->symbolfile = NULL;
        return -1;
    }
    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", filename);
    opts->audio_in_type = AUDIO_IN_SYMBOL_BIN; // symbol capture bin
    return 0;
}

int
svc_replay_last_symbol(dsd_opts* opts, dsd_state* state) {
    (void)state;
    if (!opts) {
        return -1;
    }
    opts->symbolfile = fopen(opts->audio_in_dev, "r");
    if (!opts->symbolfile) {
        LOG_ERROR("Error, couldn't open %s\n", opts->audio_in_dev);
        return -1;
    }
    dsd_stat_t sb;
    if (dsd_fstat(dsd_fileno(opts->symbolfile), &sb) != 0) {
        LOG_ERROR("Error, couldn't stat %s\n", opts->audio_in_dev);
        fclose(opts->symbolfile);
        opts->symbolfile = NULL;
        return -1;
    }
    if (!S_ISREG(sb.st_mode)) {
        LOG_ERROR("Error, %s is not a regular file\n", opts->audio_in_dev);
        fclose(opts->symbolfile);
        opts->symbolfile = NULL;
        return -1;
    }
    opts->audio_in_type = AUDIO_IN_SYMBOL_BIN; // symbol capture bin
    return 0;
}

void
svc_stop_symbol_playback(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    if (opts->symbolfile != NULL) {
        if (opts->audio_in_type == AUDIO_IN_SYMBOL_BIN) {
            fclose(opts->symbolfile);
        }
        opts->symbolfile = NULL;
    }
    if (opts->audio_out_type == 0) {
        opts->audio_in_type = AUDIO_IN_PULSE;
    } else {
        opts->audio_in_type = AUDIO_IN_STDIN;
    }
}

void
svc_stop_symbol_saving(dsd_opts* opts, dsd_state* state) {
    if (!opts) {
        return;
    }
    if (opts->symbol_out_f) {
        closeSymbolOutFile(opts, state);
        snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", opts->symbol_out_file);
    }
}

int
svc_tcp_connect_audio(dsd_opts* opts, const char* host, int port) {
    if (!opts || !host || port <= 0) {
        return -1;
    }
    snprintf(opts->tcp_hostname, sizeof opts->tcp_hostname, "%s", host);
    opts->tcp_portno = port;
    opts->tcp_sockfd = Connect(opts->tcp_hostname, opts->tcp_portno);
    if (opts->tcp_sockfd == 0) {
        return -1;
    }
    // Setup TCP audio input (cross-platform)
    opts->audio_in_type = AUDIO_IN_TCP;
    opts->tcp_in_ctx = tcp_input_open(opts->tcp_sockfd, opts->wav_sample_rate);
    if (opts->tcp_in_ctx == NULL) {
        LOG_ERROR("Error, couldn't open TCP audio input\n");
        dsd_socket_close(opts->tcp_sockfd);
        opts->tcp_sockfd = 0;
        if (opts->audio_out_type == 0) {
            snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", "pulse");
            opts->audio_in_type = AUDIO_IN_PULSE;
        } else {
            opts->audio_in_type = AUDIO_IN_STDIN;
        }
        return -1;
    }
    return 0;
}

int
svc_rigctl_connect(dsd_opts* opts, const char* host, int port) {
    if (!opts || !host || port <= 0) {
        return -1;
    }
    snprintf(opts->rigctlhostname, sizeof opts->rigctlhostname, "%s", host);
    opts->rigctlportno = port;
    opts->rigctl_sockfd = Connect(opts->rigctlhostname, opts->rigctlportno);
    if (opts->rigctl_sockfd != 0) {
        opts->use_rigctl = 1;
        return 0;
    }
    opts->use_rigctl = 0;
    return -1;
}

int
svc_lrrp_set_home(dsd_opts* opts) {
    if (!opts) {
        return -1;
    }
    char path[1024];
    if (dsd_config_expand_path("~/lrrp.txt", path, sizeof path) != 0 || !path[0]) {
        return -1;
    }
    snprintf(opts->lrrp_out_file, sizeof opts->lrrp_out_file, "%s", path);
    opts->lrrp_file_output = 1;
    return 0;
}

int
svc_lrrp_set_dsdp(dsd_opts* opts) {
    if (!opts) {
        return -1;
    }
    snprintf(opts->lrrp_out_file, sizeof opts->lrrp_out_file, "%s", "DSDPlus.LRRP");
    opts->lrrp_file_output = 1;
    return 0;
}

int
svc_lrrp_set_custom(dsd_opts* opts, const char* filename) {
    if (!opts || !filename || !*filename) {
        return -1;
    }
    snprintf(opts->lrrp_out_file, sizeof opts->lrrp_out_file, "%s", filename);
    opts->lrrp_file_output = 1;
    return 0;
}

void
svc_lrrp_disable(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->lrrp_file_output = 0;
    opts->lrrp_out_file[0] = 0;
}

void
svc_toggle_inversion(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    int inv = opts->inverted_dmr ? 0 : 1;
    opts->inverted_dmr = inv;
    opts->inverted_dpmr = inv;
    opts->inverted_x2tdma = inv;
    opts->inverted_ysf = inv;
    opts->inverted_m17 = inv;
}

void
svc_reset_event_history(dsd_state* state) {
    if (!state || !state->event_history_s) {
        return;
    }
    for (uint8_t i = 0; i < 2; i++) {
        init_event_history(&state->event_history_s[i], 0, 255);
    }
}

void
svc_toggle_payload(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->payload = !opts->payload;
    fprintf(stderr, opts->payload ? "Payload on\n" : "Payload Off\n");
}

void
svc_set_p2_params(dsd_state* state, unsigned long long wacn, unsigned long long sysid, unsigned long long cc) {
    if (!state) {
        return;
    }
    state->p2_wacn = (wacn > 0xFFFFF) ? 0xFFFFF : wacn;
    state->p2_sysid = (sysid > 0xFFF) ? 0xFFF : sysid;
    state->p2_cc = (cc > 0xFFF) ? 0xFFF : cc;
    state->p2_hardset = (state->p2_wacn != 0 && state->p2_sysid != 0 && state->p2_cc != 0) ? 1 : 0;
}

// Logging & file outputs ----------------------------------------------------
int
svc_set_event_log(dsd_opts* opts, const char* path) {
    if (!opts || !path || !*path) {
        return -1;
    }
    strncpy(opts->event_out_file, path, sizeof opts->event_out_file - 1);
    opts->event_out_file[sizeof opts->event_out_file - 1] = '\0';
    return 0;
}

void
svc_disable_event_log(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->event_out_file[0] = '\0';
}

int
svc_open_static_wav(dsd_opts* opts, dsd_state* state, const char* path) {
    if (!opts || !state || !path || !*path) {
        return -1;
    }
    strncpy(opts->wav_out_file, path, sizeof opts->wav_out_file - 1);
    opts->wav_out_file[sizeof opts->wav_out_file - 1] = '\0';
    opts->dmr_stereo_wav = 0;
    opts->static_wav_file = 1;
    openWavOutFileLR(opts, state);
    return 0;
}

int
svc_open_raw_wav(dsd_opts* opts, dsd_state* state, const char* path) {
    if (!opts || !state || !path || !*path) {
        return -1;
    }
    strncpy(opts->wav_out_file_raw, path, sizeof opts->wav_out_file_raw - 1);
    opts->wav_out_file_raw[sizeof opts->wav_out_file_raw - 1] = '\0';
    openWavOutFileRaw(opts, state);
    return 0;
}

int
svc_set_dsp_output_file(dsd_opts* opts, const char* filename) {
    if (!opts || !filename || !*filename) {
        return -1;
    }
    char dir[1024];
    snprintf(dir, sizeof dir, "./DSP");
    struct stat st;
    if (stat(dir, &st) == -1) {
        dsd_mkdir(dir, 0700);
    }
    snprintf(opts->dsp_out_file, sizeof opts->dsp_out_file, "%s/%s", dir, filename);
    opts->use_dsp_output = 1;
    return 0;
}

// Pulse/UDP helpers ---------------------------------------------------------
int
svc_set_pulse_output(dsd_opts* opts, const char* index) {
    if (!opts || !index) {
        return -1;
    }
    snprintf(opts->audio_out_dev, sizeof opts->audio_out_dev, "%s", "pulse");
    opts->audio_out_type = 0;
    // supply only the part after 'pulse:' to parser
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", index);
    parse_audio_output_string(opts, tmp);
    return 0;
}

int
svc_set_pulse_input(dsd_opts* opts, const char* index) {
    if (!opts || !index) {
        return -1;
    }
    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", "pulse");
    opts->audio_in_type = AUDIO_IN_PULSE;
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", index);
    parse_audio_input_string(opts, tmp);
    return 0;
}

int
svc_udp_output_config(dsd_opts* opts, dsd_state* state, const char* host, int port) {
    if (!opts || !state || !host || port <= 0) {
        return -1;
    }
    strncpy(opts->udp_hostname, host, sizeof opts->udp_hostname - 1);
    opts->udp_hostname[sizeof opts->udp_hostname - 1] = '\0';
    opts->udp_portno = port;
    int err = udp_socket_connect(opts, state);
    if (err < 0) {
        return -1;
    }
    opts->audio_out_type = 8;
    if (opts->monitor_input_audio == 1 || opts->frame_provoice == 1) {
        if (udp_socket_connectA(opts, state) < 0) {
            opts->udp_sockfdA = 0;
        }
    }
    return 0;
}

// Trunking & control --------------------------------------------------------
void
svc_toggle_trunking(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->p25_trunk = !opts->p25_trunk;
    opts->trunk_enable = opts->p25_trunk;
    if (opts->p25_trunk) {
        opts->scanner_mode = 0;
    }
}

void
svc_toggle_scanner(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->scanner_mode = !opts->scanner_mode;
    if (opts->scanner_mode) {
        opts->p25_trunk = 0;
        opts->trunk_enable = 0;
    }
}

int
svc_import_channel_map(dsd_opts* opts, dsd_state* state, const char* path) {
    if (!opts || !state || !path || !*path) {
        return -1;
    }
    strncpy(opts->chan_in_file, path, sizeof opts->chan_in_file - 1);
    opts->chan_in_file[sizeof opts->chan_in_file - 1] = '\0';
    return csvChanImport(opts, state);
}

int
svc_import_group_list(dsd_opts* opts, dsd_state* state, const char* path) {
    if (!opts || !state || !path || !*path) {
        return -1;
    }
    strncpy(opts->group_in_file, path, sizeof opts->group_in_file - 1);
    opts->group_in_file[sizeof opts->group_in_file - 1] = '\0';
    return csvGroupImport(opts, state);
}

int
svc_import_keys_dec(dsd_opts* opts, dsd_state* state, const char* path) {
    if (!opts || !state || !path || !*path) {
        return -1;
    }
    strncpy(opts->key_in_file, path, sizeof opts->key_in_file - 1);
    opts->key_in_file[sizeof opts->key_in_file - 1] = '\0';
    return csvKeyImportDec(opts, state);
}

int
svc_import_keys_hex(dsd_opts* opts, dsd_state* state, const char* path) {
    if (!opts || !state || !path || !*path) {
        return -1;
    }
    strncpy(opts->key_in_file, path, sizeof opts->key_in_file - 1);
    opts->key_in_file[sizeof opts->key_in_file - 1] = '\0';
    return csvKeyImportHex(opts, state);
}

void
svc_toggle_tune_group(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->trunk_tune_group_calls = !opts->trunk_tune_group_calls;
}

void
svc_toggle_tune_private(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->trunk_tune_private_calls = !opts->trunk_tune_private_calls;
}

void
svc_toggle_tune_data(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->trunk_tune_data_calls = !opts->trunk_tune_data_calls;
}

void
svc_set_tg_hold(dsd_state* state, unsigned tg) {
    if (!state) {
        return;
    }
    state->tg_hold = tg;
}

void
svc_set_hangtime(dsd_opts* opts, double seconds) {
    if (!opts) {
        return;
    }
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    opts->trunk_hangtime = seconds;
}

void
svc_set_rigctl_setmod_bw(dsd_opts* opts, int hz) {
    if (!opts) {
        return;
    }
    if (hz < 0) {
        hz = 0;
    }
    if (hz > 25000) {
        hz = 25000;
    }
    opts->setmod_bw = hz;
}

void
svc_toggle_reverse_mute(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->reverse_mute = !opts->reverse_mute;
}

void
svc_toggle_crc_relax(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->aggressive_framesync = opts->aggressive_framesync ? 0 : 1;
}

void
svc_toggle_lcw_retune(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->p25_lcw_retune = !opts->p25_lcw_retune;
}

void
svc_toggle_dmr_le(dsd_opts* opts) {
    if (!opts) {
        return;
    }
    opts->dmr_le = !opts->dmr_le;
}

void
svc_set_slot_pref(dsd_opts* opts, int pref01) {
    if (!opts) {
        return;
    }
    if (pref01 < 0) {
        pref01 = 0;
    }
    if (pref01 > 1) {
        pref01 = 1;
    }
    opts->slot_preference = pref01;
}

void
svc_set_slots_onoff(dsd_opts* opts, int mask) {
    if (!opts) {
        return;
    }
    opts->slot1_on = (mask & 1) ? 1 : 0;
    opts->slot2_on = (mask & 2) ? 1 : 0;
}

// Inversion toggles ---------------------------------------------------------
void
svc_toggle_inv_x2(dsd_opts* opts) {
    if (opts) {
        opts->inverted_x2tdma = !opts->inverted_x2tdma;
    }
}

void
svc_toggle_inv_dmr(dsd_opts* opts) {
    if (opts) {
        opts->inverted_dmr = !opts->inverted_dmr;
    }
}

void
svc_toggle_inv_dpmr(dsd_opts* opts) {
    if (opts) {
        opts->inverted_dpmr = !opts->inverted_dpmr;
    }
}

void
svc_toggle_inv_m17(dsd_opts* opts) {
    if (opts) {
        opts->inverted_m17 = !opts->inverted_m17;
    }
}

#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>

int
svc_rtl_enable_input(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return -1;
    }
    opts->audio_in_type = AUDIO_IN_RTL;
    /* Ensure an RTL stream is ready immediately when switching inputs. */
    return svc_rtl_restart(opts, state);
}

int
svc_rtl_restart(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return -1;
    }
    /* Stop and destroy any existing stream context. */
    if (state->rtl_ctx) {
        rtl_stream_soft_stop(state->rtl_ctx);
        rtl_stream_destroy(state->rtl_ctx);
        state->rtl_ctx = NULL;
    }
    opts->rtl_started = 0;
    opts->rtl_needs_restart = 0;

    /* If RTL-SDR is the active input, immediately recreate and start the stream
       so changes take effect as soon as the user confirms the setting. */
    if (opts->audio_in_type == AUDIO_IN_RTL) {
        if (rtl_stream_create(opts, &state->rtl_ctx) < 0) {
            return -1;
        }
        if (rtl_stream_start(state->rtl_ctx) < 0) {
            rtl_stream_destroy(state->rtl_ctx);
            state->rtl_ctx = NULL;
            return -1;
        }
        opts->rtl_started = 1;
        opts->rtl_needs_restart = 0;
    }
    return 0;
}

int
svc_rtl_set_dev_index(dsd_opts* opts, dsd_state* state, int index) {
    if (!opts || !state) {
        return -1;
    }
    if (index < 0) {
        index = 0;
    }
    opts->rtl_dev_index = index;
    /* Changing device requires reopen */
    opts->rtl_needs_restart = 1;
    if (opts->audio_in_type == AUDIO_IN_RTL) {
        (void)svc_rtl_restart(opts, state);
    }
    return 0;
}

int
svc_rtl_set_freq(dsd_opts* opts, dsd_state* state, uint32_t hz) {
    if (!opts) {
        return -1;
    }
    // Use centralized io/control tuning API for both RTL and rigctl
    return io_control_set_freq(opts, state, (long int)hz);
}

int
svc_rtl_set_gain(dsd_opts* opts, dsd_state* state, int value) {
    if (!opts || !state) {
        return -1;
    }
    if (value < 0) {
        value = 0;
    }
    if (value > 49) {
        value = 49;
    }
    opts->rtl_gain_value = value;
    /* Manual gain change requires reopen to apply */
    opts->rtl_needs_restart = 1;
    if (opts->audio_in_type == AUDIO_IN_RTL) {
        (void)svc_rtl_restart(opts, state);
    }
    return 0;
}

int
svc_rtl_set_ppm(dsd_opts* opts, int ppm) {
    if (!opts) {
        return -1;
    }
    if (ppm < -200) {
        ppm = -200;
    }
    if (ppm > 200) {
        ppm = 200;
    }
    opts->rtlsdr_ppm_error = ppm;
    return 0;
}

int
svc_rtl_set_bandwidth(dsd_opts* opts, dsd_state* state, int khz) {
    if (!opts || !state) {
        return -1;
    }
    if (khz != 4 && khz != 6 && khz != 8 && khz != 12 && khz != 16 && khz != 24 && khz != 48) {
        khz = 48;
    }
    opts->rtl_dsp_bw_khz = khz;
    /* Tuner bandwidth change requires reopen */
    opts->rtl_needs_restart = 1;
    if (opts->audio_in_type == AUDIO_IN_RTL) {
        (void)svc_rtl_restart(opts, state);
    }
    return 0;
}

int
svc_rtl_set_sql_db(dsd_opts* opts, double dB) {
    if (!opts) {
        return -1;
    }
    opts->rtl_squelch_level = dB_to_pwr(dB);
    /* Sync the demod state for channel-based squelching */
    rtl_stream_set_channel_squelch((float)opts->rtl_squelch_level);
    return 0;
}

int
svc_rtl_set_volume_mult(dsd_opts* opts, int mult) {
    if (!opts) {
        return -1;
    }
    if (mult < 0 || mult > 3) {
        mult = 1;
    }
    opts->rtl_volume_multiplier = mult;
    return 0;
}

int
svc_rtl_set_bias_tee(dsd_opts* opts, dsd_state* state, int on) {
    if (!opts) {
        return -1;
    }
    opts->rtl_bias_tee = on ? 1 : 0;
    if (state && state->rtl_ctx) {
        /* Apply live when RTL stream is active */
        return rtl_stream_set_bias_tee(opts->rtl_bias_tee);
    }
    return 0;
}

int
svc_rtltcp_set_autotune(dsd_opts* opts, dsd_state* state, int on) {
    if (!opts) {
        return -1;
    }
    opts->rtltcp_autotune = on ? 1 : 0;
    /* Update env so future restarts inherit */
    dsd_setenv("DSD_NEO_TCP_AUTOTUNE", on ? "1" : "0", 1);
    if (state && state->rtl_ctx) {
        /* Apply live when RTL stream is active */
        rtl_stream_set_rtltcp_autotune(opts->rtltcp_autotune);
    }
    return 0;
}

int
svc_rtl_set_auto_ppm(dsd_opts* opts, dsd_state* state, int on) {
    if (!opts) {
        return -1;
    }
    opts->rtl_auto_ppm = on ? 1 : 0;
    /* Update env for persistence */
    dsd_setenv("DSD_NEO_AUTO_PPM", on ? "1" : "0", 1);
    if (state && state->rtl_ctx) {
        rtl_stream_set_auto_ppm(on ? 1 : 0);
    }
    return 0;
}
#endif
