// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/cli.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/log.h>

#include <dsd-neo/core/csv_import.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/file_compat.h>
#include <dsd-neo/platform/posix_compat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
trim_newline(char* s) {
    if (!s) {
        return;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static int
prompt_yes_no(const char* q, int def_yes) {
    char buf[32];
    fprintf(stderr, "%s [%c/%c]: ", q, def_yes ? 'Y' : 'y', def_yes ? 'n' : 'N');
    if (!fgets(buf, sizeof buf, stdin)) {
        return def_yes;
    }
    trim_newline(buf);
    if (buf[0] == '\0') {
        return def_yes;
    }
    if (buf[0] == 'y' || buf[0] == 'Y') {
        return 1;
    }
    if (buf[0] == 'n' || buf[0] == 'N') {
        return 0;
    }
    return def_yes;
}

static int
prompt_int(const char* q, int def_val, int min_val, int max_val) {
    char buf[64];
    fprintf(stderr, "%s [%d]: ", q, def_val);
    if (!fgets(buf, sizeof buf, stdin)) {
        return def_val;
    }
    trim_newline(buf);
    if (buf[0] == '\0') {
        return def_val;
    }
    char* end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf) {
        return def_val;
    }
    if (v < min_val) {
        v = min_val;
    }
    if (v > max_val) {
        v = max_val;
    }
    return (int)v;
}

static void
prompt_string(const char* q, const char* def_val, char* out, size_t out_sz) {
    char buf[1024];
    if (!out || out_sz == 0) {
        return;
    }
    fprintf(stderr, "%s [%s]: ", q, (def_val && *def_val) ? def_val : "");
    if (!fgets(buf, sizeof buf, stdin)) {
        if (def_val) {
            snprintf(out, out_sz, "%s", def_val);
        } else {
            out[0] = '\0';
        }
        return;
    }
    trim_newline(buf);
    if (buf[0] == '\0') {
        if (def_val) {
            snprintf(out, out_sz, "%s", def_val);
        } else {
            out[0] = '\0';
        }
        return;
    }
    snprintf(out, out_sz, "%s", buf);
}

void
dsd_bootstrap_interactive(dsd_opts* opts, dsd_state* state) {
    if (!dsd_isatty(DSD_STDIN_FILENO) || !dsd_isatty(DSD_STDOUT_FILENO)) {
        // Non-interactive environment: keep defaults
        return;
    }

    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
    if (!cfg) {
        dsd_neo_config_init(opts);
        cfg = dsd_neo_get_config();
    }
    if (cfg && cfg->no_bootstrap_enable) {
        return;
    }

    LOG_NOTICE("No CLI arguments detected — starting interactive setup.\n");
    LOG_NOTICE("Press Enter to accept defaults in [brackets].\n");

    fprintf(stderr, "\nChoose input source:\n");
    fprintf(stderr, "  1) PulseAudio (mic/loopback) [default]\n");
    fprintf(stderr, "  2) RTL-SDR USB dongle\n");
    fprintf(stderr, "  3) rtl_tcp (network RTL-SDR)\n");
    fprintf(stderr, "  4) File (WAV/BIN)\n");
    fprintf(stderr, "  5) TCP audio (7355)\n");
    fprintf(stderr, "  6) UDP audio (7355)\n");
    int src = prompt_int("Selection", 1, 1, 6);

    switch (src) {
        case 2: {
#ifdef USE_RTLSDR
            // RTL-SDR path: rtl:dev:freq:gain:ppm:bw:sql:vol
            char freq[64];
            prompt_string("Center frequency in Hz (K/M/G suffix ok, e.g., 851.375M or 851375000)", "", freq,
                          sizeof freq);
            if (freq[0] == '\0') {
                LOG_WARNING("No frequency entered; falling back to PulseAudio input.\n");
                src = 1;
                break;
            }
            int dev = prompt_int("RTL device index", 0, 0, 255);
            int gain = prompt_int("RTL gain (dB)", 22, 0, 60);
            int ppm = prompt_int("PPM error", 0, -200, 200);
            int bw = prompt_int("DSP bandwidth (kHz: 4,6,8,12,16,24,48)", 48, 4, 48);
            int sql = prompt_int("Squelch (0=off; negative dB ok via CLI later)", 0, -1000, 100000);
            int vol = prompt_int("Volume multiplier (1..3)", 1, 1, 3);
            snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "rtl:%d:%s:%d:%d:%d:%d:%d", dev, freq, gain, ppm,
                     bw, sql, vol);
            break;
#else
            LOG_WARNING("RTL-SDR support not enabled in this build.\n");
            src = 1; // fall back
            break;
#endif
        }
        case 3: {
            // rtl_tcp: rtltcp[:host:port[:freq:gain:ppm:bw:sql:vol]]
            char host[128];
            prompt_string("rtl_tcp host", "127.0.0.1", host, sizeof host);
            int port = prompt_int("rtl_tcp port", 1234, 1, 65535);
            char freq[64];
            prompt_string("Center frequency in Hz (K/M/G suffix ok, optional — Enter to skip)", "", freq, sizeof freq);
            if (freq[0] == '\0') {
                snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "rtltcp:%s:%d", host, port);
            } else {
                int gain = prompt_int("RTL gain (dB)", 22, 0, 60);
                int ppm = prompt_int("PPM error", 0, -200, 200);
                int bw = prompt_int("DSP bandwidth (kHz: 4,6,8,12,16,24,48)", 48, 4, 48);
                int sql = prompt_int("Squelch (0=off)", 0, -1000, 100000);
                int vol = prompt_int("Volume multiplier (1..3)", 1, 1, 3);
                snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "rtltcp:%s:%d:%s:%d:%d:%d:%d:%d", host, port,
                         freq, gain, ppm, bw, sql, vol);
            }
            break;
        }
        case 4: {
            // File input
            char path[1024];
            prompt_string("Path to WAV/BIN/RAW/SYM file", "", path, sizeof path);
            if (path[0] == '\0') {
                LOG_WARNING("No file provided; falling back to PulseAudio input.\n");
                src = 1;
                break;
            }
            // Optional sample rate tweak for WAV/RAW
            int sr = prompt_int("Sample rate for WAV/RAW (48000 or 96000)", 48000, 8000, 192000);
            snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", path);
            if (sr != 48000) {
                opts->wav_sample_rate = sr;
                opts->wav_interpolator = opts->wav_sample_rate / opts->wav_decimator;
                state->samplesPerSymbol = state->samplesPerSymbol * opts->wav_interpolator;
                state->symbolCenter = state->symbolCenter * opts->wav_interpolator;
            }
            break;
        }
        case 5: {
            char host[128];
            prompt_string("TCP host", "127.0.0.1", host, sizeof host);
            int port = prompt_int("TCP port", 7355, 1, 65535);
            snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "tcp:%s:%d", host, port);
            break;
        }
        case 6: {
            char addr[64];
            prompt_string("UDP bind address", "127.0.0.1", addr, sizeof addr);
            int port = prompt_int("UDP port", 7355, 1, 65535);
            snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "udp:%s:%d", addr, port);
            break;
        }
        case 1:
        default: break;
    }

    if (src == 1) {
        LOG_NOTICE("PulseAudio selected; choose devices.\n");
        dsd_bootstrap_choose_audio_input(opts);
        dsd_bootstrap_choose_audio_output(opts);
    }

    // Choose what to decode (default: Auto)
    fprintf(stderr, "\nWhat do you want to decode?\n");
    fprintf(stderr, "  1) Auto (P25, YSF, D-STAR, X2-TDMA, DMR) [default]\n");
    fprintf(stderr, "  2) P25 Phase 1 only\n");
    fprintf(stderr, "  3) P25 Phase 2 only\n");
    fprintf(stderr, "  4) DMR\n");
    fprintf(stderr, "  5) NXDN48 (6.25 kHz)\n");
    fprintf(stderr, "  6) NXDN96 (12.5 kHz)\n");
    fprintf(stderr, "  7) X2-TDMA\n");
    fprintf(stderr, "  8) YSF\n");
    fprintf(stderr, "  9) D-STAR\n");
    fprintf(stderr, " 10) EDACS/ProVoice (std/net)\n");
    fprintf(stderr, " 11) dPMR\n");
    fprintf(stderr, " 12) M17\n");
    fprintf(stderr, " 13) P25 + DMR (TDMA)\n");
    fprintf(stderr, " 14) Analog monitor (passive)\n");
    int mode = prompt_int("Selection", 1, 1, 14);

    // Apply decode mode selection
    switch (mode) {
        case 1: /* Auto: keep init defaults */
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "AUTO");
            break;
        case 2: /* P25 Phase 1 only (-f1) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 1;
            opts->frame_p25p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 0;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            opts->dmr_stereo = 0;
            state->dmr_stereo = 0;
            opts->mod_c4fm = 1;
            opts->mod_qpsk = 0;
            opts->mod_gfsk = 0;
            state->rf_mod = 0;
            opts->dmr_mono = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 1;
            opts->ssize = 36;
            opts->msize = 15;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "P25p1");
            break;
        case 3: /* P25 Phase 2 only (-f2) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 1;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 0;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            state->samplesPerSymbol = 8;
            state->symbolCenter = 3;
            opts->mod_c4fm = 1;
            opts->mod_qpsk = 0;
            opts->mod_gfsk = 0;
            state->rf_mod = 0;
            opts->dmr_stereo = 1;
            state->dmr_stereo = 0;
            opts->dmr_mono = 0;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "P25p2");
            break;
        case 4: /* DMR (-fs) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 0;
            opts->inverted_p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 1;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            opts->mod_c4fm = 1;
            opts->mod_qpsk = 0;
            opts->mod_gfsk = 0;
            state->rf_mod = 0;
            opts->dmr_stereo = 1;
            opts->dmr_mono = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 2;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "DMR");
            break;
        case 5: /* NXDN48 (-fi) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 0;
            opts->frame_nxdn48 = 1;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 0;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            state->samplesPerSymbol = 20;
            state->symbolCenter = 9; /* (sps-1)/2 */
            opts->mod_c4fm = 1;
            opts->mod_qpsk = 0;
            opts->mod_gfsk = 0;
            state->rf_mod = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 1;
            opts->dmr_stereo = 0;
            state->dmr_stereo = 0;
            opts->dmr_mono = 0;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "NXDN48");
            break;
        case 6: /* NXDN96 (-fn) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 1;
            opts->frame_dmr = 0;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            opts->mod_c4fm = 1;
            opts->mod_qpsk = 0;
            opts->mod_gfsk = 0;
            state->rf_mod = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 1;
            opts->dmr_stereo = 0;
            opts->dmr_mono = 0;
            state->dmr_stereo = 0;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "NXDN96");
            break;
        case 7: /* X2-TDMA (-fx) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 1;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 0;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 1;
            opts->dmr_stereo = 0;
            state->dmr_stereo = 0;
            opts->dmr_mono = 0;
            state->rf_mod = 0;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "X2-TDMA");
            break;
        case 8: /* YSF (-fy) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 0;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 0;
            opts->frame_ysf = 1;
            opts->frame_m17 = 0;
            opts->mod_c4fm = 1;
            opts->mod_qpsk = 0;
            opts->mod_gfsk = 0;
            state->rf_mod = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 1;
            opts->dmr_stereo = 0;
            state->dmr_stereo = 0;
            opts->dmr_mono = 0;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "YSF");
            break;
        case 9: /* D-STAR (-fd) */
            opts->frame_dstar = 1;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 0;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 1;
            opts->dmr_stereo = 0;
            state->dmr_stereo = 0;
            opts->dmr_mono = 0;
            state->rf_mod = 0;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "DSTAR");
            break;
        case 10: /* EDACS/ProVoice (std/net) (-fh) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 0;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 1;
            state->ea_mode = 0;
            state->esk_mask = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            state->samplesPerSymbol = 5;
            state->symbolCenter = 2;
            opts->mod_c4fm = 0;
            opts->mod_qpsk = 0;
            opts->mod_gfsk = 1;
            state->rf_mod = 2;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 1;
            opts->dmr_stereo = 0;
            opts->dmr_mono = 0;
            state->dmr_stereo = 0;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "EDACS/PV");
            break;
        case 11: /* dPMR (-fm) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 0;
            opts->frame_provoice = 0;
            opts->frame_dpmr = 1;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            state->samplesPerSymbol = 20; // same as NXDN48
            state->symbolCenter = 9;      // (sps-1)/2, same as NXDN48
            opts->mod_c4fm = 1;
            opts->mod_qpsk = 0;
            opts->mod_gfsk = 0;
            state->rf_mod = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 1;
            opts->dmr_stereo = 0;
            opts->dmr_mono = 0;
            state->dmr_stereo = 0;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "dPMR");
            break;
        case 12: /* M17 (-fz) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 0;
            opts->frame_provoice = 0;
            opts->frame_dpmr = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 1;
            opts->mod_c4fm = 1;
            opts->mod_qpsk = 0;
            opts->mod_gfsk = 0;
            state->rf_mod = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 1;
            opts->dmr_stereo = 0;
            opts->dmr_mono = 0;
            state->dmr_stereo = 0;
            opts->use_cosine_filter = 0; // per -fz note
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "M17");
            break;
        case 13: /* P25 + DMR (TDMA) (-ft) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 1;
            opts->frame_p25p2 = 1;
            opts->inverted_p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 1;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            opts->mod_c4fm = 1;
            opts->mod_qpsk = 0;
            opts->mod_gfsk = 0;
            state->rf_mod = 0;
            opts->dmr_stereo = 1;
            opts->dmr_mono = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 2;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "TDMA");
            break;
        case 14: /* Analog monitor (-fA) */
            opts->frame_dstar = 0;
            opts->frame_x2tdma = 0;
            opts->frame_p25p1 = 0;
            opts->frame_p25p2 = 0;
            opts->frame_nxdn48 = 0;
            opts->frame_nxdn96 = 0;
            opts->frame_dmr = 0;
            opts->frame_dpmr = 0;
            opts->frame_provoice = 0;
            opts->frame_ysf = 0;
            opts->frame_m17 = 0;
            opts->pulse_digi_rate_out = 8000;
            opts->pulse_digi_out_channels = 1;
            opts->dmr_stereo = 0;
            state->dmr_stereo = 0;
            opts->dmr_mono = 0;
            state->rf_mod = 0;
            opts->monitor_input_audio = 1;
            opts->analog_only = 1;
            snprintf(opts->output_name, sizeof opts->output_name, "%s", "Analog Monitor");
            break;
        default: break;
    }

    // Offer trunking toggle when applicable
    int trunk_supported = 0;
    switch (mode) {
        case 1:  // Auto
        case 2:  // P25p1
        case 3:  // P25p2
        case 4:  // DMR
        case 5:  // NXDN48
        case 6:  // NXDN96
        case 10: // EDACS/ProVoice
        case 13: // P25+DMR (TDMA)
            trunk_supported = 1;
            break;
        default: trunk_supported = 0; break;
    }
    if (trunk_supported && (src == 2 || src == 3 || src == 5)) {
        int want_trunk = prompt_yes_no("Is this a trunked system?", 0);
        if (want_trunk) {
            opts->p25_trunk = 1;
            opts->trunk_enable = 1;
            // For TCP audio source, enable rigctl on default SDR++ port to allow tuning
            if (src == 5) {
                if (opts->rigctlportno == 0) {
                    opts->rigctlportno = 4532; // SDR++ default
                }
                opts->use_rigctl = 1;
            }
            LOG_NOTICE("Trunking: Enabled.\n");

            // Optional trunking CSV imports
            // Channel map CSV (channum,freq) — usually required for DMR/EDACS/NXDN Type-C; P25 often learns
            char cpath[1024];
            prompt_string("Channel map CSV path (optional)", "", cpath, sizeof cpath);
            if (cpath[0] != '\0') {
                // Verify file exists before attempting import
                struct stat st;
                if (stat(cpath, &st) == 0 && S_ISREG(st.st_mode)) {
                    strncpy(opts->chan_in_file, cpath, sizeof opts->chan_in_file - 1);
                    opts->chan_in_file[sizeof opts->chan_in_file - 1] = '\0';
                    if (csvChanImport(opts, state) == 0) {
                        LOG_NOTICE("Imported channel map from %s\n", opts->chan_in_file);
                    } else {
                        LOG_WARNING("Failed to import channel map from %s\n", opts->chan_in_file);
                    }
                } else {
                    LOG_WARNING("Channel map file not found: %s — skipping import.\n", cpath);
                }
            }

            // Group list CSV (TG,Mode,Name)
            char gpath[1024];
            prompt_string("Group list CSV path (optional)", "", gpath, sizeof gpath);
            if (gpath[0] != '\0') {
                struct stat stg;
                if (stat(gpath, &stg) == 0 && S_ISREG(stg.st_mode)) {
                    strncpy(opts->group_in_file, gpath, sizeof opts->group_in_file - 1);
                    opts->group_in_file[sizeof opts->group_in_file - 1] = '\0';
                    if (csvGroupImport(opts, state) == 0) {
                        LOG_NOTICE("Imported group list from %s\n", opts->group_in_file);
                    } else {
                        LOG_WARNING("Failed to import group list from %s\n", opts->group_in_file);
                    }
                    // Optional allow-list toggle
                    int use_allow = prompt_yes_no("Use group list as allow/white list?", 0);
                    if (use_allow) {
                        opts->trunk_use_allow_list = 1;
                        LOG_NOTICE("Allow/white list: Enabled.\n");
                    }
                } else {
                    LOG_WARNING("Group list file not found: %s — skipping import.\n", gpath);
                }
            }
        }
    }

    // Output sink quick choice when not using Pulse input helper
    if (src != 1) {
        int use_pulse_out = prompt_yes_no("Use PulseAudio for output?", 1);
        if (use_pulse_out) {
            // Only pick output sink; do not touch input previously chosen
            dsd_bootstrap_choose_audio_output(opts);
        } else {
            int mute = prompt_yes_no("Mute audio output (null sink)?", 0);
            if (mute) {
                snprintf(opts->audio_out_dev, sizeof opts->audio_out_dev, "%s", "null");
            }
        }
    }

    int want_ncurses = prompt_yes_no("Enable ncurses terminal UI (-N)?", 1);
    if (want_ncurses) {
        opts->use_ncurses_terminal = 1;
    }

    LOG_NOTICE("Interactive setup complete.\n");
}
