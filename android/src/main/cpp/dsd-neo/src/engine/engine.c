// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/audio_filters.h>
#include <dsd-neo/core/cleanup.h>
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/csv_import.h>
#include <dsd-neo/core/events.h>
#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/power.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/core/time_format.h>
#include <dsd-neo/core/vocoder.h>
#include <dsd-neo/dsp/frame_sync.h>
#include <dsd-neo/dsp/sps_filters.h>
#include <dsd-neo/engine/engine.h>
#include <dsd-neo/engine/frame_processing.h>
#include <dsd-neo/fec/block_codes.h>
#include <dsd-neo/io/control.h>
#include <dsd-neo/io/rigctl_client.h>
#include <dsd-neo/io/udp_input.h>
#include <dsd-neo/io/udp_socket_connect.h>
#include <dsd-neo/platform/file_compat.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/platform/timing.h>
#include <dsd-neo/protocol/dmr/dmr_trunk_sm.h>
#include <dsd-neo/protocol/m17/m17.h>
#include <dsd-neo/protocol/nxdn/nxdn_convolution.h>
#include <dsd-neo/protocol/nxdn/nxdn_trunk_diag.h>
#include <dsd-neo/protocol/p25/p25_sm_watchdog.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/runtime/cli.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/control_pump.h>
#include <dsd-neo/runtime/exitflag.h>
#include <dsd-neo/runtime/log.h>
#include <dsd-neo/runtime/trunk_cc_candidates.h>
#include <dsd-neo/ui/ui_async.h>

#include <mbelib.h>

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>
#include <rtl-sdr.h>
#endif

#ifdef USE_CODEC2
void codec2_destroy(struct CODEC2* codec2_state);
#endif

// Local caches to avoid redundant device I/O in hot paths
static long int s_last_rigctl_freq = -1;
static int s_last_rigctl_bw = -12345;
#ifdef USE_RTLSDR
static uint32_t s_last_rtl_freq = 0;
#endif

void dsd_engine_frame_sync_hooks_install(void);
void dsd_engine_trunk_tuning_hooks_install(void);
void dsd_engine_rtl_stream_io_hooks_install(void);
void dsd_engine_rtl_stream_metrics_hooks_install(void);
void dsd_engine_rigctl_query_hooks_install(void);
void dsd_engine_net_audio_input_hooks_install(void);
void dsd_engine_udp_audio_hooks_install(void);
void dsd_engine_m17_udp_hooks_install(void);
void dsd_engine_p25_optional_hooks_install(void);

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

static void
autosave_user_config(const dsd_opts* opts, const dsd_state* state) {
    if (!opts || !state) {
        return;
    }
    if (!state->config_autosave_enabled) {
        return;
    }

    const char* path = NULL;
    if (state->config_autosave_path[0] != '\0') {
        path = state->config_autosave_path;
    } else {
        path = dsd_user_config_default_path();
        if (!path || !*path) {
            return;
        }
    }

    dsdneoUserConfig cfg;
    dsd_snapshot_opts_to_user_config(opts, state, &cfg);
    if (dsd_user_config_save_atomic(path, &cfg) == 0) {
        LOG_DEBUG("Autosaved configuration to %s\n", path);
    } else {
        LOG_WARNING("Failed to save configuration to %s\n", path);
    }
}

static int
import_trunking_csvs_if_needed(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return 0;
    }

    const int trunk_or_scan = (opts->trunk_enable == 1) || (opts->p25_trunk == 1) || (opts->scanner_mode == 1);
    const int trunk_enabled = (opts->trunk_enable == 1) || (opts->p25_trunk == 1);

    if (trunk_or_scan && opts->chan_in_file[0] != '\0' && state->lcn_freq_count == 0) {
        if (csvChanImport(opts, state) != 0) {
            return -1;
        }
        LOG_NOTICE("Imported channel map from %s\n", opts->chan_in_file);
    }

    if (trunk_enabled && opts->group_in_file[0] != '\0' && state->group_tally == 0) {
        if (csvGroupImport(opts, state) != 0) {
            return -1;
        }
        LOG_NOTICE("Imported group list from %s\n", opts->group_in_file);
    }
    return 0;
}

static void
open_recording_outputs_if_needed(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return;
    }

    // Per-call WAV (-P) and static WAV (-w) are mutually exclusive.
    if (opts->dmr_stereo_wav == 1) {
        opts->static_wav_file = 0;
    }

    if (opts->dmr_stereo_wav == 1 && opts->wav_out_f == NULL && opts->wav_out_fR == NULL) {
        struct stat st;
        char wav_file_directory[1024];
        snprintf(wav_file_directory, sizeof wav_file_directory, "%s", opts->wav_out_dir);
        wav_file_directory[sizeof wav_file_directory - 1] = '\0';
        if (stat(wav_file_directory, &st) == -1) {
            LOG_NOTICE("Creating directory %s to save decoded wav files\n", wav_file_directory);
            dsd_mkdir(wav_file_directory, 0700);
        }
        srand((unsigned)time(NULL));
        opts->wav_out_f = open_wav_file(opts->wav_out_dir, opts->wav_out_file, 8000, 0);
        opts->wav_out_fR = open_wav_file(opts->wav_out_dir, opts->wav_out_fileR, 8000, 0);
    } else if (opts->static_wav_file == 1 && opts->wav_out_f == NULL && opts->wav_out_file[0] != '\0') {
        openWavOutFileLR(opts, state);
    }

    if (opts->wav_out_file_raw[0] != '\0' && opts->wav_out_raw == NULL) {
        openWavOutFileRaw(opts, state);
    }
}

static int
analog_filter_rate_hz(const dsd_opts* opts, const dsd_state* state) {
    if (!opts) {
        return 48000;
    }
#ifdef USE_RTLSDR
    if (opts->audio_in_type == AUDIO_IN_RTL && state && state->rtl_ctx) {
        uint32_t Fs = rtl_stream_output_rate(state->rtl_ctx);
        if (Fs > 0) {
            return (int)Fs;
        }
    }
#endif
    switch (opts->audio_in_type) {
        case AUDIO_IN_PULSE:
            if (opts->pulse_digi_rate_in > 0) {
                return opts->pulse_digi_rate_in;
            }
            break;
        case AUDIO_IN_STDIN:
        case AUDIO_IN_WAV:
        case AUDIO_IN_UDP:
        case AUDIO_IN_TCP:
            if (opts->wav_sample_rate > 0) {
                return opts->wav_sample_rate;
            }
            break;
        default: break;
    }
    if (opts->pulse_raw_rate_out > 0) {
        return opts->pulse_raw_rate_out;
    }
    return 48000;
}

static void
dsd_engine_signal_handler(int sgnl) {
    UNUSED(sgnl);

    exitflag = 1;
}

static double
atofs(char* s) {
    size_t len = strlen(s);
    if (len == 0) {
        return 0.0;
    }

    char last = s[len - 1];
    double factor = 1.0;

    switch (last) {
        case 'g':
        case 'G': factor = 1e9; break;
        case 'm':
        case 'M': factor = 1e6; break;
        case 'k':
        case 'K': factor = 1e3; break;
        default: return atof(s);
    }

    s[len - 1] = '\0';
    double val = atof(s);
    s[len - 1] = last;
    return val * factor;
}

static void
dsd_engine_start_ui(dsd_opts* opts, dsd_state* state) {
    if (opts->use_ncurses_terminal == 1) {
        (void)ui_start(opts, state);
    }
}

static int
dsd_engine_setup_io(dsd_opts* opts, dsd_state* state) {
    if ((strncmp(opts->audio_in_dev, "m17udp", 6) == 0)) //M17 UDP Socket Input
    {
        LOG_NOTICE("M17 UDP IP Frame Input: ");
        char* curr;
        char* saveptr = NULL;
        char inbuf[1024];
        strncpy(inbuf, opts->audio_in_dev, sizeof(inbuf) - 1);
        inbuf[sizeof(inbuf) - 1] = '\0';

        curr = dsd_strtok_r(inbuf, ":", &saveptr); //should be 'm17'
        if (curr == NULL) {
            goto M17ENDIN; //end early with preset values
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //host address
        if (curr != NULL) {
            strncpy(opts->m17_hostname, curr, 1023);
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //host port
        if (curr != NULL) {
            opts->m17_portno = atoi(curr);
        }

    M17ENDIN:
        LOG_NOTICE("%s:", opts->m17_hostname);
        LOG_NOTICE("%d \n", opts->m17_portno);
    }

    if ((strncmp(opts->audio_in_dev, "udp", 3) == 0)) // UDP Direct Audio Input
    {
        LOG_NOTICE("UDP Direct Input: ");
        char* curr;
        char* saveptr = NULL;
        char inbuf[1024];
        strncpy(inbuf, opts->audio_in_dev, sizeof(inbuf) - 1);
        inbuf[sizeof(inbuf) - 1] = '\0';

        curr = dsd_strtok_r(inbuf, ":", &saveptr); // 'udp'
        if (curr == NULL) {
            goto UDPINEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); // bind address
        if (curr != NULL) {
            strncpy(opts->udp_in_bindaddr, curr, 1023);
            opts->udp_in_bindaddr[1023] = '\0';
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); // bind port
        if (curr != NULL) {
            opts->udp_in_portno = atoi(curr);
        }

    UDPINEND:
        if (opts->udp_in_portno == 0) {
            opts->udp_in_portno = 7355;
        }
        if (opts->udp_in_bindaddr[0] == '\0') {
            snprintf(opts->udp_in_bindaddr, sizeof(opts->udp_in_bindaddr), "%s", "127.0.0.1");
        }
        LOG_NOTICE("%s:%d\n", opts->udp_in_bindaddr, opts->udp_in_portno);
    }

    if ((strncmp(opts->audio_out_dev, "m17udp", 6) == 0)) //M17 UDP Socket Output
    {
        LOG_NOTICE("M17 UDP IP Frame Output: ");
        char* curr;
        char* saveptr = NULL;
        char outbuf[1024];
        strncpy(outbuf, opts->audio_out_dev, sizeof(outbuf) - 1);
        outbuf[sizeof(outbuf) - 1] = '\0';

        curr = dsd_strtok_r(outbuf, ":", &saveptr); //should be 'm17'
        if (curr == NULL) {
            goto M17ENDOUT; //end early with preset values
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //host address
        if (curr != NULL) {
            strncpy(opts->m17_hostname, curr, 1023);
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //host port
        if (curr != NULL) {
            opts->m17_portno = atoi(curr);
        }

    M17ENDOUT:
        LOG_NOTICE("%s:", opts->m17_hostname);
        LOG_NOTICE("%d \n", opts->m17_portno);
        opts->m17_use_ip = 1;     //tell the encoder to open the socket
        opts->audio_out_type = 9; //set to null device
    }

    if ((strncmp(opts->audio_in_dev, "tcp", 3) == 0)) //tcp socket input from SDR++ and others
    {
        LOG_NOTICE("TCP Direct Link: ");
        char* curr;
        char* saveptr = NULL;
        char inbuf[1024];
        strncpy(inbuf, opts->audio_in_dev, sizeof(inbuf) - 1);
        inbuf[sizeof(inbuf) - 1] = '\0';

        curr = dsd_strtok_r(inbuf, ":", &saveptr); //should be 'tcp'
        if (curr == NULL) {
            goto TCPEND; //end early with preset values
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //host address
        if (curr != NULL) {
            strncpy(opts->tcp_hostname, curr, 1023);
            //shim to tie the hostname of the tcp input to the rigctl hostname (probably covers a vast majority of use cases)
            //in the future, I will rework part of this so that users can enter a hostname and port similar to how tcp and rtl strings work
            memcpy(opts->rigctlhostname, opts->tcp_hostname, sizeof(opts->rigctlhostname));
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //host port
        if (curr != NULL) {
            opts->tcp_portno = atoi(curr);
        }

    TCPEND:
        if (exitflag == 1) {
            cleanupAndExit(opts, state);
            return 0;
        }
        LOG_NOTICE("%s:", opts->tcp_hostname);
        LOG_NOTICE("%d \n", opts->tcp_portno);
        opts->tcp_sockfd = Connect(opts->tcp_hostname, opts->tcp_portno);
        if (opts->tcp_sockfd != DSD_INVALID_SOCKET) {
            opts->audio_in_type = AUDIO_IN_TCP;

            LOG_NOTICE("TCP Connection Success!\n");
            // openAudioInDevice(opts); //do this to see if it makes it work correctly
        } else {
            if (opts->frame_m17 == 1) {
                dsd_sleep_ms(1000);
                goto TCPEND; //try again if using M17 encoder / decoder over TCP
            }
            sprintf(opts->audio_in_dev, "%s", "pulse");
            LOG_ERROR("TCP Connection Failure - Using %s Audio Input.\n", opts->audio_in_dev);
            opts->audio_in_type = AUDIO_IN_PULSE;
        }
    }

    if (opts->use_rigctl == 1) {
        opts->rigctl_sockfd = Connect(opts->rigctlhostname, opts->rigctlportno);
        if (opts->rigctl_sockfd != DSD_INVALID_SOCKET) {
            opts->use_rigctl = 1;
        } else {
            LOG_ERROR("RIGCTL Connection Failure - RIGCTL Features Disabled\n");
            opts->use_rigctl = 0;
        }
    }

    if ((strncmp(opts->audio_in_dev, "rtltcp", 6) == 0)) // rtl_tcp networked RTL-SDR
    {
        LOG_NOTICE("RTL_TCP Input: ");
        char* curr;
        char* saveptr = NULL;
        char inbuf[1024];
        strncpy(inbuf, opts->audio_in_dev, sizeof(inbuf) - 1);
        inbuf[sizeof(inbuf) - 1] = '\0';

        curr = dsd_strtok_r(inbuf, ":", &saveptr); // 'rtltcp'
        if (curr == NULL) {
            goto RTLTCPEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); // host
        if (curr != NULL) {
            strncpy(opts->rtltcp_hostname, curr, 1023);
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); // port
        if (curr != NULL) {
            opts->rtltcp_portno = atoi(curr);
        }

        // Optional: freq:gain:ppm:bw:sql:vol (mirrors rtl: string semantics)
        curr = dsd_strtok_r(NULL, ":", &saveptr); // freq
        if (curr != NULL) {
            opts->rtlsdr_center_freq = (uint32_t)atofs(curr);
        } else {
            goto RTLTCPEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); // gain
        if (curr != NULL) {
            opts->rtl_gain_value = atoi(curr);
        } else {
            goto RTLTCPEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); // ppm
        if (curr != NULL) {
            opts->rtlsdr_ppm_error = atoi(curr);
        } else {
            goto RTLTCPEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); // bw (kHz)
        if (curr != NULL) {
            int bw = atoi(curr);
            if (bw == 4 || bw == 6 || bw == 8 || bw == 12 || bw == 16 || bw == 24 || bw == 48) {
                opts->rtl_dsp_bw_khz = bw;
            } else {
                opts->rtl_dsp_bw_khz = 48;
            }
        } else {
            goto RTLTCPEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); // sql (dB if negative; else linear)
        if (curr != NULL) {
            double sq_val = atof(curr);
            if (sq_val < 0.0) {
                opts->rtl_squelch_level = dB_to_pwr(sq_val);
            } else {
                opts->rtl_squelch_level = sq_val;
            }
        } else {
            goto RTLTCPEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); // vol (1..3)
        if (curr != NULL) {
            opts->rtl_volume_multiplier = atoi(curr);
        } else {
            goto RTLTCPEND;
        }

        // Optional trailing tokens: bias tee toggle
        while ((curr = dsd_strtok_r(NULL, ":", &saveptr)) != NULL) {
            if (strncmp(curr, "bias", 4) == 0 || strncmp(curr, "b", 1) == 0) {
                const char* val = strchr(curr, '=');
                int on = 1; // default enable if no explicit value
                if (val && *(val + 1)) {
                    val++; // move past '='
                    if (*val == '0' || *val == 'n' || *val == 'N' || *val == 'o' || *val == 'O' || *val == 'f'
                        || *val == 'F') {
                        on = 0;
                    }
                }
                opts->rtl_bias_tee = on;
            }
        }

    RTLTCPEND:
        if (opts->rtltcp_portno == 0) {
            opts->rtltcp_portno = 1234;
        }
        LOG_NOTICE("%s:%d", opts->rtltcp_hostname, opts->rtltcp_portno);
        if (opts->rtl_bias_tee) {
            LOG_NOTICE(" (bias=on)\n");
        } else {
            LOG_NOTICE("\n");
        }
        opts->rtltcp_enabled = 1;
        opts->audio_in_type = AUDIO_IN_RTL; // use RTL pipeline
    }

    // NOTE: Guard against matching "rtltcp" here; it shares the "rtl" prefix
    // and opts->audio_in_dev has been tokenized by strtok above. Without this
    // guard, selecting rtltcp would also fall through to the local RTL path
    // and erroneously require a USB device, causing an early exit.
    if ((strncmp(opts->audio_in_dev, "rtl", 3) == 0)
        && (strncmp(opts->audio_in_dev, "rtltcp", 6) != 0)) //rtl dongle input
    {
        uint8_t rtl_ok = 0;
        //use to list out all detected RTL dongles
        char vendor[256], product[256], serial[256];
        int device_count = 0;

#ifdef USE_RTLSDR
        LOG_NOTICE("RTL Input: ");
        char* curr;
        char* saveptr = NULL;
        char inbuf[1024];
        strncpy(inbuf, opts->audio_in_dev, sizeof(inbuf) - 1);
        inbuf[sizeof(inbuf) - 1] = '\0';

        curr = dsd_strtok_r(inbuf, ":", &saveptr); //should be 'rtl'
        if (curr == NULL) {
            goto RTLEND; //end early with preset values
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //rtl device number "-D"
        if (curr != NULL) {
            opts->rtl_dev_index = atoi(curr);
        } else {
            goto RTLEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //rtl freq "-c"
        if (curr != NULL) {
            opts->rtlsdr_center_freq = (uint32_t)atofs(curr);
        } else {
            goto RTLEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //rtl gain value "-G"
        if (curr != NULL) {
            opts->rtl_gain_value = atoi(curr);
        } else {
            goto RTLEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //rtl ppm err "-P"
        if (curr != NULL) {
            opts->rtlsdr_ppm_error = atoi(curr);
        } else {
            goto RTLEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //rtl bandwidth "-Y"
        if (curr != NULL) {
            int bw = 0;
            bw = atoi(curr);
            //check for proper values (4,6,8,12,16,24,48)
            if (bw == 4 || bw == 6 || bw == 8 || bw == 12 || bw == 16 || bw == 24
                || bw == 48) // testing 4 and 16 as well for weak and/or nxdn48 systems
            {
                opts->rtl_dsp_bw_khz = bw;
            } else {
                opts->rtl_dsp_bw_khz = 48; // default baseband when input omits/invalid
            }
        } else {
            goto RTLEND;
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //rtl squelch threshold (dB if negative; else linear)
        if (curr != NULL) {
            double sq_val = atof(curr);
            if (sq_val < 0.0) {
                opts->rtl_squelch_level = dB_to_pwr(sq_val);
            } else {
                opts->rtl_squelch_level = sq_val;
            }
        } else {
            goto RTLEND;
        }

        // curr = dsd_strtok_r(NULL, ":", &saveptr); //rtl udp port "-U"
        // if (curr != NULL) opts->rtl_udp_port = atoi (curr);
        // else goto RTLEND;

        curr = dsd_strtok_r(NULL, ":", &saveptr); //rtl sample / volume multiplier
        if (curr != NULL) {
            opts->rtl_volume_multiplier = atoi(curr);
        } else {
            goto RTLEND;
        }

        // Optional trailing tokens: bias tee toggle
        while ((curr = dsd_strtok_r(NULL, ":", &saveptr)) != NULL) {
            if (strncmp(curr, "bias", 4) == 0 || strncmp(curr, "b", 1) == 0) {
                const char* val = strchr(curr, '=');
                int on = 1; // default enable if no explicit value
                if (val && *(val + 1)) {
                    val++; // move past '='
                    if (*val == '0' || *val == 'n' || *val == 'N' || *val == 'o' || *val == 'O' || *val == 'f'
                        || *val == 'F') {
                        on = 0;
                    }
                }
                opts->rtl_bias_tee = on;
            }
        }

    RTLEND:

#if defined(_MSC_VER) && defined(_WIN32)
        __try {
            device_count = (int)rtlsdr_get_device_count();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            LOG_ERROR("RTL: libusb exception during device enumeration.\n");
            device_count = 0;
            exitflag = 1;
        }
#else
        device_count = (int)rtlsdr_get_device_count();
#endif
        if (!device_count) {
            LOG_ERROR("No supported devices found.\n");
            exitflag = 1;
        } else {
            LOG_NOTICE("Found %d device(s):\n", device_count);
        }
        for (int i = 0; i < device_count; i++) {
#if defined(_MSC_VER) && defined(_WIN32)
            __try {
                (void)rtlsdr_get_device_usb_strings((uint32_t)i, vendor, product, serial);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                snprintf(vendor, sizeof(vendor), "%s", "unknown");
                snprintf(product, sizeof(product), "%s", "unknown");
                snprintf(serial, sizeof(serial), "%s", "unknown");
            }
#else
            (void)rtlsdr_get_device_usb_strings((uint32_t)i, vendor, product, serial);
#endif
            LOG_NOTICE("  %d:  %s, %s, SN: %s\n", i, vendor, product, serial);
            if (opts->rtl_dev_index == i) {
                LOG_NOTICE("Selected Device #%d with Serial Number: %s \n", i, serial);
            }
        }

        // Guard against out-of-range index (indices are 0-based, not serial numbers)
        if (opts->rtl_dev_index < 0 || opts->rtl_dev_index >= device_count) {
            const int requested = opts->rtl_dev_index;
            LOG_WARNING("Requested RTL device index %d out of range (found %d device(s)); using 0\n", requested,
                        device_count);
            opts->rtl_dev_index = 0;
            // Keep the input string consistent with the effective device selection
            if (strncmp(opts->audio_in_dev, "rtl:", 4) == 0) {
                const char* rest = strchr(opts->audio_in_dev + 4, ':');
                if (rest) {
                    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "rtl:%d%s", opts->rtl_dev_index, rest);
                } else {
                    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "rtl:%d", opts->rtl_dev_index);
                }
                opts->audio_in_dev[sizeof opts->audio_in_dev - 1] = '\0';
            }
        }

        if (opts->rtl_volume_multiplier > 3 || opts->rtl_volume_multiplier < 0) {
            opts->rtl_volume_multiplier = 1; //I wonder if you could flip polarity by using -1
        }

        LOG_NOTICE("RTL #%d: Freq=%d Gain=%d PPM=%d DSP-BW=%dkHz SQ=%.1fdB VOL=%d%s\n", opts->rtl_dev_index,
                   opts->rtlsdr_center_freq, opts->rtl_gain_value, opts->rtlsdr_ppm_error, opts->rtl_dsp_bw_khz,
                   pwr_to_dB(opts->rtl_squelch_level), opts->rtl_volume_multiplier,
                   opts->rtl_bias_tee ? " BIAS=on" : "");
        opts->audio_in_type = AUDIO_IN_RTL;

        rtl_ok = 1;
#endif

        if (rtl_ok == 0) //not set, means rtl support isn't compiled/available
        {
            LOG_ERROR("RTL Support not enabled/compiled, falling back to Pulse Audio Input.\n");
            sprintf(opts->audio_in_dev, "%s", "pulse");
            opts->audio_in_type = AUDIO_IN_PULSE;
        }
        UNUSED(vendor);
        UNUSED(product);
        UNUSED(serial);
        UNUSED(device_count);
    }

    if ((strncmp(opts->audio_in_dev, "pulse", 5) == 0)) {
        opts->audio_in_type = AUDIO_IN_PULSE;

        //string yeet
        parse_audio_input_string(opts, opts->audio_in_dev + 5);
    }

    //UDP Socket Blaster Audio Output Setup
    if ((strncmp(opts->audio_out_dev, "udp", 3) == 0)) {

        //read in values
        LOG_NOTICE("UDP Blaster Output: ");
        char* curr;
        char* saveptr = NULL;
        char outbuf[1024];
        strncpy(outbuf, opts->audio_out_dev, sizeof(outbuf) - 1);
        outbuf[sizeof(outbuf) - 1] = '\0';

        curr = dsd_strtok_r(outbuf, ":", &saveptr); //should be 'udp'
        if (curr == NULL) {
            goto UDPEND; //end early with preset values
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //udp blaster hostname
        if (curr != NULL) {
            strncpy(opts->udp_hostname, curr, 1023); //set address to blast to
        }

        curr = dsd_strtok_r(NULL, ":", &saveptr); //udp blaster port
        if (curr != NULL) {
            opts->udp_portno = atoi(curr);
        }

    UDPEND:
        LOG_NOTICE("%s:", opts->udp_hostname);
        LOG_NOTICE("%d \n", opts->udp_portno);

        int err = udp_socket_connect(opts, state);
        if (err < 0) {
            LOG_ERROR("Error Configuring UDP Socket for UDP Blaster Audio :( \n");
            sprintf(opts->audio_out_dev, "%s", "pulse");
            opts->audio_out_type = 0;
        }

        opts->audio_out_type = 8;

        if (opts->monitor_input_audio == 1 || opts->frame_provoice == 1) {
            err = udp_socket_connectA(opts, state);
            if (err < 0) {
                LOG_ERROR("Error Configuring UDP Socket for UDP Blaster Audio Analog :( \n");
                opts->udp_sockfdA = DSD_INVALID_SOCKET;
                opts->monitor_input_audio = 0;
            } else {
                LOG_NOTICE("UDP Blaster Output (Analog): ");
                LOG_NOTICE("%s:", opts->udp_hostname);
                LOG_NOTICE("%d \n", opts->udp_portno + 2);
            }

            //this functionality is disabled when trunking EDACS, but we still use the behavior for analog channel monitoring
            if (opts->frame_provoice == 1 && opts->p25_trunk == 1) {
                opts->monitor_input_audio = 0;
            }
        }
    }

    if ((strncmp(opts->audio_out_dev, "pulse", 5) == 0)) {
        opts->audio_out_type = 0;

        //string yeet
        parse_audio_output_string(opts, opts->audio_out_dev + 5);
    }

    if ((strncmp(opts->audio_out_dev, "null", 4) == 0)) {
        opts->audio_out_type = 9; //9 for NULL, or mute output
        opts->audio_out = 0;      //turn off so we won't playSynthesized
    }

    if ((strncmp(opts->audio_out_dev, "-", 1) == 0)) {
        opts->audio_out_fd = dsd_fileno(stdout); //DSD_STDOUT_FILENO;
        opts->audio_out_type = 1;                //using 1 for stdout to match input stdin as 1
        LOG_NOTICE("Audio Out Device: -\n");
    }

    if (opts->playfiles == 1) {
        opts->split = 1;
        opts->playoffset = 0;
        opts->playoffsetR = 0;
        opts->delay = 0;
        opts->pulse_digi_rate_out = 8000;
        opts->pulse_digi_out_channels = 1;
        if (opts->audio_out_type == 0) {
            if (openAudioOutput(opts) != 0) {
                return -1;
            }
        }
    }

    //this particular if-elseif-else could be rewritten to be a lot neater and simpler
    else if (strcmp(opts->audio_in_dev, opts->audio_out_dev) != 0) {
        opts->split = 1;
        opts->playoffset = 0;
        opts->playoffsetR = 0;
        opts->delay = 0;

        if (openAudioInDevice(opts) != 0) {
            return -1;
        }
    }

    else {
        opts->split = 0;
        opts->playoffset = 0;
        opts->playoffsetR = 0;
        opts->delay = 0;
        if (openAudioInDevice(opts) != 0) {
            return -1;
        }
    }
    return 0;
}

static void
dsd_engine_parse_m17_userdata(dsd_opts* opts, dsd_state* state) {
    UNUSED(opts);

    //read in any user supplied M17 CAN and/or CSD data
    if ((strncmp(state->m17dat, "M17", 3) == 0)) {
        //read in values
        //string in format of M17:can:src_csd:dst_csd:input_rate

        //check and capatalize any letters in the CSD
        for (int i = 0; state->m17dat[i] != '\0'; i++) {
            if (state->m17dat[i] >= 'a' && state->m17dat[i] <= 'z') {
                state->m17dat[i] = state->m17dat[i] - 32;
            }
        }

        LOG_NOTICE("M17 User Data: ");
        char* curr;

        // if((strncmp(state->m17dat, "M17", 3) == 0))
        // goto M17END;

        curr = strtok(state->m17dat, ":"); //should be 'M17'
        if (curr != NULL)
            ; //continue
        else {
            goto M17END; //end early with preset values
        }

        curr = strtok(NULL, ":"); //m17 channel access number
        if (curr != NULL) {
            state->m17_can_en = atoi(curr);
        }

        curr = strtok(NULL, ":"); //m17 src address
        if (curr != NULL) {
            strncpy(state->str50c, curr, 9); //only read first 9
            state->str50c[9] = '\0';
        }

        curr = strtok(NULL, ":"); //m17 dst address
        if (curr != NULL) {
            strncpy(state->str50b, curr, 9); //only read first 9
            state->str50b[9] = '\0';
        }

        curr = strtok(NULL, ":"); //m17 input audio rate
        if (curr != NULL) {
            state->m17_rate = atoi(curr);
        }

        curr = strtok(NULL, ":"); //m17 vox enable
        if (curr != NULL) {
            state->m17_vox = atoi(curr);
        }

    M17END:; //do nothing

        //check to make sure can value is no greater than 15 (4 bit value)
        if (state->m17_can_en > 15) {
            state->m17_can_en = 15;
        }

        //if vox is greater than 1, assume user meant 'yes' and set to one
        if (state->m17_vox > 1) {
            state->m17_vox = 1;
        }

        //debug print m17dat string
        // fprintf (stderr, " %s;", state->m17dat);

        LOG_NOTICE(" M17:%d:%s:%s:%d;", state->m17_can_en, state->str50c, state->str50b, state->m17_rate);
        if (state->m17_vox == 1) {
            LOG_NOTICE("VOX;");
        }
        LOG_NOTICE("\n");
    }
}

void
noCarrier(dsd_opts* opts, dsd_state* state) {
    const time_t now = time(NULL);

    //when no carrier sync, rotate the symbol out file every hour, if enabled
    if (opts->symbol_out_f && opts->symbol_out_file_is_auto == 1) {
        rotate_symbol_out_file(opts, state);
    }

    if (opts->floating_point == 1) {
        state->aout_gain = opts->audio_gain;
        state->aout_gainR = opts->audio_gain;
    }

    //clear heuristics from last carrier signal
    if (opts->frame_p25p1 == 1 && opts->use_heuristics == 1) {
        initialize_p25_heuristics(&state->p25_heuristics);
        initialize_p25_heuristics(&state->inv_p25_heuristics);
    }

//only do it here on the tweaks
#ifdef LIMAZULUTWEAKS
    state->nxdn_last_ran = -1;
    state->nxdn_last_rid = 0;
    state->nxdn_last_tg = 0;
#endif

    //experimental conventional frequency scanner mode
    if (opts->scanner_mode == 1 && ((now - state->last_cc_sync_time) > opts->trunk_hangtime)) {

        //always do these -- makes sense during scanning
        state->nxdn_last_ran = -1; //
        state->nxdn_last_rid = 0;
        state->nxdn_last_tg = 0;

        if (state->lcn_freq_roll >= state->lcn_freq_count) {
            state->lcn_freq_roll = 0; //reset to zero
        }
        //check that we have a non zero value first, then tune next frequency
        if (state->trunk_lcn_freq[state->lcn_freq_roll] != 0) {
            //rigctl
            if (opts->use_rigctl == 1) {
                if (opts->setmod_bw != 0 && opts->setmod_bw != s_last_rigctl_bw) {
                    SetModulation(opts->rigctl_sockfd, opts->setmod_bw);
                    s_last_rigctl_bw = opts->setmod_bw;
                }
                long int f = state->trunk_lcn_freq[state->lcn_freq_roll];
                if (f != s_last_rigctl_freq) {
                    SetFreq(opts->rigctl_sockfd, f);
                    s_last_rigctl_freq = f;
                }
            }
            //rtl
            if (opts->audio_in_type == AUDIO_IN_RTL) {
#ifdef USE_RTLSDR
                if (state->rtl_ctx) {
                    uint32_t rf = (uint32_t)state->trunk_lcn_freq[state->lcn_freq_roll];
                    if (rf != s_last_rtl_freq) {
                        rtl_stream_tune(state->rtl_ctx, rf);
                        s_last_rtl_freq = rf;
                    }
                }
#endif
            }
        }
        state->lcn_freq_roll++;
        state->last_cc_sync_time = now;
    }
    //end experimental conventional frequency scanner mode

    // Tune back to last known CC when using trunking after hangtime expires.
    // Use VC activity when currently tuned to a VC; otherwise use CC timer.
    if (opts->p25_trunk == 1 && (opts->trunk_is_tuned == 1 || opts->p25_is_tuned == 1)) {
        double dt;
        if (opts->p25_is_tuned == 1) {
            // On a voice channel: gate return by recent voice activity
            if (state->last_vc_sync_time == 0) {
                dt = 1e9; // no activity recorded; treat as expired
            } else {
                dt = (double)(now - state->last_vc_sync_time);
            }
        } else {
            // On control or idle: use CC timer
            if (state->last_cc_sync_time == 0) {
                dt = 1e9;
            } else {
                dt = (double)(now - state->last_cc_sync_time);
            }
        }

        if (dt > opts->trunk_hangtime) {
            long cc = (state->trunk_cc_freq != 0) ? state->trunk_cc_freq : state->p25_cc_freq;
            if (cc != 0) {

                //cap+ rest channel - redundant?
                if (state->dmr_rest_channel != -1) {
                    if (state->trunk_chan_map[state->dmr_rest_channel] != 0) {
                        cc = state->trunk_chan_map[state->dmr_rest_channel];
                        state->p25_cc_freq = cc;
                        state->trunk_cc_freq = cc;
                    }
                }

                if (opts->use_rigctl == 1) //rigctl tuning
                {
                    if (opts->setmod_bw != 0 && opts->setmod_bw != s_last_rigctl_bw) {
                        SetModulation(opts->rigctl_sockfd, opts->setmod_bw);
                        s_last_rigctl_bw = opts->setmod_bw;
                    }
                    if (cc != s_last_rigctl_freq) {
                        SetFreq(opts->rigctl_sockfd, cc);
                        s_last_rigctl_freq = cc;
                    }
                    state->dmr_rest_channel = -1; //maybe?
                }
                //rtl
                else if (opts->audio_in_type == AUDIO_IN_RTL) {
#ifdef USE_RTLSDR
                    if (state->rtl_ctx) {
                        uint32_t rf = (uint32_t)cc;
                        if (rf != s_last_rtl_freq) {
                            rtl_stream_tune(state->rtl_ctx, rf);
                            s_last_rtl_freq = rf;
                        }
                    }
                    state->dmr_rest_channel = -1;
#endif
                }

                opts->p25_is_tuned = 0;
                state->edacs_tuned_lcn = -1;

                state->last_cc_sync_time = now;
                //test to switch back to 10/4 P1 QPSK for P25 FDMA CC

                //if P25p2 VCH and going back to P25p1 CC, flip symbolrate
                if (state->p25_cc_is_tdma == 0) //is set on signal from P25 TSBK or MAC_SIGNAL
                {
                    state->samplesPerSymbol = 10;
                    state->symbolCenter = 4;
                    //re-enable both slots
                    opts->slot1_on = 1;
                    opts->slot2_on = 1;
                }
                //if P25p1 SNDCP channel (or revert) and going to a P25 TDMA CC
                else if (state->p25_cc_is_tdma == 1) {
                    state->samplesPerSymbol = 8;
                    state->symbolCenter = 3;
                    //re-enable both slots (in case of late entry voice, MAC_SIGNAL can turn them back off)
                    opts->slot1_on = 1;
                    opts->slot2_on = 1;
                }
            }
            //zero out vc frequencies?
            state->p25_vc_freq[0] = 0;
            state->p25_vc_freq[1] = 0;

            memset(state->active_channel, 0, sizeof(state->active_channel));

            state->is_con_plus = 0; //flag off
        }
    }

    state->dibit_buf_p = state->dibit_buf + 200;
    memset(state->dibit_buf, 0, sizeof(int) * 200);
    //dmr buffer
    state->dmr_payload_p = state->dmr_payload_buf + 200;
    memset(state->dmr_payload_buf, 0, sizeof(int) * 200);
    memset(state->dmr_stereo_payload, 1, sizeof(int) * 144);
    if (state->dmr_reliab_buf) {
        state->dmr_reliab_p = state->dmr_reliab_buf + 200;
        memset(state->dmr_reliab_buf, 0, 200 * sizeof(uint8_t));
    }
    //dmr buffer end

    //close MBE out files
    if (opts->mbe_out_f != NULL) {
        closeMbeOutFile(opts, state);
    }
    if (opts->mbe_out_fR != NULL) {
        closeMbeOutFileR(opts, state);
    }

    state->jitter = -1;
    state->lastsynctype = DSD_SYNC_NONE;
    state->carrier = 0;
    state->max = 15000;
    state->min = -15000;
    state->center = 0;
    state->m17_polarity = 0; /* Reset M17 polarity so next transmission can auto-detect fresh */
    state->err_str[0] = '\0';
    state->err_strR[0] = '\0';
    set_spaces(state->fsubtype, 14);
    set_spaces(state->ftype, 13);
    state->errs = 0;
    state->errs2 = 0;

    //zero out right away if not trunking
    if (opts->p25_trunk == 0) {
        state->lasttg = 0;
        state->lastsrc = 0;
        state->lasttgR = 0;
        state->lastsrcR = 0;
        state->gi[0] = -1;
        state->gi[1] = -1;

        //zero out vc frequencies?
        state->p25_vc_freq[0] = 0;
        state->p25_vc_freq[1] = 0;

        //only reset cap+ rest channel if not trunking
        state->dmr_rest_channel = -1;

        //DMR Color Code
        //  state->dmr_color_code = 16; //disabled

        //zero out nxdn site/srv/cch info if not trunking
        state->nxdn_location_site_code = 0;
        state->nxdn_location_sys_code = 0;
        set_spaces(state->nxdn_location_category, 1);

        //channel access information
        state->nxdn_rcn = 0;
        state->nxdn_base_freq = 0;
        state->nxdn_step = 0;
        state->nxdn_bw = 0;

        //dmr mfid branding and site parms
        state->dmr_branding_sub[0] = '\0';
        state->dmr_branding[0] = '\0';
        state->dmr_site_parms[0] = '\0';
    }

    //The new event history should not require this, but revert if other random issues suddenly come up
    //this was mainly for preventling numbers blipping out on signal fade, but also leaves stale values
    //on occassion when carrier drops and return to control channel, doesn't close wav files in that instance
    //  if (opts->p25_trunk == 1 && opts->p25_is_tuned == 1 && time(NULL) - state->last_cc_sync_time > opts->trunk_hangtime)
    {
        state->lasttg = 0;
        state->lastsrc = 0;
        state->lasttgR = 0;
        state->lastsrcR = 0;
        state->gi[0] = -1;
        state->gi[1] = -1;
        //  state->nxdn_last_ran = -1; //
        state->nxdn_last_rid = 0;
        state->nxdn_last_tg = 0;
    }

    state->lastp25type = 0;
    state->repeat = 0;
    state->nac = 0;
    state->numtdulc = 0;
    state->slot1light[0] = '\0';
    state->slot2light[0] = '\0';
    state->firstframe = 0;
    memset(state->aout_max_buf, 0, sizeof(float) * 200);
    state->aout_max_buf_p = state->aout_max_buf;
    state->aout_max_buf_idx = 0;

    memset(state->aout_max_bufR, 0, sizeof(float) * 200);
    state->aout_max_buf_pR = state->aout_max_bufR;
    state->aout_max_buf_idxR = 0;

    set_underscores(state->algid, 8);
    set_underscores(state->keyid, 16);
    mbe_initMbeParms(state->cur_mp, state->prev_mp, state->prev_mp_enhanced);
    mbe_initMbeParms(state->cur_mp2, state->prev_mp2, state->prev_mp_enhanced2);

    state->dmr_ms_mode = 0;

    //not sure if desirable here or not just yet, may need to disable a few of these
    state->payload_mi = 0;
    state->payload_miR = 0;
    state->payload_mfid = 0;
    state->payload_mfidR = 0;
    state->payload_algid = 0;
    state->payload_algidR = 0;
    state->payload_keyid = 0;
    state->payload_keyidR = 0;

    state->HYTL = 0;
    state->HYTR = 0;
    state->DMRvcL = 0;
    state->DMRvcR = 0;
    state->dropL = 256;
    state->dropR = 256;

    state->payload_miN = 0;
    state->p25vc = 0;
    state->payload_miP = 0;

    //ks array storage and counters
    memset(state->ks_octetL, 0, sizeof(state->ks_octetL));
    memset(state->ks_octetR, 0, sizeof(state->ks_octetR));
    memset(state->ks_bitstreamL, 0, sizeof(state->ks_bitstreamL));
    memset(state->ks_bitstreamR, 0, sizeof(state->ks_bitstreamR));
    state->octet_counter = 0;
    state->bit_counterL = 0;
    state->bit_counterR = 0;

    //xl specific, we need to know if the ESS is from HDU, or from LDU2
    state->xl_is_hdu = 0;

    //NXDN, when a new IV has arrived
    state->nxdn_new_iv = 0;

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

    state->dmr_encL = 0;
    state->dmr_encR = 0;

    state->dmrburstL = 17;
    state->dmrburstR = 17;

    //reset P2 ESS_B fragments and 4V counter
    for (short i = 0; i < 4; i++) {
        state->ess_b[0][i] = 0;
        state->ess_b[1][i] = 0;
    }
    state->fourv_counter[0] = 0;
    state->fourv_counter[1] = 0;
    state->voice_counter[0] = 0;
    state->voice_counter[1] = 0;

    //values displayed in ncurses terminal
    // state->p25_vc_freq[0] = 0;
    // state->p25_vc_freq[1] = 0;

    //new nxdn stuff
    state->nxdn_part_of_frame = 0;
    state->nxdn_ran = 0;
    state->nxdn_sf = 0;
    memset(state->nxdn_sacch_frame_segcrc, 1, sizeof(state->nxdn_sacch_frame_segcrc)); //init on 1, bad CRC all
    state->nxdn_sacch_non_superframe = TRUE;
    memset(state->nxdn_sacch_frame_segment, 1, sizeof(state->nxdn_sacch_frame_segment));
    state->nxdn_alias_block_number = 0;
    memset(state->nxdn_alias_block_segment, 0, sizeof(state->nxdn_alias_block_segment));
    state->nxdn_call_type[0] = '\0';

    //unload keys when using keylaoder
    if (state->keyloader == 1) {
        state->R = 0;  //NXDN, or RC4 (slot 1)
        state->RR = 0; //RC4 (slot 2)
        state->K = 0;  //BP
        state->K1 = 0; //tera 10/32/64 char BP
        state->K2 = 0;
        state->K3 = 0;
        state->K4 = 0;
        memset(state->A1, 0, sizeof(state->A1));
        memset(state->A2, 0, sizeof(state->A2));
        memset(state->A3, 0, sizeof(state->A3));
        memset(state->A4, 0, sizeof(state->A4));
        memset(state->aes_key_loaded, 0, sizeof(state->aes_key_loaded));
        state->H = 0; //shim for above
    }

    //forcing key application will re-enable this at the time of voice tx
    state->nxdn_cipher_type = 0;

    //dmr overaching manufacturer in use for a particular system or radio
    // state->dmr_mfid = -1;

    //dmr slco stuff
    memset(state->dmr_cach_fragment, 1, sizeof(state->dmr_cach_fragment));
    state->dmr_cach_counter = 0;

    //initialize unified dmr pdu 'superframe'
    memset(state->dmr_pdu_sf, 0, sizeof(state->dmr_pdu_sf));
    memset(state->data_header_valid, 0, sizeof(state->data_header_valid));

    //initialize cap+ bits and block num storage
    memset(state->cap_plus_csbk_bits, 0, sizeof(state->cap_plus_csbk_bits));
    memset(state->cap_plus_block_num, 0, sizeof(state->cap_plus_block_num));

    //init confirmed data individual block crc as invalid
    memset(state->data_block_crc_valid, 0, sizeof(state->data_block_crc_valid));

    //embedded signalling
    memset(state->dmr_embedded_signalling, 0, sizeof(state->dmr_embedded_signalling));

    //late entry mi fragments
    memset(state->late_entry_mi_fragment, 0, sizeof(state->late_entry_mi_fragment));

    //dmr talker alias new/fixed stuff
    memset(state->dmr_alias_format, 0, sizeof(state->dmr_alias_format));
    memset(state->dmr_alias_block_len, 0, sizeof(state->dmr_alias_block_len));
    memset(state->dmr_alias_char_size, 0, sizeof(state->dmr_alias_char_size));
    memset(state->dmr_alias_block_segment, 0, sizeof(state->dmr_alias_block_segment));
    memset(state->dmr_embedded_gps, 0, sizeof(state->dmr_embedded_gps));
    memset(state->dmr_lrrp_gps, 0, sizeof(state->dmr_lrrp_gps));

    //Generic Talker Alias String
    memset(state->generic_talker_alias, 0, sizeof(state->generic_talker_alias));
    state->generic_talker_alias_src[0] = 0;
    state->generic_talker_alias_src[1] = 0;

    /* Initialize P25 metrics counters used by ncurses BER display */
    state->p25_p1_fec_ok = 0;
    state->p25_p1_fec_err = 0;
    state->p25_p1_voice_fec_ok = 0;
    state->p25_p1_voice_fec_err = 0;
    state->p25_p2_rs_facch_ok = 0;
    state->p25_p2_rs_facch_err = 0;
    state->p25_p2_rs_facch_corr = 0;
    state->p25_p2_rs_sacch_ok = 0;
    state->p25_p2_rs_sacch_err = 0;
    state->p25_p2_rs_sacch_corr = 0;
    state->p25_p2_rs_ess_ok = 0;
    state->p25_p2_rs_ess_err = 0;
    state->p25_p2_rs_ess_corr = 0;

    // Initialize P25 SM candidate cache bookkeeping
    dsd_trunk_cc_candidates* cc_candidates = dsd_trunk_cc_candidates_get(state);
    if (cc_candidates) {
        cc_candidates->count = 0;
        cc_candidates->idx = 0;
    }
    state->p25_cc_cache_loaded = 0;

    // memset(state->active_channel, 0, sizeof(state->active_channel));

    //REMUS! multi-purpose call_string
    set_spaces(state->call_string[0], 21);
    set_spaces(state->call_string[1], 21);

    if (now - state->last_cc_sync_time > 10) //ten seconds of no carrier
    {
        state->dmr_rest_channel = -1;
        state->p25_vc_freq[0] = 0;
        state->p25_vc_freq[1] = 0;
        state->dmr_mfid = -1;
        state->dmr_branding_sub[0] = '\0';
        state->dmr_branding[0] = '\0';
        state->dmr_site_parms[0] = '\0';
        opts->p25_is_tuned = 0;
        memset(state->active_channel, 0, sizeof(state->active_channel));
    }

    opts->dPMR_next_part_of_superframe = 0;

    state->dPMRVoiceFS2Frame.CalledIDOk = 0;
    state->dPMRVoiceFS2Frame.CallingIDOk = 0;
    memset(state->dPMRVoiceFS2Frame.CalledID, 0, 8);
    memset(state->dPMRVoiceFS2Frame.CallingID, 0, 8);
    memset(state->dPMRVoiceFS2Frame.Version, 0, 8);

    set_spaces(state->dpmr_caller_id, 6);
    set_spaces(state->dpmr_target_id, 6);

    //YSF Fusion Call Strings
    set_spaces(state->ysf_tgt, 10);
    set_spaces(state->ysf_src, 10);
    set_spaces(state->ysf_upl, 10);
    set_spaces(state->ysf_dnl, 10);
    set_spaces(state->ysf_rm1, 5);
    set_spaces(state->ysf_rm2, 5);
    set_spaces(state->ysf_rm3, 5);
    set_spaces(state->ysf_rm4, 5);
    memset(state->ysf_txt, 0, sizeof(state->ysf_txt));
    state->ysf_dt = 9;
    state->ysf_fi = 9;
    state->ysf_cm = 9;

    //DSTAR Call Strings
    set_spaces(state->dstar_rpt1, 8);
    set_spaces(state->dstar_rpt2, 8);
    set_spaces(state->dstar_dst, 8);
    set_spaces(state->dstar_src, 8);
    set_spaces(state->dstar_txt, 8);
    set_spaces(state->dstar_gps, 8);

    //M17 Storage
    memset(state->m17_lsf, 0, sizeof(state->m17_lsf));
    memset(state->m17_pkt, 0, sizeof(state->m17_pkt));
    state->m17_pbc_ct = 0;
    state->m17_str_dt = 9;

    state->m17_dst = 0;
    state->m17_src = 0;
    state->m17_can = 0;
    memset(state->m17_dst_csd, 0, sizeof(state->m17_dst_csd));
    memset(state->m17_src_csd, 0, sizeof(state->m17_src_csd));
    state->m17_dst_str[0] = '\0';
    state->m17_src_str[0] = '\0';

    state->m17_enc = 0;
    state->m17_enc_st = 0;
    memset(state->m17_meta, 0, sizeof(state->m17_meta));

    //misc str storage
    //  sprintf (state->str50a, "%s", "");
    // memset (state->str50b, 0, 50*sizeof(char));
    // memset (state->str50c, 0, 50*sizeof(char));
    // memset (state->m17sms, 0, 800*sizeof(char));
    // sprintf (state->m17dat, "%s", "");

    //set float temp buffer to baseline
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

    //we do reset the counter, but not the static_ks_bits
    memset(state->static_ks_counter, 0, sizeof(state->static_ks_counter));

} //nocarrier

static int
liveScanner(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return -1;
    }
    // Cache previous thresholds to avoid redundant recalculation
    static int last_max = INT_MIN;
    static int last_min = INT_MAX;

    if (opts->floating_point == 1) {

        if (opts->audio_gain > 50.0f) {
            opts->audio_gain = 50.0f;
        }
        if (opts->audio_gain < 0.0f) {
            opts->audio_gain = 0.0f;
        }
    } else if (opts->audio_gain == 0) {
        state->aout_gain = 15.0f;
        state->aout_gainR = 15.0f;
    }

#ifdef USE_RTLSDR
    if (opts->audio_in_type == AUDIO_IN_RTL) {
        if (state->rtl_ctx == NULL) {
            if (rtl_stream_create(opts, &state->rtl_ctx) < 0) {
                LOG_ERROR("Failed to create RTL stream.\n");
            }
        }
        if (state->rtl_ctx && rtl_stream_start(state->rtl_ctx) < 0) {
            LOG_ERROR("Failed to open RTL-SDR stream.\n");
        }
        opts->rtl_started = 1;
        opts->rtl_needs_restart = 0;
    }
#endif

    if (opts->audio_in_type == AUDIO_IN_PULSE) {
        if (openAudioInput(opts) != 0) {
            return -1;
        }
    }

    if (opts->audio_out_type == 0) {
        if (openAudioOutput(opts) != 0) {
            return -1;
        }
    }

    //push a DSD-neo started event so users can see what this section does, and also gives users an idea of when context started
    state->event_history_s[0].Event_History_Items[0].color_pair = 4;
    watchdog_event_datacall(opts, state, 0, 0, "Any decoded voice calls or data calls display here;", 0);
    push_event_history(&state->event_history_s[0]);
    init_event_history(&state->event_history_s[0], 0, 1);
    state->event_history_s[0].Event_History_Items[0].color_pair = 4;
    watchdog_event_datacall(opts, state, 0, 0, "DSD-neo Started and Event History Initialized;", 0);
    push_event_history(&state->event_history_s[0]);
    init_event_history(&state->event_history_s[0], 0, 1);

    if (opts->event_out_file[0] != 0) {
        time_t now = time(NULL);
        char timestr[9];
        char datestr[11];
        getTimeN_buf(now, timestr);
        getDateN_buf(now, datestr);
        char event_string[2000];
        memset(event_string, 0, sizeof(event_string));
        snprintf(event_string, sizeof event_string, "%s %s DSD-neo Started and Event History Initialized;", datestr,
                 timestr);
        write_event_to_log_file(opts, state, 0, 0, event_string);
        memset(event_string, 0, sizeof(event_string));
        snprintf(event_string, sizeof event_string, "%s %s Any decoded voice calls or data calls display here;",
                 datestr, timestr);
        write_event_to_log_file(opts, state, 0, 0, event_string);
    }

    //test P25 moto alias by loading in test vectors captured from a system and dumped on forum (see dsd_gps.c)
    // apx_embedded_alias_test_phase1(opts, state); //enable this to run test

    /* Start P25 SM watchdog thread to ensure ticks during I/O stalls */
    p25_sm_watchdog_start(opts, state);

    while (!exitflag) {
        // Drain any pending UI->Demod commands before heavy work
        dsd_runtime_pump_controls(opts, state);

        // Cooperative tick: runs only if another tick isn't in progress
        p25_sm_try_tick(opts, state);

        // Drain again to reduce latency for common key actions
        dsd_runtime_pump_controls(opts, state);

        noCarrier(opts, state);
        state->synctype = getFrameSync(opts, state);
        // Recompute thresholds only when extrema change
        if (state->max != last_max || state->min != last_min) {
            state->center = ((state->max) + (state->min)) / 2;
            state->umid = (((state->max) - state->center) * 5 / 8) + state->center;
            state->lmid = (((state->min) - state->center) * 5 / 8) + state->center;
            last_max = state->max;
            last_min = state->min;
        }

        while (state->synctype != DSD_SYNC_NONE) {
            // Drain UI commands during active decoding so hotkeys work in-call
            dsd_runtime_pump_controls(opts, state);

            processFrame(opts, state);

#ifdef TRACE_DSD
            state->debug_prefix = 'S';
#endif

            // state->synctype = getFrameSync (opts, state);

#ifdef TRACE_DSD
            state->debug_prefix = '\0';
#endif

            // Drain again between frames to reduce latency
            dsd_runtime_pump_controls(opts, state);
            state->synctype = getFrameSync(opts, state);
            // Recompute thresholds only when extrema change
            if (state->max != last_max || state->min != last_min) {
                state->center = ((state->max) + (state->min)) / 2;
                state->umid = (((state->max) - state->center) * 5 / 8) + state->center;
                state->lmid = (((state->min) - state->center) * 5 / 8) + state->center;
                last_max = state->max;
                last_min = state->min;
            }
        }
    }

    p25_sm_watchdog_stop();
    return 0;
}

void
dsd_engine_cleanup(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return;
    }

    // Signal that everything should shutdown.
    exitflag = 1;

    // Stop UI thread if the ncurses UI was in use.
    if (opts->use_ncurses_terminal == 1) {
        ui_stop();
    }

    // Emit a summary of any trunking channel map issues after UI teardown so output is visible.
    nxdn_trunk_diag_log_summary(opts, state);

#ifdef USE_CODEC2
    if (state->codec2_1600) {
        codec2_destroy(state->codec2_1600);
        state->codec2_1600 = NULL;
    }
    if (state->codec2_3200) {
        codec2_destroy(state->codec2_3200);
        state->codec2_3200 = NULL;
    }
#endif

    //watchdog event at this point
    watchdog_event_history(opts, state, 0);
    watchdog_event_current(opts, state, 0);
    watchdog_event_history(opts, state, 1);
    watchdog_event_current(opts, state, 1);

    noCarrier(opts, state);

    //watchdog event at this point
    watchdog_event_history(opts, state, 0);
    watchdog_event_current(opts, state, 0);
    watchdog_event_history(opts, state, 1);
    watchdog_event_current(opts, state, 1);

    if (opts->static_wav_file == 0) {
        if (opts->wav_out_f != NULL) {
            opts->wav_out_f = close_and_rename_wav_file(opts->wav_out_f, opts->wav_out_file, opts->wav_out_dir,
                                                        &state->event_history_s[0]);
        }

        if (opts->wav_out_fR != NULL) {
            opts->wav_out_fR = close_and_rename_wav_file(opts->wav_out_fR, opts->wav_out_fileR, opts->wav_out_dir,
                                                         &state->event_history_s[1]);
        }
    }

    else if (opts->static_wav_file == 1) {

        if (opts->wav_out_f != NULL) {
            opts->wav_out_f = close_wav_file(opts->wav_out_f);
        }

        //this one needed?
        if (opts->wav_out_fR != NULL) {
            opts->wav_out_fR = close_wav_file(opts->wav_out_fR);
        }
    }

    if (opts->wav_out_raw != NULL) {
        opts->wav_out_raw = close_wav_file(opts->wav_out_raw);
    }

    //no if statement first?
    closeSymbolOutFile(opts, state);

#ifdef USE_RTLSDR
    if (opts->rtl_started == 1) {
        if (state->rtl_ctx) {
            rtl_stream_stop(state->rtl_ctx);
            rtl_stream_destroy(state->rtl_ctx);
            state->rtl_ctx = NULL;
        }
    }
#endif

    if (opts->udp_sockfd != DSD_INVALID_SOCKET) {
        dsd_socket_close(opts->udp_sockfd);
    }

    if (opts->udp_sockfdA != DSD_INVALID_SOCKET) {
        dsd_socket_close(opts->udp_sockfdA);
    }

    if (opts->m17_udp_sock != DSD_INVALID_SOCKET) {
        dsd_socket_close(opts->m17_udp_sock);
    }

    if (opts->udp_in_ctx) {
        udp_input_stop(opts);
    }

    //close MBE out files
    if (opts->mbe_out_f != NULL) {
        closeMbeOutFile(opts, state);
    }
    if (opts->mbe_out_fR != NULL) {
        closeMbeOutFileR(opts, state);
    }

    // Persist the final effective configuration for the next run, if enabled.
    autosave_user_config(opts, state);

    LOG_NOTICE("\n");
    if (state->debug_mode == 1) {
        uint64_t* start_ms = DSD_STATE_EXT_GET_AS(uint64_t, state, DSD_STATE_EXT_ENGINE_START_MS);
        if (start_ms) {
            uint64_t elapsed_ms = dsd_time_monotonic_ms() - *start_ms;
            LOG_NOTICE("Runtime: %llu ms\n", (unsigned long long)elapsed_ms);
        }
    }
    LOG_NOTICE("Total audio errors: %i\n", state->debug_audio_errors);
    LOG_NOTICE("Total header errors: %i\n", state->debug_header_errors);
    LOG_NOTICE("Total irrecoverable header errors: %i\n", state->debug_header_critical_errors);
    LOG_NOTICE("Exiting.\n");

    dsd_state_ext_free_all(state);

    // Cleanup socket subsystem (required for Windows, no-op on POSIX)
    dsd_socket_cleanup();
}

int
dsd_engine_run(dsd_opts* opts, dsd_state* state) {
    if (!opts || !state) {
        return -1;
    }

    dsd_bootstrap_enable_ftz_daz_if_enabled();
    init_rrc_filter_memory(); //initialize input filtering
    InitAllFecFunction();
    CNXDNConvolution_init();

    // Initialize socket subsystem (required for Windows, no-op on POSIX)
    if (dsd_socket_init() != 0) {
        fprintf(stderr, "Failed to initialize socket subsystem\n");
        return 1;
    }
    int rc = 0;

    exitflag = 0;

    if (state->debug_mode == 1) {
        uint64_t* start_ms = (uint64_t*)malloc(sizeof(*start_ms));
        if (start_ms) {
            *start_ms = dsd_time_monotonic_ms();
            (void)dsd_state_ext_set(state, DSD_STATE_EXT_ENGINE_START_MS, start_ms, free);
        }
    }

    dsd_engine_frame_sync_hooks_install();
    dsd_engine_trunk_tuning_hooks_install();
    dsd_engine_rtl_stream_io_hooks_install();
    dsd_engine_rtl_stream_metrics_hooks_install();
    dsd_engine_rigctl_query_hooks_install();
    dsd_engine_net_audio_input_hooks_install();
    dsd_engine_udp_audio_hooks_install();
    dsd_engine_m17_udp_hooks_install();
    dsd_engine_p25_optional_hooks_install();

    // If trunking/scanner inputs were configured via INI/env rather than CLI (-C/-G),
    // import the CSVs now before the decoder begins processing.
    if (import_trunking_csvs_if_needed(opts, state) != 0) {
        rc = 1;
        goto ENGINE_OUT;
    }

    // If recording outputs were configured via INI, open them now for this run.
    open_recording_outputs_if_needed(opts, state);

    /* Rebuild audio filters after CLI/config/bootstrap may have changed the output rate.
       Base coefficients on the analog monitor sample rate so cutoffs stay correct. */
    {
        int filter_rate = analog_filter_rate_hz(opts, state);
        init_audio_filters(state, filter_rate);
    }

    /* Initialize trunking state machines with user configuration.
       Must be done after all opts parsing so hangtime/timeouts are honored. */
    p25_sm_init(opts, state);
    dmr_sm_init(opts, state);

    if (opts->resume > 0) {
        openSerial(opts, state);
        if (opts->serial_fd < 0) {
            rc = 1;
            goto ENGINE_OUT;
        }
    }

    if (dsd_engine_setup_io(opts, state) != 0) {
        rc = 1;
        goto ENGINE_OUT;
    }
    if (exitflag) {
        goto ENGINE_OUT;
    }

    signal(SIGINT, dsd_engine_signal_handler);
    signal(SIGTERM, dsd_engine_signal_handler);

    dsd_engine_parse_m17_userdata(opts, state);

    if (opts->playfiles == 1) {

        // Use the effective argc (post long-option compaction) so the file
        // list aligns with state->optind from getopt.
        playMbeFiles(opts, state, state->cli_argc_effective, state->cli_argv);
    }

    else if (opts->m17encoder == 1) {
        //disable RRC filter for now
        opts->use_cosine_filter = 0;

        opts->pulse_digi_rate_out = 8000;

        //open any inputs, if not already opened
        if (opts->audio_in_type == AUDIO_IN_PULSE) {
            if (openAudioInput(opts) != 0) {
                rc = 1;
                goto ENGINE_OUT;
            }
        }

#ifdef USE_RTLSDR
        else if (opts->audio_in_type == AUDIO_IN_RTL) {
            if (state->rtl_ctx == NULL) {
                if (rtl_stream_create(opts, &state->rtl_ctx) < 0) {
                    LOG_ERROR("Failed to create RTL stream.\n");
                }
            }
            if (state->rtl_ctx && rtl_stream_start(state->rtl_ctx) < 0) {
                LOG_ERROR("Failed to open RTL-SDR stream.\n");
            }
            opts->rtl_started = 1;
        }
#endif

        //open any outputs, if not already opened
        if (opts->audio_out_type == 0) {
            if (openAudioOutput(opts) != 0) {
                rc = 1;
                goto ENGINE_OUT;
            }
        }
        // Start UI thread when ncurses UI is enabled so ncursesPrinter updates are rendered
        dsd_engine_start_ui(opts, state);
        //All input and output now opened and handled correctly, so let's not break things by tweaking
        encodeM17STR(opts, state);
    }

    else if (opts->m17encoderbrt == 1) {
        opts->pulse_digi_rate_out = 8000;
        //open any outputs, if not already opened
        if (opts->audio_out_type == 0) {
            if (openAudioOutput(opts) != 0) {
                rc = 1;
                goto ENGINE_OUT;
            }
        }
        // Start UI thread when ncurses UI is enabled so ncursesPrinter updates are rendered
        dsd_engine_start_ui(opts, state);
        encodeM17BRT(opts, state);
    }

    else if (opts->m17encoderpkt == 1) {
        //disable RRC filter for now
        opts->use_cosine_filter = 0;

        opts->pulse_digi_rate_out = 8000;
        //open any outputs, if not already opened
        if (opts->audio_out_type == 0) {
            if (openAudioOutput(opts) != 0) {
                rc = 1;
                goto ENGINE_OUT;
            }
        }
        // Start UI thread when ncurses UI is enabled so ncursesPrinter updates are rendered
        dsd_engine_start_ui(opts, state);
        encodeM17PKT(opts, state);
    }

    else if (opts->m17decoderip == 1) {
        opts->pulse_digi_rate_out = 8000;
        //open any outputs, if not already opened
        if (opts->audio_out_type == 0) {
            if (openAudioOutput(opts) != 0) {
                rc = 1;
                goto ENGINE_OUT;
            }
        }
        // Start UI thread when ncurses UI is enabled so ncursesPrinter updates are rendered
        dsd_engine_start_ui(opts, state);
        processM17IPF(opts, state);
    }

    else {
        // Start UI thread before entering main decode loop when enabled
        dsd_engine_start_ui(opts, state);
        if (liveScanner(opts, state) != 0) {
            rc = 1;
        }
    }

ENGINE_OUT:
    dsd_engine_cleanup(opts, state);
    return rc;
}
