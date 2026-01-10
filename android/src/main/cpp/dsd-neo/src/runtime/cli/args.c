// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/cli.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/log.h>

#include <dsd-neo/runtime/colors.h>

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/csv_import.h>
#include <dsd-neo/core/file_io.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/crypto/dmr_keystream.h>
#include <dsd-neo/platform/posix_compat.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(__MINGW32__) || defined(__MINGW64__)
#include <getopt.h>
#include <unistd.h>
#elif !DSD_PLATFORM_WIN_NATIVE
#include <unistd.h>
#endif

// Local helpers --------------------------------------------------------------
static int dsd_parse_short_opts(int argc, char** argv, dsd_opts* opts, dsd_state* state, int* out_exit_rc);

static void
cli_set_exit_rc(int* out_exit_rc, int rc) {
    if (out_exit_rc) {
        *out_exit_rc = rc;
    }
}

static int
cli_collect_hex_digits(const char* in, char* out, size_t out_cap, size_t* out_len) {
    if (!in || !out || out_cap == 0) {
        return 0;
    }
    size_t w = 0;
    const char* p = in;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    for (; *p; ++p) {
        if (isspace((unsigned char)*p)) {
            continue;
        }
        if (!isxdigit((unsigned char)*p)) {
            return 0;
        }
        if (w + 1 >= out_cap) {
            return 0;
        }
        out[w++] = *p;
    }
    out[w] = '\0';
    if (out_len) {
        *out_len = w;
    }
    return 1;
}

static int
cli_parse_hex_u64_n(const char* hex, size_t n, unsigned long long* out) {
    if (!hex || !out || n == 0 || n > 16) {
        return 0;
    }
    char buf[17];
    memcpy(buf, hex, n);
    buf[n] = '\0';
    char* end = NULL;
    unsigned long long v = strtoull(buf, &end, 16);
    if (!end || *end != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

// Parse long-style options and environment mapping; also supports the
// one-shot LCN calculator. Short-option parsing has been migrated here
// to centralize all CLI handling in runtime.
int
dsd_parse_args(int argc, char** argv, dsd_opts* opts, dsd_state* state, int* out_argc, int* out_exit_rc) {

    dsd_neo_config_init(opts);
    const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();

    // CLI long options (pre-scan) ------------------------------------------------
    const char* calc_csv_cli = NULL;
    const char* calc_step_cli = NULL;
    const char* calc_ccf_cli = NULL;
    const char* calc_ccl_cli = NULL;
    const char* calc_start_cli = NULL;
    const char* input_vol_cli = NULL;
    const char* input_warn_db_cli = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rtltcp-autotune") == 0) {
            opts->rtltcp_autotune = 1;
            dsd_setenv("DSD_NEO_TCP_AUTOTUNE", "1", 1);
            continue;
        }
        if (strcmp(argv[i], "--p25-vc-grace") == 0 && i + 1 < argc) {
            opts->p25_vc_grace_s = atof(argv[++i]);
            char buf[32];
            snprintf(buf, sizeof buf, "%.3f", opts->p25_vc_grace_s);
            dsd_setenv("DSD_NEO_P25_VC_GRACE", buf, 1);
            LOG_NOTICE("P25: VC grace set to %.2fs (CLI).\n", opts->p25_vc_grace_s);
            continue;
        }
        if (strcmp(argv[i], "--p25-min-follow-dwell") == 0 && i + 1 < argc) {
            opts->p25_min_follow_dwell_s = atof(argv[++i]);
            char buf[32];
            snprintf(buf, sizeof buf, "%.3f", opts->p25_min_follow_dwell_s);
            dsd_setenv("DSD_NEO_P25_MIN_FOLLOW_DWELL", buf, 1);
            LOG_NOTICE("P25: Min follow dwell set to %.2fs (CLI).\n", opts->p25_min_follow_dwell_s);
            continue;
        }
        if (strcmp(argv[i], "--p25-grant-voice-timeout") == 0 && i + 1 < argc) {
            opts->p25_grant_voice_to_s = atof(argv[++i]);
            char buf[32];
            snprintf(buf, sizeof buf, "%.3f", opts->p25_grant_voice_to_s);
            dsd_setenv("DSD_NEO_P25_GRANT_VOICE_TO", buf, 1);
            LOG_NOTICE("P25: Grant->Voice timeout set to %.2fs (CLI).\n", opts->p25_grant_voice_to_s);
            continue;
        }
        if (strcmp(argv[i], "--p25-retune-backoff") == 0 && i + 1 < argc) {
            opts->p25_retune_backoff_s = atof(argv[++i]);
            char buf[32];
            snprintf(buf, sizeof buf, "%.3f", opts->p25_retune_backoff_s);
            dsd_setenv("DSD_NEO_P25_RETUNE_BACKOFF", buf, 1);
            LOG_NOTICE("P25: Retune backoff set to %.2fs (CLI).\n", opts->p25_retune_backoff_s);
            continue;
        }
        if (strcmp(argv[i], "--p25-mac-hold") == 0 && i + 1 < argc) {
            double v = atof(argv[++i]);
            char buf[32];
            snprintf(buf, sizeof buf, "%.3f", v);
            dsd_setenv("DSD_NEO_P25_MAC_HOLD", buf, 1);
            LOG_NOTICE("P25: MAC hold set to %.2fs (CLI).\n", v);
            continue;
        }
        if (strcmp(argv[i], "--p25-ring-hold") == 0 && i + 1 < argc) {
            double v = atof(argv[++i]);
            char buf[32];
            snprintf(buf, sizeof buf, "%.3f", v);
            dsd_setenv("DSD_NEO_P25_RING_HOLD", buf, 1);
            LOG_NOTICE("P25: Ring hold set to %.2fs (CLI).\n", v);
            continue;
        }
        if (strcmp(argv[i], "--p25-cc-grace") == 0 && i + 1 < argc) {
            double v = atof(argv[++i]);
            if (v < 0) {
                v = 0;
            }
            if (v > 120) {
                v = 120;
            }
            char buf[32];
            snprintf(buf, sizeof buf, "%.3f", v);
            dsd_setenv("DSD_NEO_P25_CC_GRACE", buf, 1);
            LOG_NOTICE("P25: CC grace set to %.2fs (CLI).\n", v);
            continue;
        }
        if (strcmp(argv[i], "--p25-force-release-extra") == 0 && i + 1 < argc) {
            opts->p25_force_release_extra_s = atof(argv[++i]);
            char buf[32];
            snprintf(buf, sizeof buf, "%.3f", opts->p25_force_release_extra_s);
            dsd_setenv("DSD_NEO_P25_FORCE_RELEASE_EXTRA", buf, 1);
            LOG_NOTICE("P25: Force-release extra set to %.2fs (CLI).\n", opts->p25_force_release_extra_s);
            continue;
        }
        if (strcmp(argv[i], "--p25-force-release-margin") == 0 && i + 1 < argc) {
            opts->p25_force_release_margin_s = atof(argv[++i]);
            char buf[32];
            snprintf(buf, sizeof buf, "%.3f", opts->p25_force_release_margin_s);
            dsd_setenv("DSD_NEO_P25_FORCE_RELEASE_MARGIN", buf, 1);
            LOG_NOTICE("P25: Force-release margin set to %.2fs (CLI).\n", opts->p25_force_release_margin_s);
            continue;
        }
        if (strcmp(argv[i], "--p25-p1-err-hold-pct") == 0 && i + 1 < argc) {
            opts->p25_p1_err_hold_pct = atof(argv[++i]);
            char buf[32];
            snprintf(buf, sizeof buf, "%.1f", opts->p25_p1_err_hold_pct);
            dsd_setenv("DSD_NEO_P25P1_ERR_HOLD_PCT", buf, 1);
            LOG_NOTICE("P25p1: Error-hold threshold set to %.1f%% (CLI).\n", opts->p25_p1_err_hold_pct);
            continue;
        }
        if (strcmp(argv[i], "--p25-p1-err-hold-sec") == 0 && i + 1 < argc) {
            opts->p25_p1_err_hold_s = atof(argv[++i]);
            char buf[32];
            snprintf(buf, sizeof buf, "%.3f", opts->p25_p1_err_hold_s);
            dsd_setenv("DSD_NEO_P25P1_ERR_HOLD_S", buf, 1);
            LOG_NOTICE("P25p1: Error-hold seconds set to %.2fs (CLI).\n", opts->p25_p1_err_hold_s);
            continue;
        }
        if (strcmp(argv[i], "--calc-lcn") == 0 && i + 1 < argc) {
            calc_csv_cli = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--calc-step") == 0 && i + 1 < argc) {
            calc_step_cli = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--calc-cc-freq") == 0 && i + 1 < argc) {
            calc_ccf_cli = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--calc-cc-lcn") == 0 && i + 1 < argc) {
            calc_ccl_cli = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--calc-start-lcn") == 0 && i + 1 < argc) {
            calc_start_cli = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--auto-ppm") == 0) {
            opts->rtl_auto_ppm = 1;
            dsd_setenv("DSD_NEO_AUTO_PPM", "1", 1);
            continue;
        }
        if (strcmp(argv[i], "--input-volume") == 0 && i + 1 < argc) {
            input_vol_cli = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--input-level-warn-db") == 0 && i + 1 < argc) {
            input_warn_db_cli = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--auto-ppm-snr") == 0 && i + 1 < argc) {
            const char* sv = argv[++i];
            if (sv && *sv) {
                opts->rtl_auto_ppm_snr_db = (float)atof(sv);
                char buf[32];
                snprintf(buf, sizeof buf, "%.2f", opts->rtl_auto_ppm_snr_db);
                dsd_setenv("DSD_NEO_AUTO_PPM_SNR_DB", buf, 1);
            }
            continue;
        }
        if (strcmp(argv[i], "--enc-lockout") == 0) {
            opts->trunk_tune_enc_calls = 0;
            LOG_NOTICE("P25: Encrypted call lockout: On (skip encrypted).\n");
            continue;
        }
        if (strcmp(argv[i], "--enc-follow") == 0) {
            opts->trunk_tune_enc_calls = 1;
            LOG_NOTICE("P25: Encrypted call lockout: Off (follow encrypted).\n");
            continue;
        }
        if (strcmp(argv[i], "--no-p25p2-soft") == 0) {
            opts->p25_p2_soft_erasure = 0;
            LOG_NOTICE("P25P2: Soft-decision RS erasure marking disabled.\n");
            continue;
        }
        if (strcmp(argv[i], "--no-p25p1-soft-voice") == 0) {
            opts->p25_p1_soft_voice = 0;
            LOG_NOTICE("P25P1: Soft-decision voice FEC disabled.\n");
            continue;
        }
    }

    // If CLI present, set env vars and maybe run calculator
    if (calc_csv_cli) {
        dsd_setenv("DSD_NEO_DMR_T3_CALC_CSV", calc_csv_cli, 1);
        if (calc_step_cli) {
            dsd_setenv("DSD_NEO_DMR_T3_STEP_HZ", calc_step_cli, 1);
        }
        if (calc_ccf_cli) {
            dsd_setenv("DSD_NEO_DMR_T3_CC_FREQ", calc_ccf_cli, 1);
        }
        if (calc_ccl_cli) {
            dsd_setenv("DSD_NEO_DMR_T3_CC_LCN", calc_ccl_cli, 1);
        }
        if (calc_start_cli) {
            dsd_setenv("DSD_NEO_DMR_T3_START_LCN", calc_start_cli, 1);
        }
        /* Refresh typed env config after CLI writes. */
        dsd_neo_config_init(opts);
        int rc = dsd_cli_calc_dmr_t3_lcn_from_csv(calc_csv_cli);
        cli_set_exit_rc(out_exit_rc, rc);
        return DSD_PARSE_ONE_SHOT;
    }

    // Environment fallback
    if (cfg && cfg->dmr_t3_calc_csv_is_set && cfg->dmr_t3_calc_csv[0] != '\0') {
        int rc = dsd_cli_calc_dmr_t3_lcn_from_csv(cfg->dmr_t3_calc_csv);
        cli_set_exit_rc(out_exit_rc, rc);
        return DSD_PARSE_ONE_SHOT;
    }

    // Apply input volume and warn threshold
    if (input_vol_cli) {
        int mv = atoi(input_vol_cli);
        if (mv < 1) {
            mv = 1;
        }
        if (mv > 16) {
            mv = 16;
        }
        opts->input_volume_multiplier = mv;
        char b[16];
        snprintf(b, sizeof b, "%d", mv);
        dsd_setenv("DSD_NEO_INPUT_VOLUME", b, 1);
        LOG_NOTICE("Input volume multiplier: %dx\n", mv);
    } else if (cfg && cfg->input_volume_is_set) {
        opts->input_volume_multiplier = cfg->input_volume_multiplier;
        LOG_NOTICE("Input volume multiplier (env): %dx\n", opts->input_volume_multiplier);
    }
    if (input_warn_db_cli) {
        double thr = atof(input_warn_db_cli);
        if (thr < -200.0) {
            thr = -200.0;
        }
        if (thr > 0.0) {
            thr = 0.0;
        }
        opts->input_warn_db = thr;
        char b[32];
        snprintf(b, sizeof b, "%.1f", thr);
        dsd_setenv("DSD_NEO_INPUT_WARN_DB", b, 1);
        LOG_NOTICE("Low input warning threshold: %.1f dBFS\n", thr);
    } else if (cfg && cfg->input_warn_db_is_set) {
        opts->input_warn_db = (float)cfg->input_warn_db;
        LOG_NOTICE("Low input warning threshold (env): %.1f dBFS\n", opts->input_warn_db);
    }

    int new_argc = dsd_cli_compact_args(argc, argv);
    // Reset getopt index and parse short options here (migrated)
    extern int optind;
    // NOTE: We invoke getopt() multiple times (unit tests, bootstrap flows).
    // glibc requires optind=0 to fully reset internal state; BSD variants use optreset.
    optind = 1;
#if defined(__GLIBC__)
    optind = 0;
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    extern int optreset;
    optreset = 1;
#endif

    int parse_rc = dsd_parse_short_opts(new_argc, argv, opts, state, out_exit_rc);
    if (out_argc) {
        *out_argc = new_argc;
    }
    return parse_rc;
}

// Short-option getopt loop migrated to runtime
static int
dsd_parse_short_opts(int argc, char** argv, dsd_opts* opts, dsd_state* state, int* out_exit_rc) {
    int c;
    extern char* optarg;
    extern int optind;
    struct stat st = {0};
    char wav_file_directory[1024] = {0};
    char dsp_filename[1024] = {0};
    while ((c = getopt(argc, argv,
                       "~yhaepPqs:t:v:z:i:o:d:c:g:n:w:B:C:R:f:m:u:x:A:S:M:G:D:L:V:U:YK:b:H:X:NQ:WrlZTF@:!:01:2:345:6:7:"
                       "89:Ek:I:J:Oj^"))
           != -1) {
        switch (c) {
            case 'h':
                dsd_cli_usage();
                cli_set_exit_rc(out_exit_rc, 0);
                return DSD_PARSE_ONE_SHOT;
            case 'a': opts->call_alert = 1; break;
            case '~':
                state->debug_mode = 1;
                LOG_NOTICE("Debug Mode Enabled; \n");
                break;
            case 'O':
                audio_list_devices();
                cli_set_exit_rc(out_exit_rc, 0);
                return DSD_PARSE_ONE_SHOT;
            case 'M':
                strncpy(state->m17dat, optarg, 49);
                state->m17dat[49] = '\0';
                break;
            case 'I':
                sscanf(optarg, "%u", &state->tg_hold);
                LOG_NOTICE("TG Hold set to %u \n", state->tg_hold);
                break;
            case '8':
                opts->monitor_input_audio = 1;
                LOG_NOTICE("Experimental Raw Analog Source Monitoring Enabled (Pulse Audio Only!)\n");
                break;
            case 'j':
                opts->p25_lcw_retune = 1;
                LOG_NOTICE("P25: Enable LCW explicit retune (0x44).\n");
                break;
            case '^':
                opts->p25_prefer_candidates = 1;
                LOG_NOTICE("P25: Prefer CC candidates during hunt: On.\n");
                break;
            case '0':
                state->M = 0x21;
                LOG_NOTICE("Force RC4 Key over Missing PI header/LE Encryption Identifiers (DMR)\n");
                break;
            case '1':
                if (state) {
                    char hex[128];
                    size_t nhex = 0;
                    if (!cli_collect_hex_digits(optarg, hex, sizeof hex, &nhex)) {
                        LOG_ERROR("-1 expects a hex key (spaces allowed)\n");
                        cli_set_exit_rc(out_exit_rc, 1);
                        return DSD_PARSE_ERROR;
                    }
                    if (nhex == 0 || nhex > 16) {
                        LOG_ERROR("-1 expects 1..16 hex characters (spaces allowed)\n");
                        cli_set_exit_rc(out_exit_rc, 1);
                        return DSD_PARSE_ERROR;
                    }
                    unsigned long long key = 0ULL;
                    if (!cli_parse_hex_u64_n(hex, nhex, &key)) {
                        LOG_ERROR("-1 failed to parse key\n");
                        cli_set_exit_rc(out_exit_rc, 1);
                        return DSD_PARSE_ERROR;
                    }
                    state->R = key;
                    state->RR = key;
                    LOG_NOTICE("RC4/DES Encryption Key Value set to 0x%llX \n", state->R);
                    opts->unmute_encrypted_p25 = 0;
                    state->keyloader = 0;
                }
                break;
            case '2':
                state->tyt_bp = 1;
                sscanf(optarg, "%llX", &state->H);
                state->H = state->H & 0xFFFF;
                LOG_NOTICE("DMR TYT Basic 16-bit Key 0x%llX with Forced Application\n", state->H);
                break;
            case '!': tyt_ap_pc4_keystream_creation(state, optarg); break;
            case '@': retevis_rc2_keystream_creation(state, optarg); break;
            case '5': tyt_ep_aes_keystream_creation(state, optarg); break;
            case '9': ken_dmr_scrambler_keystream_creation(state, optarg); break;
            case 'A': anytone_bp_keystream_creation(state, optarg); break;
            case 'S': straight_mod_xor_keystream_creation(state, optarg); break;
            case '3':
                opts->dmr_le = 0;
                LOG_NOTICE("DMRA Late Entry Encryption Identifiers Disabled\n");
                break;
            case 'y':
                opts->floating_point = 1;
                LOG_NOTICE("Enabling Experimental Floating Point Audio Output\n");
                break;
            case 'Y':
                opts->scanner_mode = 1;
                opts->p25_trunk = 0;
                opts->trunk_enable = 0;
                opts->trunk_cli_seen = 1;
                break;
            case 'k':
                strncpy(opts->key_in_file, optarg, 1023);
                opts->key_in_file[1023] = '\0';
                if (csvKeyImportDec(opts, state) != 0) {
                    cli_set_exit_rc(out_exit_rc, 1);
                    return DSD_PARSE_ERROR;
                }
                state->keyloader = 1;
                break;
            case 'K':
                strncpy(opts->key_in_file, optarg, 1023);
                opts->key_in_file[1023] = '\0';
                if (csvKeyImportHex(opts, state) != 0) {
                    cli_set_exit_rc(out_exit_rc, 1);
                    return DSD_PARSE_ERROR;
                }
                state->keyloader = 1;
                break;
            case 'Q':
                snprintf(wav_file_directory, sizeof wav_file_directory, "%s", "./DSP");
                wav_file_directory[1023] = '\0';
                if (stat(wav_file_directory, &st) == -1) {
                    LOG_NOTICE("-Q %s DSP file directory does not exist\n", wav_file_directory);
                    LOG_NOTICE("Creating directory %s to save DSP Structured or M17 Binary Stream files\n",
                               wav_file_directory);
                    dsd_mkdir(wav_file_directory, 0700);
                }
                strncpy(dsp_filename, optarg, 1023);
                snprintf(opts->dsp_out_file, sizeof opts->dsp_out_file, "%s/%s", wav_file_directory, dsp_filename);
                LOG_NOTICE("Saving DSP Structured or M17 Float Stream Output to %s\n", opts->dsp_out_file);
                opts->use_dsp_output = 1;
                break;
            case 'z': {
                // TDMA voice slot preference
                // 0 = prefer slot 1, 1 = prefer slot 2, 2 = auto
                int pref = atoi(optarg);
                if (pref < 0) {
                    pref = 0;
                }
                if (pref > 2) {
                    pref = 2;
                }
                opts->slot_preference = pref;
                LOG_NOTICE("Slot preference set: %s\n", (pref == 0 ? "Slot 1" : (pref == 1 ? "Slot 2" : "Auto")));
                break;
            }
            case 'H':
                if (state) {
                    char hex[128];
                    size_t nhex = 0;
                    if (!cli_collect_hex_digits(optarg, hex, sizeof hex, &nhex)) {
                        LOG_ERROR("-H expects a hex key (spaces allowed)\n");
                        cli_set_exit_rc(out_exit_rc, 1);
                        return DSD_PARSE_ERROR;
                    }

                    unsigned long long k1 = 0ULL, k2 = 0ULL, k3 = 0ULL, k4 = 0ULL;
                    if (nhex == 10) {
                        if (!cli_parse_hex_u64_n(hex, 10, &k1)) {
                            LOG_ERROR("-H failed to parse 10-hex Hytera BP key\n");
                            cli_set_exit_rc(out_exit_rc, 1);
                            return DSD_PARSE_ERROR;
                        }
                        state->H = k1 & 0xFFFFFFFFFFULL;
                        state->K1 = state->H;
                        state->K2 = state->K3 = state->K4 = 0ULL;
                        LOG_NOTICE("Hytera BP key loaded (40-bit)\n");
                    } else if (nhex == 32) {
                        if (!cli_parse_hex_u64_n(hex + 0, 16, &k1) || !cli_parse_hex_u64_n(hex + 16, 16, &k2)) {
                            LOG_ERROR("-H failed to parse 32-hex key (2x16)\n");
                            cli_set_exit_rc(out_exit_rc, 1);
                            return DSD_PARSE_ERROR;
                        }
                        state->H = k1;
                        state->K1 = k1;
                        state->K2 = k2;
                        state->K3 = state->K4 = 0ULL;

                        state->A1[0] = state->A1[1] = k1;
                        state->A2[0] = state->A2[1] = k2;
                        state->A3[0] = state->A3[1] = 0ULL;
                        state->A4[0] = state->A4[1] = 0ULL;
                        state->aes_key_loaded[0] = state->aes_key_loaded[1] = (k1 != 0ULL || k2 != 0ULL) ? 1 : 0;

                        memset(state->aes_key, 0, sizeof(state->aes_key));
                        for (int i = 0; i < 8; i++) {
                            state->aes_key[i + 0] = (uint8_t)((state->A1[0] >> (56 - (i * 8))) & 0xFF);
                            state->aes_key[i + 8] = (uint8_t)((state->A2[0] >> (56 - (i * 8))) & 0xFF);
                        }
                        LOG_NOTICE("AES-128 / Hytera 128-bit key loaded (2x64)\n");
                    } else if (nhex == 64) {
                        if (!cli_parse_hex_u64_n(hex + 0, 16, &k1) || !cli_parse_hex_u64_n(hex + 16, 16, &k2)
                            || !cli_parse_hex_u64_n(hex + 32, 16, &k3) || !cli_parse_hex_u64_n(hex + 48, 16, &k4)) {
                            LOG_ERROR("-H failed to parse 64-hex key (4x16)\n");
                            cli_set_exit_rc(out_exit_rc, 1);
                            return DSD_PARSE_ERROR;
                        }
                        state->H = k1;
                        state->K1 = k1;
                        state->K2 = k2;
                        state->K3 = k3;
                        state->K4 = k4;

                        state->A1[0] = state->A1[1] = k1;
                        state->A2[0] = state->A2[1] = k2;
                        state->A3[0] = state->A3[1] = k3;
                        state->A4[0] = state->A4[1] = k4;
                        state->aes_key_loaded[0] = state->aes_key_loaded[1] =
                            (k1 != 0ULL || k2 != 0ULL || k3 != 0ULL || k4 != 0ULL) ? 1 : 0;

                        memset(state->aes_key, 0, sizeof(state->aes_key));
                        for (int i = 0; i < 8; i++) {
                            state->aes_key[i + 0] = (uint8_t)((state->A1[0] >> (56 - (i * 8))) & 0xFF);
                            state->aes_key[i + 8] = (uint8_t)((state->A2[0] >> (56 - (i * 8))) & 0xFF);
                            state->aes_key[i + 16] = (uint8_t)((state->A3[0] >> (56 - (i * 8))) & 0xFF);
                            state->aes_key[i + 24] = (uint8_t)((state->A4[0] >> (56 - (i * 8))) & 0xFF);
                        }
                        LOG_NOTICE("AES-256 / Hytera 256-bit key loaded (4x64)\n");
                    } else {
                        LOG_ERROR("-H expects 10, 32, or 64 hex characters (spaces allowed)\n");
                        cli_set_exit_rc(out_exit_rc, 1);
                        return DSD_PARSE_ERROR;
                    }
                    state->keyloader = 0;
                }
                break;
            case 'V': {
                // Enable TDMA voice synthesis for selected slot(s)
                // 1 = Slot 1, 2 = Slot 2, 3 = Both
                int v = atoi(optarg);
                if (v < 0) {
                    v = 0;
                }
                if (v > 3) {
                    v = 3;
                }
                opts->slot1_on = (v & 1) ? 1 : 0;
                opts->slot2_on = (v & 2) ? 1 : 0;
                if (v == 0) {
                    LOG_NOTICE("Voice synthesis disabled for both slots\n");
                } else if (v == 3) {
                    LOG_NOTICE("Voice synthesis enabled for Slot 1 and 2\n");
                } else {
                    LOG_NOTICE("Voice synthesis enabled for %s\n", v == 1 ? "Slot 1" : "Slot 2");
                }
                break;
            }
            case 'W':
                // Use imported group list as allow/white list (trunking)
                opts->trunk_use_allow_list = 1;
                LOG_NOTICE("Trunking: Group list allow/white list enabled.\n");
                break;
            case 'e':
                // Enable tune to data calls (DMR TIII, Cap+, NXDN Type-C)
                opts->trunk_tune_data_calls = 1;
                LOG_NOTICE("Trunking: Tune to data calls enabled.\n");
                break;
            case 'E':
                // Disable tune to group calls (DMR TIII, P25, NXDN Type-C/D)
                opts->trunk_tune_group_calls = 0;
                LOG_NOTICE("Trunking: Group call follow disabled.\n");
                break;
            case 'p':
                // Disable tune to private calls (DMR TIII, P25, NXDN Type-C/D)
                opts->trunk_tune_private_calls = 0;
                LOG_NOTICE("Trunking: Private call follow disabled.\n");
                break;
            case 'Z':
                // Log MBE/PDU payloads to console
                opts->payload = 1;
                LOG_NOTICE("Logging MBE/PDU payloads to console.\n");
                break;
            case 'P':
                if (opts->static_wav_file == 1) {
                    // Allow CLI to override config-derived static WAV settings (file not opened yet).
                    if (opts->wav_out_f != NULL) {
                        LOG_ERROR("-P cannot be used with -w (static WAV output)\n");
                        cli_set_exit_rc(out_exit_rc, 1);
                        return DSD_PARSE_ERROR;
                    }
                    opts->static_wav_file = 0;
                    opts->wav_out_file[0] = '\0';
                }
                snprintf(wav_file_directory, sizeof wav_file_directory, "%s", opts->wav_out_dir);
                wav_file_directory[1023] = '\0';
                if (stat(wav_file_directory, &st) == -1) {
                    LOG_NOTICE("-P %s WAV file directory does not exist\n", wav_file_directory);
                    LOG_NOTICE("Creating directory %s to save decoded wav files\n", wav_file_directory);
                    dsd_mkdir(wav_file_directory, 0700);
                }
                LOG_NOTICE("Per Call Wav File Enabled.\n");
                srand(time(NULL));
                opts->wav_out_f = open_wav_file(opts->wav_out_dir, opts->wav_out_file, 8000, 0);
                opts->wav_out_fR = open_wav_file(opts->wav_out_dir, opts->wav_out_fileR, 8000, 0);
                opts->dmr_stereo_wav = 1;
                break;
            case '7':
                // Set custom directory for per-call WAV saving
                strncpy(opts->wav_out_dir, optarg, 511);
                opts->wav_out_dir[511] = '\0';
                LOG_NOTICE("Per-call WAV directory set to: %s\n", opts->wav_out_dir);
                break;
            case 'F':
                opts->aggressive_framesync = 0;
                opts->dmr_crc_relaxed_default = 1;
                LOG_NOTICE("%s", KYEL);
                LOG_NOTICE("Relax P25 Phase 2 MAC_SIGNAL CRC Checksum Pass/Fail\n");
                LOG_NOTICE("Relax DMR RAS/CRC CSBK/DATA Pass/Fail\n");
                LOG_NOTICE("Relax NXDN SACCH/FACCH/CAC/F2U CRC Pass/Fail\n");
                LOG_NOTICE("Relax M17 LSF/PKT CRC Pass/Fail\n");
                LOG_NOTICE("%s", KNRM);
                break;
            case 'i':
                strncpy(opts->audio_in_dev, optarg, 2047);
                opts->audio_in_dev[2047] = '\0';
                break;
            case 'N': opts->use_ncurses_terminal = 1; break;
            case 'T':
                /* Enable trunking features; protocol-agnostic alias kept in sync */
                opts->p25_trunk = 1;
                opts->trunk_enable = 1;
                opts->trunk_cli_seen = 1;
                break;
            case 'U':
                // Enable rigctl/TCP and set port
                opts->use_rigctl = 1;
                opts->rigctlportno = atoi(optarg);
                if (opts->rigctlportno <= 0) {
                    opts->rigctlportno = 4532; // SDR++ default
                }
                break;
            case 'B':
                // Set rigctl setmod bandwidth (Hz)
                opts->setmod_bw = atoi(optarg);
                if (opts->setmod_bw < 0) {
                    opts->setmod_bw = 0;
                }
                break;
            case 'o':
                strncpy(opts->audio_out_dev, optarg, 1023);
                opts->audio_out_dev[1023] = '\0';
                break;
            case 'd':
                strncpy(opts->mbe_out_dir, optarg, 1023);
                opts->mbe_out_dir[1023] = '\0';
                if (stat(opts->mbe_out_dir, &st) == -1) {
                    LOG_NOTICE("%s directory does not exist\n", opts->mbe_out_dir);
                    LOG_NOTICE("Creating directory %s to save mbe+ processed files\n", opts->mbe_out_dir);
                    dsd_mkdir(opts->mbe_out_dir, 0700);
                }
                break;
            case '6':
                // Output raw 48k/1 audio WAV file
                strncpy(opts->wav_out_file_raw, optarg, sizeof opts->wav_out_file_raw - 1);
                opts->wav_out_file_raw[sizeof opts->wav_out_file_raw - 1] = '\0';
                openWavOutFileRaw(opts, state);
                LOG_NOTICE("Raw audio WAV output: %s\n", opts->wav_out_file_raw);
                break;
            case 'c':
                // Symbol capture (dibit) output file
                strncpy(opts->symbol_out_file, optarg, sizeof opts->symbol_out_file - 1);
                opts->symbol_out_file[sizeof opts->symbol_out_file - 1] = '\0';
                opts->symbol_out_file_is_auto = 0;
                openSymbolOutFile(opts, state);
                LOG_NOTICE("Saving symbol capture to %s\n", opts->symbol_out_file);
                break;
            case 'g': {
                /* Digital output gain (matches legacy main.c semantics).
                   0 = auto gain; >0 fixes gain in the 0..50 range. */
                float g = (float)atof(optarg);
                if (g < 0.0f) {
                    /* Historical behavior: negative disables manual gain without changing autogain. */
                    LOG_NOTICE("Disabling audio out gain setting\n");
                    opts->audio_gain = g;
                    opts->audio_gainR = g;
                } else if (g == 0.0f) {
                    opts->audio_gain = 0.0f;
                    opts->audio_gainR = 0.0f;
                    LOG_NOTICE("Enabling audio out auto-gain\n");
                } else {
                    if (g > 50.0f) {
                        g = 50.0f;
                    }
                    opts->audio_gain = g;
                    opts->audio_gainR = g;
                    state->aout_gain = g;
                    state->aout_gainR = g;
                    LOG_NOTICE("Setting audio out gain to %.1f\n", g);
                }
                break;
            }
            case 'n': {
                /* Dual-purpose: -nm enables DMR mono; otherwise treat as analog gain 0..100. */
                if (optarg[0] == 'm' && optarg[1] == '\0') {
                    opts->dmr_mono = 1;
                    LOG_NOTICE("DMR Mono (1997 method) enabled\n");
                } else {
                    float ga = (float)atof(optarg);
                    if (ga < 0.0f) {
                        ga = 0.0f;
                    } else if (ga > 100.0f) {
                        ga = 100.0f;
                    }
                    opts->audio_gainA = ga;
                    LOG_NOTICE("Analog Audio Out Gain set to %.1f;\n", ga);
                    /* 0.0 means auto; analog_gain/agsm will derive the effective coefficient. */
                }
                break;
            }
            case 'w':
                if (opts->dmr_stereo_wav == 1) {
                    // Allow CLI to override config-derived per-call WAV settings (files not opened yet).
                    if (opts->wav_out_f != NULL || opts->wav_out_fR != NULL) {
                        LOG_ERROR("-w cannot be used with -P (per-call WAV saving)\n");
                        cli_set_exit_rc(out_exit_rc, 1);
                        return DSD_PARSE_ERROR;
                    }
                    opts->dmr_stereo_wav = 0;
                }
                strncpy(opts->wav_out_file, optarg, 1023);
                opts->wav_out_file[1023] = '\0';
                opts->dmr_stereo_wav = 0;
                opts->static_wav_file = 1;
                openWavOutFileLR(opts, state);
                LOG_NOTICE("Static WAV output: %s\n", opts->wav_out_file);
                break;
            case 'C': {
                // Import channel map CSV (channum,freq)
                strncpy(opts->chan_in_file, optarg, 1023);
                opts->chan_in_file[1023] = '\0';
                if (csvChanImport(opts, state) != 0) {
                    cli_set_exit_rc(out_exit_rc, 1);
                    return DSD_PARSE_ERROR;
                }
                LOG_NOTICE("Imported channel map from %s\n", opts->chan_in_file);
                break;
            }
            case 'G': {
                // Import group list CSV (TG,Mode,Name)
                strncpy(opts->group_in_file, optarg, 1023);
                opts->group_in_file[1023] = '\0';
                if (csvGroupImport(opts, state) != 0) {
                    cli_set_exit_rc(out_exit_rc, 1);
                    return DSD_PARSE_ERROR;
                }
                LOG_NOTICE("Imported group list from %s\n", opts->group_in_file);
                break;
            }
            case 'R':
                strncpy(opts->symbol_out_file, optarg, 1023);
                opts->symbol_out_file[1023] = '\0';
                opts->symbol_out_file_is_auto = 0;
                break;
            case 'v': {
                // Filtering bitmap (PBF/LPF/HPF/HPFD) -- accepts hex or dec
                unsigned long bm = strtoul(optarg, NULL, 0);
                opts->use_pbf = (bm & 0x1) ? 1 : 0;
                opts->use_lpf = (bm & 0x2) ? 1 : 0;
                opts->use_hpf = (bm & 0x4) ? 1 : 0;
                opts->use_hpf_d = (bm & 0x8) ? 1 : 0;
                LOG_NOTICE("Filters: PBF=%d LPF=%d HPF=%d HPFD=%d\n", opts->use_pbf, opts->use_lpf, opts->use_hpf,
                           opts->use_hpf_d);
                break;
            }
            case 'f':
                // Any -f* preset should stop pure analog-monitor mode unless explicitly selecting it.
                opts->analog_only = 0;
                opts->monitor_input_audio = 0;
                if (optarg[0] == 'a') {
                    opts->frame_dstar = 1;
                    opts->frame_x2tdma = 1;
                    opts->frame_p25p1 = 1;
                    opts->frame_p25p2 = 1;
                    opts->inverted_p2 = 0;
                    opts->frame_nxdn48 = 1;
                    opts->frame_nxdn96 = 1;
                    opts->frame_dmr = 1;
                    opts->frame_dpmr = 1;
                    opts->frame_provoice = 1;
                    opts->frame_ysf = 1;
                    opts->frame_m17 = 1;
                    opts->mod_c4fm = 1;
                    opts->mod_qpsk = 0;
                    state->rf_mod = 0;
                    opts->dmr_stereo = 1;
                    opts->dmr_mono = 0;
                    opts->pulse_digi_rate_out = 8000;
                    opts->pulse_digi_out_channels = 2;
                    snprintf(opts->output_name, sizeof opts->output_name, "%s", "AUTO");
                    LOG_NOTICE("Decoding AUTO: all digital modes with multi-rate SPS hunting\n");
                } else if (optarg[0] == 'A') {
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
                    LOG_NOTICE("Only Monitoring Passive Analog Signal\n");
                } else if (optarg[0] == 'd') {
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
                    LOG_NOTICE("Decoding only DSTAR frames.\n");
                } else if (optarg[0] == 'x') {
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
                    opts->pulse_digi_out_channels = 2;
                    opts->dmr_stereo = 0;
                    opts->dmr_mono = 0;
                    state->dmr_stereo = 0;
                    snprintf(opts->output_name, sizeof opts->output_name, "%s", "X2-TDMA");
                    LOG_NOTICE("Decoding only X2-TDMA frames.\n");
                } else if (optarg[0] == 't') {
                    /* TDMA focus: P25 p1+p2 and DMR enabled; others off */
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
                } else if (optarg[0] == 'p') {
                    opts->frame_dstar = 0;
                    opts->frame_x2tdma = 0;
                    opts->frame_p25p1 = 0;
                    opts->frame_p25p2 = 0;
                    opts->frame_nxdn48 = 0;
                    opts->frame_nxdn96 = 0;
                    opts->frame_dmr = 0;
                    opts->frame_dpmr = 0;
                    opts->frame_provoice = 1;
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
                    LOG_NOTICE("Setting symbol rate to 9600 / second\n");
                    LOG_NOTICE("Decoding only ProVoice frames.\n");
                    LOG_NOTICE("EDACS Analog Voice Channels are Experimental.\n");
                    opts->rtl_dsp_bw_khz = 24;
                } else if (optarg[0] == 'h') {
                    if (optarg[1] != 0) {
                        char abits[2] = {optarg[1], 0};
                        char fbits[2] = {optarg[2], 0};
                        char sbits[2] = {optarg[3], 0};
                        state->edacs_a_bits = atoi(&abits[0]);
                        state->edacs_f_bits = atoi(&fbits[0]);
                        state->edacs_s_bits = atoi(&sbits[0]);
                    }
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
                    LOG_NOTICE("Setting symbol rate to 9600 / second\n");
                    LOG_NOTICE("Decoding EDACS STD/NET and ProVoice frames.\n");
                    LOG_NOTICE("EDACS Analog Voice Channels are Experimental.\n");
                    if (optarg[1] != 0) {
                        if ((state->edacs_a_bits + state->edacs_f_bits + state->edacs_s_bits) != 11) {
                            LOG_NOTICE("Invalid AFS Configuration: Reverting to Default.\n");
                            state->edacs_a_bits = 4;
                            state->edacs_f_bits = 4;
                            state->edacs_s_bits = 3;
                        }
                        LOG_NOTICE("AFS Setup in %d:%d:%d configuration.\n", state->edacs_a_bits, state->edacs_f_bits,
                                   state->edacs_s_bits);
                    }
                    opts->rtl_dsp_bw_khz = 24;
                } else if (optarg[0] == 'H') {
                    if (optarg[1] != 0) {
                        char abits[2] = {optarg[1], 0};
                        char fbits[2] = {optarg[2], 0};
                        char sbits[2] = {optarg[3], 0};
                        state->edacs_a_bits = atoi(&abits[0]);
                        state->edacs_f_bits = atoi(&fbits[0]);
                        state->edacs_s_bits = atoi(&sbits[0]);
                    }
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
                    state->esk_mask = 0xA0;
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
                    LOG_NOTICE("Setting symbol rate to 9600 / second\n");
                    LOG_NOTICE("Decoding EDACS Extended Addressing w/ ESK and ProVoice frames.\n");
                    LOG_NOTICE("EDACS Analog Voice Channels are Experimental.\n");
                    opts->rtl_dsp_bw_khz = 24;
                } else if (optarg[0] == 'e') {
                    if (optarg[1] != 0) {
                        char abits[2] = {optarg[1], 0};
                        char fbits[2] = {optarg[2], 0};
                        char sbits[2] = {optarg[3], 0};
                        state->edacs_a_bits = atoi(&abits[0]);
                        state->edacs_f_bits = atoi(&fbits[0]);
                        state->edacs_s_bits = atoi(&sbits[0]);
                    }
                    opts->frame_dstar = 0;
                    opts->frame_x2tdma = 0;
                    opts->frame_p25p1 = 0;
                    opts->frame_p25p2 = 0;
                    opts->frame_nxdn48 = 0;
                    opts->frame_nxdn96 = 0;
                    opts->frame_dmr = 0;
                    opts->frame_dpmr = 0;
                    opts->frame_provoice = 1;
                    state->ea_mode = 1;
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
                    LOG_NOTICE("Setting symbol rate to 9600 / second\n");
                    LOG_NOTICE("Decoding EDACS EA/ProVoice frames.\n");
                    LOG_NOTICE("EDACS Analog Voice Channels are Experimental.\n");
                    if (optarg[1] != 0) {
                        if ((state->edacs_a_bits + state->edacs_f_bits + state->edacs_s_bits) != 11) {
                            LOG_NOTICE("Invalid AFS Configuration: Reverting to Default.\n");
                            state->edacs_a_bits = 4;
                            state->edacs_f_bits = 4;
                            state->edacs_s_bits = 3;
                        }
                        LOG_NOTICE("AFS Setup in %d:%d:%d configuration.\n", state->edacs_a_bits, state->edacs_f_bits,
                                   state->edacs_s_bits);
                    }
                    opts->rtl_dsp_bw_khz = 24;
                } else if (optarg[0] == 'E') {
                    if (optarg[1] != 0) {
                        char abits[2] = {optarg[1], 0};
                        char fbits[2] = {optarg[2], 0};
                        char sbits[2] = {optarg[3], 0};
                        state->edacs_a_bits = atoi(&abits[0]);
                        state->edacs_f_bits = atoi(&fbits[0]);
                        state->edacs_s_bits = atoi(&sbits[0]);
                    }
                    opts->frame_dstar = 0;
                    opts->frame_x2tdma = 0;
                    opts->frame_p25p1 = 0;
                    opts->frame_p25p2 = 0;
                    opts->frame_nxdn48 = 0;
                    opts->frame_nxdn96 = 0;
                    opts->frame_dmr = 0;
                    opts->frame_dpmr = 0;
                    opts->frame_provoice = 1;
                    state->ea_mode = 1;
                    state->esk_mask = 0xA0;
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
                    LOG_NOTICE("Setting symbol rate to 9600 / second\n");
                    LOG_NOTICE("Decoding EDACS EA/ProVoice w/ ESK frames.\n");
                    LOG_NOTICE("EDACS Analog Voice Channels are Experimental.\n");
                    if (optarg[1] != 0) {
                        if ((state->edacs_a_bits + state->edacs_f_bits + state->edacs_s_bits) != 11) {
                            LOG_NOTICE("Invalid AFS Configuration: Reverting to Default.\n");
                            state->edacs_a_bits = 4;
                            state->edacs_f_bits = 4;
                            state->edacs_s_bits = 3;
                        }
                        LOG_NOTICE("AFS Setup in %d:%d:%d configuration.\n", state->edacs_a_bits, state->edacs_f_bits,
                                   state->edacs_s_bits);
                    }
                    opts->rtl_dsp_bw_khz = 24;
                } else if (optarg[0] == '1') {
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
                    opts->dmr_stereo = 0;
                    opts->dmr_mono = 0;
                    opts->pulse_digi_rate_out = 8000;
                    opts->pulse_digi_out_channels = 1;
                    opts->ssize = 36;
                    opts->msize = 15;
                    snprintf(opts->output_name, sizeof opts->output_name, "%s", "P25p1");
                    LOG_NOTICE("Decoding only P25 Phase 1 frames.\n");
                } else if (optarg[0] == '2') {
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
                    LOG_NOTICE("Decoding only P25 Phase 2 frames.\n");
                } else if (optarg[0] == 's') {
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
                    LOG_NOTICE("Decoding only DMR frames.\n");
                } else if (optarg[0] == 'r') {
                    /* Legacy -fr alias: DMR BS/MS simplex with mono audio.
                       Mirrors -fs but prefers mono content while keeping a
                       2-channel output interface for compatibility. */
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
                    opts->dmr_stereo = 0;
                    state->dmr_stereo = 0;
                    opts->dmr_mono = 1;
                    opts->pulse_digi_rate_out = 8000;
                    /* Use 2-channel digital output (like -fs) so audio
                       routing stays consistent across sinks; mono behavior
                       is provided by dmr_mono in the vocoder/mixers. */
                    opts->pulse_digi_out_channels = 2;
                    snprintf(opts->output_name, sizeof opts->output_name, "%s", "DMR-Mono");
                    LOG_NOTICE("Decoding DMR (legacy -fr mono mode).\n");
                } else if (optarg[0] == 'i') {
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
                    LOG_NOTICE("Decoding only NXDN48 frames.\n");
                } else if (optarg[0] == 'n') {
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
                    LOG_NOTICE("Decoding only NXDN96 frames.\n");
                } else if (optarg[0] == 'y') {
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
                    LOG_NOTICE("Decoding only YSF frames.\n");
                } else if (optarg[0] == 'm') {
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
                    snprintf(opts->output_name, sizeof opts->output_name, "%s", "M17");
                    LOG_NOTICE("Decoding only M17 frames (polarity auto-detected from preamble).\n");
                    opts->use_cosine_filter = 0;
                } else if (optarg[0] == 'Z') {
                    opts->m17encoder = 1;
                    opts->pulse_digi_rate_out = 48000;
                    opts->pulse_digi_out_channels = 1;
                    opts->use_lpf = 0;
                    opts->use_hpf = 0;
                    opts->use_pbf = 0;
                    opts->dmr_stereo = 0;
                    snprintf(opts->output_name, sizeof opts->output_name, "%s", "M17 Encoder");
                } else if (optarg[0] == 'B') {
                    opts->m17encoderbrt = 1;
                    opts->pulse_digi_rate_out = 48000;
                    opts->pulse_digi_out_channels = 1;
                    snprintf(opts->output_name, sizeof opts->output_name, "%s", "M17 BERT");
                } else if (optarg[0] == 'P') {
                    opts->m17encoderpkt = 1;
                    opts->pulse_digi_rate_out = 48000;
                    opts->pulse_digi_out_channels = 1;
                    snprintf(opts->output_name, sizeof opts->output_name, "%s", "M17 Packet");
                } else if (optarg[0] == 'U') {
                    opts->m17decoderip = 1;
                    opts->pulse_digi_rate_out = 8000;
                    opts->pulse_digi_out_channels = 1;
                    snprintf(opts->output_name, sizeof opts->output_name, "%s", "M17 IP Frame");
                    LOG_NOTICE("Decoding M17 UDP/IP Frames.\n");
                }
                break;
            case 'm':
                if (optarg[0] == 'a') {
                    opts->mod_c4fm = 1;
                    opts->mod_qpsk = 1;
                    opts->mod_gfsk = 1;
                    state->rf_mod = 0;
                    opts->mod_cli_lock = 0;
                    LOG_NOTICE("Don't use the -ma switch.\n");
                } else if (optarg[0] == 'c') {
                    opts->mod_c4fm = 1;
                    opts->mod_qpsk = 0;
                    opts->mod_gfsk = 0;
                    state->rf_mod = 0;
                    opts->mod_cli_lock = 1;
                    LOG_NOTICE("Enabling only C4FM modulation optimizations.\n");
                } else if (optarg[0] == 'g') {
                    opts->mod_c4fm = 0;
                    opts->mod_qpsk = 0;
                    opts->mod_gfsk = 1;
                    state->rf_mod = 2;
                    opts->mod_cli_lock = 1;
                    LOG_NOTICE("Enabling only GFSK modulation optimizations.\n");
                } else if (optarg[0] == 'q') {
                    opts->mod_c4fm = 0;
                    opts->mod_qpsk = 1;
                    opts->mod_gfsk = 0;
                    state->rf_mod = 1;
                    opts->mod_cli_lock = 1;
                    LOG_NOTICE("Enabling only QPSK modulation optimizations.\n");
                } else if (optarg[0] == '2') {
                    opts->mod_c4fm = 0;
                    opts->mod_qpsk = 1;
                    opts->mod_gfsk = 0;
                    state->rf_mod = 1;
                    state->samplesPerSymbol = 8;
                    state->symbolCenter = 3;
                    opts->mod_cli_lock = 1;
                    LOG_NOTICE("Enabling 6000 sps P25p2 QPSK.\n");
                } else if (optarg[0] == '3') {
                    opts->mod_c4fm = 1;
                    opts->mod_qpsk = 0;
                    opts->mod_gfsk = 0;
                    state->rf_mod = 0;
                    state->samplesPerSymbol = 10;
                    state->symbolCenter = 4;
                    opts->mod_cli_lock = 1;
                    LOG_NOTICE("Enabling 6000 sps P25p2 C4FM.\n");
                } else if (optarg[0] == '4') {
                    opts->mod_c4fm = 1;
                    opts->mod_qpsk = 1;
                    opts->mod_gfsk = 1;
                    state->rf_mod = 0;
                    state->samplesPerSymbol = 8;
                    state->symbolCenter = 3;
                    opts->mod_cli_lock = 0;
                    LOG_NOTICE("Enabling 6000 sps P25p2 all optimizations.\n");
                }
                break;
            case 'u':
                sscanf(optarg, "%i", &opts->uvquality);
                if (opts->uvquality < 1) {
                    opts->uvquality = 1;
                } else if (opts->uvquality > 64) {
                    opts->uvquality = 64;
                }
                LOG_NOTICE("Setting unvoice speech quality to %i waves per band.\n", opts->uvquality);
                break;
            case 's': {
                // Sample rate for WAV/RAW input files
                int sr = atoi(optarg);
                if (sr < 8000) {
                    sr = 8000;
                }
                if (sr > 192000) {
                    sr = 192000;
                }
                opts->wav_sample_rate = sr;
                opts->wav_interpolator = opts->wav_sample_rate / opts->wav_decimator;
                state->samplesPerSymbol = state->samplesPerSymbol * opts->wav_interpolator;
                state->symbolCenter = state->symbolCenter * opts->wav_interpolator;
                LOG_NOTICE("WAV input sample rate: %d Hz (interp=%d)\n", opts->wav_sample_rate, opts->wav_interpolator);
                break;
            }
            case 'J':
                // Event log output
                strncpy(opts->event_out_file, optarg, 1023);
                opts->event_out_file[1023] = '\0';
                LOG_NOTICE("Event log file: %s\n", opts->event_out_file);
                break;
            case 'L':
                // LRRP output
                strncpy(opts->lrrp_out_file, optarg, 1023);
                opts->lrrp_out_file[1023] = '\0';
                opts->lrrp_file_output = 1;
                LOG_NOTICE("LRRP output file: %s\n", opts->lrrp_out_file);
                break;
            case 'x':
                if (optarg[0] == 'x') {
                    opts->inverted_x2tdma = 0;
                    LOG_NOTICE("Expecting non-inverted X2-TDMA signals.\n");
                } else if (optarg[0] == 'r') {
                    opts->inverted_dmr = 1;
                    LOG_NOTICE("Expecting inverted DMR signals.\n");
                } else if (optarg[0] == 'd') {
                    opts->inverted_dpmr = 1;
                    LOG_NOTICE("Expecting inverted ICOM dPMR signals.\n");
                } else if (optarg[0] == 'z') {
                    opts->inverted_m17 = 1;
                    LOG_NOTICE("Expecting inverted M17 signals.\n");
                }
                break;
            case 'r':
                opts->playfiles = 1;
                opts->errorbars = 0;
                opts->datascope = 0;
                opts->pulse_digi_rate_out = 48000;
                opts->pulse_digi_out_channels = 1;
                opts->dmr_stereo = 0;
                state->dmr_stereo = 0;
                snprintf(opts->output_name, sizeof opts->output_name, "%s", "MBE Playback");
                state->optind = optind;
                break;
            case 'l': opts->use_cosine_filter = 0; break;
            case 't':
                /* Trunking/scan hangtime seconds (0 = immediate release) */
                opts->trunk_hangtime = (float)atof(optarg);
                if (opts->trunk_hangtime < 0.0f) {
                    opts->trunk_hangtime = 2.0f;
                }
                break;
            case 'q':
                // Reverse mute: mute clear and unmute encrypted
                opts->reverse_mute = 1;
                LOG_NOTICE("Reverse mute enabled (mute clear, unmute encrypted).\n");
                break;
            case 'X': {
                // Manually set P25p2 WACN/SYSID/NAC via hex string, e.g., BEE00ABC123
                const char* s = optarg;
                size_t len = strlen(s);
                if (len >= 11) {
                    char wb[6] = {0}, sb[4] = {0}, nb[4] = {0};
                    memcpy(wb, s, 5);
                    memcpy(sb, s + 5, 3);
                    memcpy(nb, s + 8, 3);
                    unsigned long w = strtoul(wb, NULL, 16);
                    unsigned long sy = strtoul(sb, NULL, 16);
                    unsigned long na = strtoul(nb, NULL, 16);
                    state->p2_wacn = w & 0xFFFFF;
                    state->p2_sysid = sy & 0xFFF;
                    state->p2_cc = na & 0xFFF;
                    LOG_NOTICE("P25p2 manual WACN/SYSID/NAC set: %05lX/%03lX/%03lX\n", state->p2_wacn, state->p2_sysid,
                               state->p2_cc);
                } else {
                    LOG_WARNING("-X expects 11 hex chars (WACN[5]+SYSID[3]+NAC[3]), e.g., BEE00ABC123\n");
                }
                break;
            }
            case 'b': {
                // Manually enter Basic Privacy key number (decimal 0..255)
                long v = strtol(optarg, NULL, 10);
                if (v < 0) {
                    v = 0;
                }
                if (v > 255) {
                    v = 255;
                }
                state->K = v;
                LOG_NOTICE("Basic Privacy key number set to %ld (forced priority)\n", v);
                break;
            }
            case 'D': {
                // Manually set DMR TIII Location Area n-bit length
                long n = strtol(optarg, NULL, 10);
                if (n < 0) {
                    n = 0;
                }
                if (n > 10) {
                    n = 10;
                }
                opts->dmr_dmrla_is_set = 1;
                opts->dmr_dmrla_n = (uint8_t)n;
                LOG_NOTICE("DMR TIII Location Area n-bit length set to %ld\n", n);
                break;
            }
            case '4':
                // Force Privacy Key over Encryption Identifiers
                state->M = 1;
                LOG_NOTICE("Force Privacy Key priority enabled\n");
                break;
            default:
                dsd_cli_usage();
                cli_set_exit_rc(out_exit_rc, 1);
                return DSD_PARSE_ERROR;
        }
    }
    return DSD_PARSE_CONTINUE;
}
