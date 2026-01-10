// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * User-facing INI configuration for DSD-neo.
 *
 * Parses and writes a small, stable subset of options (input/output/mode/
 * trunking) to allow users to persist common preferences without impacting
 * existing CLI/environment workflows.
 */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/file_compat.h>
#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/config_schema.h>

#include <cmath>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#endif

// Internal helpers ------------------------------------------------------------

// Local helper to convert linear power to dB (avoids dependency on dsd_misc.c)
static double
local_pwr_to_dB(double mean_power) {
    if (mean_power <= 0) {
        return -120.0;
    }
    double dB = 10.0 * std::log10(mean_power);
    if (dB > 0.0) {
        dB = 0.0;
    }
    if (dB < -120.0) {
        dB = -120.0;
    }
    return dB;
}

static void
user_cfg_reset(dsdneoUserConfig* cfg) {
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->version = 1;
    // Set trunking tune defaults to match main.c init defaults
    cfg->trunk_tune_group_calls = 1;
    cfg->trunk_tune_private_calls = 1;
    cfg->trunk_tune_data_calls = 0;
    cfg->trunk_tune_enc_calls = 1;

    cfg->rtl_auto_ppm = 0;

    // Recording defaults (match initOpts)
    cfg->per_call_wav = 0;
    snprintf(cfg->per_call_wav_dir, sizeof cfg->per_call_wav_dir, "%s", "./WAV");
    cfg->per_call_wav_dir[sizeof cfg->per_call_wav_dir - 1] = '\0';

    // DSP defaults (match runtime defaults)
    cfg->iq_balance = 0;
    cfg->iq_dc_block = 0;
}

static void
trim_whitespace(char* s) {
    char* p = s;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

static void
strip_inline_comment(char* s) {
    int in_quote = 0;
    for (char* p = s; *p; ++p) {
        if (*p == '"') {
            in_quote = !in_quote;
            continue;
        }
        if (!in_quote && (*p == '#' || *p == ';')) {
            *p = '\0';
            break;
        }
    }
}

static void
unquote(char* s) {
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

static int
parse_bool(const char* v, int* out) {
    if (!v || !*v || !out) {
        return -1;
    }
    if (dsd_strcasecmp(v, "1") == 0 || dsd_strcasecmp(v, "true") == 0 || dsd_strcasecmp(v, "yes") == 0
        || dsd_strcasecmp(v, "on") == 0) {
        *out = 1;
        return 0;
    }
    if (dsd_strcasecmp(v, "0") == 0 || dsd_strcasecmp(v, "false") == 0 || dsd_strcasecmp(v, "no") == 0
        || dsd_strcasecmp(v, "off") == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static long
parse_int(const char* v, long defv) {
    if (!v || !*v) {
        return defv;
    }
    char* end = NULL;
    long x = strtol(v, &end, 10);
    if (end == v) {
        return defv;
    }
    return x;
}

/**
 * @brief Copy a path value with shell-like expansion (~, $VAR, ${VAR}).
 *
 * Expands the source path and copies it to the destination buffer.
 * Falls back to verbatim copy if expansion fails.
 */
static void
copy_path_expanded(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src || !*src) {
        dst[0] = '\0';
        return;
    }

    char expanded[1024];
    if (dsd_config_expand_path(src, expanded, sizeof(expanded)) == 0) {
        snprintf(dst, dst_size, "%s", expanded);
    } else {
        /* Expansion failed (truncation or error), use verbatim */
        snprintf(dst, dst_size, "%s", src);
    }
    dst[dst_size - 1] = '\0';
}

static void
ensure_dir_exists(const char* path) {
    if (!path || !*path) {
        return;
    }

    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    buf[sizeof buf - 1] = '\0';

    // strip trailing slash if present
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '/' || buf[n - 1] == '\\')) {
        buf[--n] = '\0';
    }

    if (n == 0) {
        return;
    }

    // Walk components and mkdir progressively.
    char* p = buf;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            if (strlen(buf) > 0) {
                dsd_mkdir(buf, 0700);
            }
            *p = saved;
        }
        p++;
    }
    dsd_mkdir(buf, 0700);
}

// Default path ---------------------------------------------------------------

const char*
dsd_user_config_default_path(void) {
    static char buf[1024];
    static int inited = 0;

    if (inited) {
        return buf[0] ? buf : NULL;
    }

    buf[0] = '\0';

#if defined(_WIN32)
    const char* appdata = getenv("APPDATA");
    if (appdata && *appdata) {
        snprintf(buf, sizeof buf, "%s\\dsd-neo\\config.ini", appdata);
        buf[sizeof buf - 1] = '\0';
    }
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(buf, sizeof buf, "%s/dsd-neo/config.ini", xdg);
        buf[sizeof buf - 1] = '\0';
    } else {
        const char* home = getenv("HOME");
        if (home && *home) {
            snprintf(buf, sizeof buf, "%s/.config/dsd-neo/config.ini", home);
            buf[sizeof buf - 1] = '\0';
        }
    }
#endif

    inited = 1;
    return buf[0] ? buf : NULL;
}

// INI loader ------------------------------------------------------------------

/* Forward declarations for include processing */
static int process_includes(const char* path, dsdneoUserConfig* cfg, int depth, const char** include_stack,
                            int include_stack_size);

/* Internal loader that does NOT reset the config struct.
 * Used for accumulating values from multiple files (includes). */
static int
user_config_load_no_reset(const char* path, dsdneoUserConfig* cfg) {
    if (!cfg || !path || !*path) {
        return -1;
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    char line[1024];
    char current_section[64];
    current_section[0] = '\0';

    while (fgets(line, sizeof line, fp)) {
        trim_whitespace(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (!end) {
                continue;
            }
            *end = '\0';
            snprintf(current_section, sizeof current_section, "%s", line + 1);
            trim_whitespace(current_section);
            for (char* p = current_section; *p; ++p) {
                *p = (char)tolower((unsigned char)*p);
            }
            continue;
        }

        strip_inline_comment(line);
        if (line[0] == '\0') {
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        trim_whitespace(key);
        trim_whitespace(val);
        unquote(val);

        char key_lc[64];
        snprintf(key_lc, sizeof key_lc, "%.*s", (int)(sizeof key_lc - 1), key);
        key_lc[sizeof key_lc - 1] = '\0';
        for (char* p = key_lc; *p; ++p) {
            *p = (char)tolower((unsigned char)*p);
        }

        if (current_section[0] == '\0') {
            // Top-level keys
            if (strcmp(key_lc, "version") == 0) {
                cfg->version = (int)parse_int(val, 1);
            }
            continue;
        }

        if (strcmp(current_section, "input") == 0) {
            cfg->has_input = 1;
            if (strcmp(key_lc, "source") == 0) {
                if (dsd_strcasecmp(val, "pulse") == 0) {
                    cfg->input_source = DSDCFG_INPUT_PULSE;
                } else if (dsd_strcasecmp(val, "rtl") == 0) {
                    cfg->input_source = DSDCFG_INPUT_RTL;
                } else if (dsd_strcasecmp(val, "rtltcp") == 0) {
                    cfg->input_source = DSDCFG_INPUT_RTLTCP;
                } else if (dsd_strcasecmp(val, "file") == 0) {
                    cfg->input_source = DSDCFG_INPUT_FILE;
                } else if (dsd_strcasecmp(val, "tcp") == 0) {
                    cfg->input_source = DSDCFG_INPUT_TCP;
                } else if (dsd_strcasecmp(val, "udp") == 0) {
                    cfg->input_source = DSDCFG_INPUT_UDP;
                }
            } else if (strcmp(key_lc, "pulse_source") == 0 || strcmp(key_lc, "pulse_input") == 0) {
                snprintf(cfg->pulse_input, sizeof cfg->pulse_input, "%s", val);
                cfg->pulse_input[sizeof cfg->pulse_input - 1] = '\0';
            } else if (strcmp(key_lc, "rtl_device") == 0) {
                cfg->rtl_device = (int)parse_int(val, 0);
            } else if (strcmp(key_lc, "rtl_freq") == 0) {
                snprintf(cfg->rtl_freq, sizeof cfg->rtl_freq, "%s", val);
                cfg->rtl_freq[sizeof cfg->rtl_freq - 1] = '\0';
            } else if (strcmp(key_lc, "rtl_gain") == 0) {
                cfg->rtl_gain = (int)parse_int(val, 22);
            } else if (strcmp(key_lc, "rtl_ppm") == 0) {
                cfg->rtl_ppm = (int)parse_int(val, 0);
            } else if (strcmp(key_lc, "rtl_bw_khz") == 0) {
                cfg->rtl_bw_khz = (int)parse_int(val, 12);
            } else if (strcmp(key_lc, "rtl_sql") == 0) {
                cfg->rtl_sql = (int)parse_int(val, 0);
            } else if (strcmp(key_lc, "rtl_volume") == 0) {
                cfg->rtl_volume = (int)parse_int(val, 1);
            } else if (strcmp(key_lc, "auto_ppm") == 0 || strcmp(key_lc, "rtl_auto_ppm") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->rtl_auto_ppm = b;
                }
            } else if (strcmp(key_lc, "rtltcp_host") == 0) {
                snprintf(cfg->rtltcp_host, sizeof cfg->rtltcp_host, "%s", val);
                cfg->rtltcp_host[sizeof cfg->rtltcp_host - 1] = '\0';
            } else if (strcmp(key_lc, "rtltcp_port") == 0) {
                cfg->rtltcp_port = (int)parse_int(val, 1234);
            } else if (strcmp(key_lc, "file_path") == 0) {
                copy_path_expanded(cfg->file_path, sizeof cfg->file_path, val);
            } else if (strcmp(key_lc, "file_sample_rate") == 0) {
                cfg->file_sample_rate = (int)parse_int(val, 48000);
            } else if (strcmp(key_lc, "tcp_host") == 0) {
                snprintf(cfg->tcp_host, sizeof cfg->tcp_host, "%s", val);
                cfg->tcp_host[sizeof cfg->tcp_host - 1] = '\0';
            } else if (strcmp(key_lc, "tcp_port") == 0) {
                cfg->tcp_port = (int)parse_int(val, 7355);
            } else if (strcmp(key_lc, "udp_addr") == 0) {
                snprintf(cfg->udp_addr, sizeof cfg->udp_addr, "%s", val);
                cfg->udp_addr[sizeof cfg->udp_addr - 1] = '\0';
            } else if (strcmp(key_lc, "udp_port") == 0) {
                cfg->udp_port = (int)parse_int(val, 7355);
            }
            continue;
        }

        if (strcmp(current_section, "output") == 0) {
            cfg->has_output = 1;
            if (strcmp(key_lc, "backend") == 0) {
                if (dsd_strcasecmp(val, "pulse") == 0) {
                    cfg->output_backend = DSDCFG_OUTPUT_PULSE;
                } else if (dsd_strcasecmp(val, "null") == 0) {
                    cfg->output_backend = DSDCFG_OUTPUT_NULL;
                }
            } else if (strcmp(key_lc, "pulse_sink") == 0 || strcmp(key_lc, "pulse_output") == 0) {
                snprintf(cfg->pulse_output, sizeof cfg->pulse_output, "%s", val);
                cfg->pulse_output[sizeof cfg->pulse_output - 1] = '\0';
            } else if (strcmp(key_lc, "ncurses_ui") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->ncurses_ui = b;
                }
            }
            continue;
        }

        if (strcmp(current_section, "mode") == 0) {
            cfg->has_mode = 1;
            if (strcmp(key_lc, "decode") == 0) {
                if (dsd_strcasecmp(val, "auto") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_AUTO;
                } else if (dsd_strcasecmp(val, "p25p1") == 0 || dsd_strcasecmp(val, "p25p1_only") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_P25P1;
                } else if (dsd_strcasecmp(val, "p25p2") == 0 || dsd_strcasecmp(val, "p25p2_only") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_P25P2;
                } else if (dsd_strcasecmp(val, "dmr") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_DMR;
                } else if (dsd_strcasecmp(val, "nxdn48") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_NXDN48;
                } else if (dsd_strcasecmp(val, "nxdn96") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_NXDN96;
                } else if (dsd_strcasecmp(val, "x2tdma") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_X2TDMA;
                } else if (dsd_strcasecmp(val, "ysf") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_YSF;
                } else if (dsd_strcasecmp(val, "dstar") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_DSTAR;
                } else if (dsd_strcasecmp(val, "edacs") == 0 || dsd_strcasecmp(val, "provoice") == 0
                           || dsd_strcasecmp(val, "edacs_pv") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_EDACS_PV;
                } else if (dsd_strcasecmp(val, "dpmr") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_DPMR;
                } else if (dsd_strcasecmp(val, "m17") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_M17;
                } else if (dsd_strcasecmp(val, "tdma") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_TDMA;
                } else if (dsd_strcasecmp(val, "analog") == 0 || dsd_strcasecmp(val, "analog_monitor") == 0) {
                    cfg->decode_mode = DSDCFG_MODE_ANALOG;
                }
            } else if (strcmp(key_lc, "demod") == 0) {
                cfg->has_demod = 1;
                if (dsd_strcasecmp(val, "auto") == 0) {
                    cfg->demod_path = DSDCFG_DEMOD_AUTO;
                } else if (dsd_strcasecmp(val, "c4fm") == 0) {
                    cfg->demod_path = DSDCFG_DEMOD_C4FM;
                } else if (dsd_strcasecmp(val, "gfsk") == 0) {
                    cfg->demod_path = DSDCFG_DEMOD_GFSK;
                } else if (dsd_strcasecmp(val, "qpsk") == 0) {
                    cfg->demod_path = DSDCFG_DEMOD_QPSK;
                }
            }
            continue;
        }

        if (strcmp(current_section, "trunking") == 0) {
            cfg->has_trunking = 1;
            if (strcmp(key_lc, "enabled") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->trunk_enabled = b;
                }
            } else if (strcmp(key_lc, "chan_csv") == 0) {
                copy_path_expanded(cfg->trunk_chan_csv, sizeof cfg->trunk_chan_csv, val);
            } else if (strcmp(key_lc, "group_csv") == 0) {
                copy_path_expanded(cfg->trunk_group_csv, sizeof cfg->trunk_group_csv, val);
            } else if (strcmp(key_lc, "allow_list") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->trunk_use_allow_list = b;
                }
            } else if (strcmp(key_lc, "tune_group_calls") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->trunk_tune_group_calls = b;
                }
            } else if (strcmp(key_lc, "tune_private_calls") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->trunk_tune_private_calls = b;
                }
            } else if (strcmp(key_lc, "tune_data_calls") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->trunk_tune_data_calls = b;
                }
            } else if (strcmp(key_lc, "tune_enc_calls") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->trunk_tune_enc_calls = b;
                }
            }
            continue;
        }

        if (strcmp(current_section, "logging") == 0) {
            cfg->has_logging = 1;
            if (strcmp(key_lc, "event_log") == 0 || strcmp(key_lc, "event_log_file") == 0) {
                copy_path_expanded(cfg->event_log, sizeof cfg->event_log, val);
            }
            continue;
        }

        if (strcmp(current_section, "recording") == 0) {
            cfg->has_recording = 1;
            if (strcmp(key_lc, "per_call_wav") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->per_call_wav = b;
                }
            } else if (strcmp(key_lc, "per_call_wav_dir") == 0) {
                copy_path_expanded(cfg->per_call_wav_dir, sizeof cfg->per_call_wav_dir, val);
            } else if (strcmp(key_lc, "static_wav") == 0) {
                copy_path_expanded(cfg->static_wav_path, sizeof cfg->static_wav_path, val);
            } else if (strcmp(key_lc, "raw_wav") == 0) {
                copy_path_expanded(cfg->raw_wav_path, sizeof cfg->raw_wav_path, val);
            }
            continue;
        }

        if (strcmp(current_section, "dsp") == 0) {
            cfg->has_dsp = 1;
            if (strcmp(key_lc, "iq_balance") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->iq_balance = b;
                }
            } else if (strcmp(key_lc, "iq_dc_block") == 0) {
                int b = 0;
                if (parse_bool(val, &b) == 0) {
                    cfg->iq_dc_block = b;
                }
            }
            continue;
        }
    }

    fclose(fp);
    return 0;
}

int
dsd_user_config_load(const char* path, dsdneoUserConfig* cfg) {
    if (!cfg) {
        return -1;
    }

    user_cfg_reset(cfg);

    /* Process includes first (they provide base values that can be overridden) */
    const char* stack[1] = {path};
    process_includes(path, cfg, 0, stack, 1);

    /* Now load the main config (which overrides included values) */
    return user_config_load_no_reset(path, cfg);
}

// INI writer ------------------------------------------------------------------

int
dsd_user_config_save_atomic(const char* path, const dsdneoUserConfig* cfg) {
    if (!path || !*path || !cfg) {
        return -1;
    }

    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    dir[sizeof dir - 1] = '\0';
    char* last_sep = strrchr(dir, '/');
#if defined(_WIN32)
    char* last_sep2 = strrchr(dir, '\\');
    if (!last_sep || (last_sep2 && last_sep2 > last_sep)) {
        last_sep = last_sep2;
    }
#endif
    if (last_sep) {
        *last_sep = '\0';
        ensure_dir_exists(dir);
    }

    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    tmp[sizeof tmp - 1] = '\0';

    FILE* fp = fopen(tmp, "w");
    if (!fp) {
        return -1;
    }

    dsd_user_config_render_ini(cfg, fp);

    if (fflush(fp) != 0) {
        fclose(fp);
        return -1;
    }

    int fd = dsd_fileno(fp);
    if (fd >= 0) {
        (void)dsd_fchmod(fd, 0600);
        (void)dsd_fsync(fd);
    }

    fclose(fp);

#if defined(_WIN32)
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        (void)remove(tmp);
        return -1;
    }
#else
    if (rename(tmp, path) != 0) {
        (void)remove(tmp);
        return -1;
    }
#endif

    return 0;
}

void
dsd_user_config_render_ini(const dsdneoUserConfig* cfg, FILE* out) {
    if (!cfg || !out) {
        return;
    }

    fprintf(out, "version = %d\n\n", cfg->version > 0 ? cfg->version : 1);

    if (cfg->has_input) {
        fprintf(out, "[input]\n");
        switch (cfg->input_source) {
            case DSDCFG_INPUT_PULSE: fprintf(out, "source = \"pulse\"\n"); break;
            case DSDCFG_INPUT_RTL: fprintf(out, "source = \"rtl\"\n"); break;
            case DSDCFG_INPUT_RTLTCP: fprintf(out, "source = \"rtltcp\"\n"); break;
            case DSDCFG_INPUT_FILE: fprintf(out, "source = \"file\"\n"); break;
            case DSDCFG_INPUT_TCP: fprintf(out, "source = \"tcp\"\n"); break;
            case DSDCFG_INPUT_UDP: fprintf(out, "source = \"udp\"\n"); break;
            default: break;
        }
        if (cfg->input_source == DSDCFG_INPUT_PULSE) {
            if (cfg->pulse_input[0]) {
                fprintf(out, "pulse_source = \"%s\"\n", cfg->pulse_input);
            }
        } else if (cfg->input_source == DSDCFG_INPUT_RTL) {
            fprintf(out, "rtl_device = %d\n", cfg->rtl_device);
            if (cfg->rtl_freq[0]) {
                fprintf(out, "rtl_freq = \"%s\"\n", cfg->rtl_freq);
            }
            if (cfg->rtl_gain) {
                fprintf(out, "rtl_gain = %d\n", cfg->rtl_gain);
            }
            if (cfg->rtl_ppm) {
                fprintf(out, "rtl_ppm = %d\n", cfg->rtl_ppm);
            }
            if (cfg->rtl_bw_khz) {
                fprintf(out, "rtl_bw_khz = %d\n", cfg->rtl_bw_khz);
            }
            fprintf(out, "rtl_sql = %d\n", cfg->rtl_sql);
            if (cfg->rtl_volume) {
                fprintf(out, "rtl_volume = %d\n", cfg->rtl_volume);
            }
            fprintf(out, "auto_ppm = %s\n", cfg->rtl_auto_ppm ? "true" : "false");
        } else if (cfg->input_source == DSDCFG_INPUT_RTLTCP) {
            if (cfg->rtltcp_host[0]) {
                fprintf(out, "rtltcp_host = \"%s\"\n", cfg->rtltcp_host);
            }
            if (cfg->rtltcp_port) {
                fprintf(out, "rtltcp_port = %d\n", cfg->rtltcp_port);
            }
            if (cfg->rtl_freq[0]) {
                fprintf(out, "rtl_freq = \"%s\"\n", cfg->rtl_freq);
            }
            if (cfg->rtl_gain) {
                fprintf(out, "rtl_gain = %d\n", cfg->rtl_gain);
            }
            if (cfg->rtl_ppm) {
                fprintf(out, "rtl_ppm = %d\n", cfg->rtl_ppm);
            }
            if (cfg->rtl_bw_khz) {
                fprintf(out, "rtl_bw_khz = %d\n", cfg->rtl_bw_khz);
            }
            fprintf(out, "rtl_sql = %d\n", cfg->rtl_sql);
            if (cfg->rtl_volume) {
                fprintf(out, "rtl_volume = %d\n", cfg->rtl_volume);
            }
            fprintf(out, "auto_ppm = %s\n", cfg->rtl_auto_ppm ? "true" : "false");
        } else if (cfg->input_source == DSDCFG_INPUT_FILE) {
            if (cfg->file_path[0]) {
                fprintf(out, "file_path = \"%s\"\n", cfg->file_path);
            }
            if (cfg->file_sample_rate) {
                fprintf(out, "file_sample_rate = %d\n", cfg->file_sample_rate);
            }
        } else if (cfg->input_source == DSDCFG_INPUT_TCP) {
            if (cfg->tcp_host[0]) {
                fprintf(out, "tcp_host = \"%s\"\n", cfg->tcp_host);
            }
            if (cfg->tcp_port) {
                fprintf(out, "tcp_port = %d\n", cfg->tcp_port);
            }
        } else if (cfg->input_source == DSDCFG_INPUT_UDP) {
            if (cfg->udp_addr[0]) {
                fprintf(out, "udp_addr = \"%s\"\n", cfg->udp_addr);
            }
            if (cfg->udp_port) {
                fprintf(out, "udp_port = %d\n", cfg->udp_port);
            }
        }
        fprintf(out, "\n");
    }

    if (cfg->has_output) {
        fprintf(out, "[output]\n");
        switch (cfg->output_backend) {
            case DSDCFG_OUTPUT_PULSE: fprintf(out, "backend = \"pulse\"\n"); break;
            case DSDCFG_OUTPUT_NULL: fprintf(out, "backend = \"null\"\n"); break;
            default: break;
        }
        if (cfg->pulse_output[0]) {
            fprintf(out, "pulse_sink = \"%s\"\n", cfg->pulse_output);
        }
        fprintf(out, "ncurses_ui = %s\n", cfg->ncurses_ui ? "true" : "false");
        fprintf(out, "\n");
    }

    if (cfg->has_mode) {
        fprintf(out, "[mode]\n");
        switch (cfg->decode_mode) {
            case DSDCFG_MODE_AUTO: fprintf(out, "decode = \"auto\"\n"); break;
            case DSDCFG_MODE_P25P1: fprintf(out, "decode = \"p25p1\"\n"); break;
            case DSDCFG_MODE_P25P2: fprintf(out, "decode = \"p25p2\"\n"); break;
            case DSDCFG_MODE_DMR: fprintf(out, "decode = \"dmr\"\n"); break;
            case DSDCFG_MODE_NXDN48: fprintf(out, "decode = \"nxdn48\"\n"); break;
            case DSDCFG_MODE_NXDN96: fprintf(out, "decode = \"nxdn96\"\n"); break;
            case DSDCFG_MODE_X2TDMA: fprintf(out, "decode = \"x2tdma\"\n"); break;
            case DSDCFG_MODE_YSF: fprintf(out, "decode = \"ysf\"\n"); break;
            case DSDCFG_MODE_DSTAR: fprintf(out, "decode = \"dstar\"\n"); break;
            case DSDCFG_MODE_EDACS_PV: fprintf(out, "decode = \"edacs_pv\"\n"); break;
            case DSDCFG_MODE_DPMR: fprintf(out, "decode = \"dpmr\"\n"); break;
            case DSDCFG_MODE_M17: fprintf(out, "decode = \"m17\"\n"); break;
            case DSDCFG_MODE_TDMA: fprintf(out, "decode = \"tdma\"\n"); break;
            case DSDCFG_MODE_ANALOG: fprintf(out, "decode = \"analog\"\n"); break;
            default: break;
        }
        if (cfg->has_demod) {
            switch (cfg->demod_path) {
                case DSDCFG_DEMOD_AUTO: fprintf(out, "demod = \"auto\"\n"); break;
                case DSDCFG_DEMOD_C4FM: fprintf(out, "demod = \"c4fm\"\n"); break;
                case DSDCFG_DEMOD_GFSK: fprintf(out, "demod = \"gfsk\"\n"); break;
                case DSDCFG_DEMOD_QPSK: fprintf(out, "demod = \"qpsk\"\n"); break;
                default: break;
            }
        }
        fprintf(out, "\n");
    }

    if (cfg->has_trunking) {
        fprintf(out, "[trunking]\n");
        fprintf(out, "enabled = %s\n", cfg->trunk_enabled ? "true" : "false");
        if (cfg->trunk_chan_csv[0]) {
            fprintf(out, "chan_csv = \"%s\"\n", cfg->trunk_chan_csv);
        }
        if (cfg->trunk_group_csv[0]) {
            fprintf(out, "group_csv = \"%s\"\n", cfg->trunk_group_csv);
        }
        fprintf(out, "allow_list = %s\n", cfg->trunk_use_allow_list ? "true" : "false");
        fprintf(out, "tune_group_calls = %s\n", cfg->trunk_tune_group_calls ? "true" : "false");
        fprintf(out, "tune_private_calls = %s\n", cfg->trunk_tune_private_calls ? "true" : "false");
        fprintf(out, "tune_data_calls = %s\n", cfg->trunk_tune_data_calls ? "true" : "false");
        fprintf(out, "tune_enc_calls = %s\n", cfg->trunk_tune_enc_calls ? "true" : "false");
        fprintf(out, "\n");
    }

    if (cfg->has_logging) {
        fprintf(out, "[logging]\n");
        if (cfg->event_log[0]) {
            fprintf(out, "event_log = \"%s\"\n", cfg->event_log);
        }
        fprintf(out, "\n");
    }

    if (cfg->has_recording) {
        fprintf(out, "[recording]\n");
        fprintf(out, "per_call_wav = %s\n", cfg->per_call_wav ? "true" : "false");
        if (cfg->per_call_wav_dir[0]) {
            fprintf(out, "per_call_wav_dir = \"%s\"\n", cfg->per_call_wav_dir);
        }
        if (cfg->static_wav_path[0]) {
            fprintf(out, "static_wav = \"%s\"\n", cfg->static_wav_path);
        }
        if (cfg->raw_wav_path[0]) {
            fprintf(out, "raw_wav = \"%s\"\n", cfg->raw_wav_path);
        }
        fprintf(out, "\n");
    }

    if (cfg->has_dsp) {
        fprintf(out, "[dsp]\n");
        fprintf(out, "iq_balance = %s\n", cfg->iq_balance ? "true" : "false");
        fprintf(out, "iq_dc_block = %s\n", cfg->iq_dc_block ? "true" : "false");
        fprintf(out, "\n");
    }
}

// Mapping helpers -------------------------------------------------------------

void
dsd_apply_user_config_to_opts(const dsdneoUserConfig* cfg, dsd_opts* opts, dsd_state* state) {
    if (!cfg || !opts || !state) {
        return;
    }

    // Input source
    if (cfg->has_input) {
        switch (cfg->input_source) {
            case DSDCFG_INPUT_PULSE:
                if (cfg->pulse_input[0]) {
                    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "pulse:%s", cfg->pulse_input);
                } else {
                    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", "pulse");
                }
                break;
            case DSDCFG_INPUT_RTL:
                if (cfg->rtl_freq[0]) {
                    /* When gain/volume are omitted in config, preserve whatever
                     * defaults initOpts()/CLI already established (AGC and
                     * multiplier 2) instead of forcing hardcoded values. */
                    int gain = cfg->rtl_gain ? cfg->rtl_gain : opts->rtl_gain_value;
                    int ppm = cfg->rtl_ppm;
                    int bw = cfg->rtl_bw_khz ? cfg->rtl_bw_khz : opts->rtl_dsp_bw_khz;
                    int sql = cfg->rtl_sql;
                    int vol = cfg->rtl_volume ? cfg->rtl_volume : opts->rtl_volume_multiplier;
                    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "rtl:%d:%s:%d:%d:%d:%d:%d", cfg->rtl_device,
                             cfg->rtl_freq, gain, ppm, bw, sql, vol);
                }
                break;
            case DSDCFG_INPUT_RTLTCP:
                if (cfg->rtltcp_host[0]) {
                    if (cfg->rtl_freq[0]) {
                        int gain = cfg->rtl_gain ? cfg->rtl_gain : opts->rtl_gain_value;
                        int ppm = cfg->rtl_ppm;
                        int bw = cfg->rtl_bw_khz ? cfg->rtl_bw_khz : opts->rtl_dsp_bw_khz;
                        int sql = cfg->rtl_sql;
                        int vol = cfg->rtl_volume ? cfg->rtl_volume : opts->rtl_volume_multiplier;
                        snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "rtltcp:%s:%d:%s:%d:%d:%d:%d:%d",
                                 cfg->rtltcp_host, cfg->rtltcp_port ? cfg->rtltcp_port : 1234, cfg->rtl_freq, gain, ppm,
                                 bw, sql, vol);
                    } else {
                        snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "rtltcp:%s:%d", cfg->rtltcp_host,
                                 cfg->rtltcp_port ? cfg->rtltcp_port : 1234);
                    }
                }
                break;
            case DSDCFG_INPUT_FILE:
                if (cfg->file_path[0]) {
                    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "%s", cfg->file_path);
                    if (cfg->file_sample_rate > 0 && cfg->file_sample_rate != 48000) {
                        opts->wav_sample_rate = cfg->file_sample_rate;
                        if (opts->wav_decimator != 0) {
                            opts->wav_interpolator = opts->wav_sample_rate / opts->wav_decimator;
                        }
                    }
                }
                break;
            case DSDCFG_INPUT_TCP:
                if (cfg->tcp_host[0]) {
                    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "tcp:%s:%d", cfg->tcp_host,
                             cfg->tcp_port ? cfg->tcp_port : 7355);
                }
                break;
            case DSDCFG_INPUT_UDP:
                if (cfg->udp_addr[0]) {
                    snprintf(opts->audio_in_dev, sizeof opts->audio_in_dev, "udp:%s:%d", cfg->udp_addr,
                             cfg->udp_port ? cfg->udp_port : 7355);
                }
                break;
            default: break;
        }

        // RTL-only helpers
        if (cfg->input_source == DSDCFG_INPUT_RTL || cfg->input_source == DSDCFG_INPUT_RTLTCP) {
            opts->rtl_auto_ppm = cfg->rtl_auto_ppm ? 1 : 0;
            if (getenv("DSD_NEO_AUTO_PPM") == NULL) {
                dsd_setenv("DSD_NEO_AUTO_PPM", opts->rtl_auto_ppm ? "1" : "0", 0);
            }
        }
    }

    // Output backend and UI
    if (cfg->has_output) {
        switch (cfg->output_backend) {
            case DSDCFG_OUTPUT_PULSE:
                if (cfg->pulse_output[0]) {
                    snprintf(opts->audio_out_dev, sizeof opts->audio_out_dev, "pulse:%s", cfg->pulse_output);
                } else {
                    snprintf(opts->audio_out_dev, sizeof opts->audio_out_dev, "%s", "pulse");
                }
                break;
            case DSDCFG_OUTPUT_NULL: snprintf(opts->audio_out_dev, sizeof opts->audio_out_dev, "%s", "null"); break;
            default: break;
        }
        if (cfg->ncurses_ui) {
            opts->use_ncurses_terminal = 1;
        }
    }

    // Decode mode mapping (mirror interactive/bootstrap and -f presets)
    if (cfg->has_mode) {
        switch (cfg->decode_mode) {
            case DSDCFG_MODE_AUTO:
                // AUTO: keep init defaults and just tag name
                snprintf(opts->output_name, sizeof opts->output_name, "%s", "AUTO");
                break;
            case DSDCFG_MODE_P25P1:
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
            case DSDCFG_MODE_P25P2:
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
            case DSDCFG_MODE_DMR:
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
            case DSDCFG_MODE_NXDN48:
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
                opts->dmr_stereo = 0;
                opts->pulse_digi_rate_out = 8000;
                opts->pulse_digi_out_channels = 1;
                snprintf(opts->output_name, sizeof opts->output_name, "%s", "NXDN48");
                break;
            case DSDCFG_MODE_NXDN96:
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
                state->samplesPerSymbol = 20;
                state->symbolCenter = 9; /* (sps-1)/2 */
                opts->mod_c4fm = 1;
                opts->mod_qpsk = 0;
                opts->mod_gfsk = 0;
                state->rf_mod = 0;
                opts->dmr_stereo = 0;
                opts->pulse_digi_rate_out = 8000;
                opts->pulse_digi_out_channels = 1;
                snprintf(opts->output_name, sizeof opts->output_name, "%s", "NXDN96");
                break;
            case DSDCFG_MODE_X2TDMA:
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
                break;
            case DSDCFG_MODE_YSF:
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
                opts->dmr_stereo = 1;
                opts->dmr_mono = 0;
                opts->pulse_digi_rate_out = 8000;
                opts->pulse_digi_out_channels = 2;
                snprintf(opts->output_name, sizeof opts->output_name, "%s", "YSF");
                break;
            case DSDCFG_MODE_DSTAR:
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
                opts->dmr_mono = 0;
                state->dmr_stereo = 0;
                state->rf_mod = 0;
                snprintf(opts->output_name, sizeof opts->output_name, "%s", "DSTAR");
                break;
            case DSDCFG_MODE_EDACS_PV:
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
            case DSDCFG_MODE_DPMR:
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
                state->samplesPerSymbol = 20;
                state->symbolCenter = 9; /* (sps-1)/2 */
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
            case DSDCFG_MODE_M17:
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
                opts->use_cosine_filter = 0;
                snprintf(opts->output_name, sizeof opts->output_name, "%s", "M17");
                break;
            case DSDCFG_MODE_TDMA:
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
            case DSDCFG_MODE_ANALOG:
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
    }

    // Demodulator path lock/unlock
    if (cfg->has_demod) {
        switch (cfg->demod_path) {
            case DSDCFG_DEMOD_AUTO:
                opts->mod_c4fm = 1;
                opts->mod_qpsk = 1;
                opts->mod_gfsk = 1;
                opts->mod_cli_lock = 0;
                state->rf_mod = 0;
                break;
            case DSDCFG_DEMOD_C4FM:
                opts->mod_c4fm = 1;
                opts->mod_qpsk = 0;
                opts->mod_gfsk = 0;
                opts->mod_cli_lock = 1;
                state->rf_mod = 0;
                break;
            case DSDCFG_DEMOD_GFSK:
                opts->mod_c4fm = 0;
                opts->mod_qpsk = 0;
                opts->mod_gfsk = 1;
                opts->mod_cli_lock = 1;
                state->rf_mod = 2;
                break;
            case DSDCFG_DEMOD_QPSK:
                opts->mod_c4fm = 0;
                opts->mod_qpsk = 1;
                opts->mod_gfsk = 0;
                opts->mod_cli_lock = 1;
                state->rf_mod = 1;
                break;
            default: break;
        }
    }

    // Trunking: enable/CSV/allow-list flags.
    if (cfg->has_trunking) {
        if (cfg->trunk_enabled) {
            opts->p25_trunk = 1;
            opts->trunk_enable = 1;
        }
        if (cfg->trunk_chan_csv[0]) {
            snprintf(opts->chan_in_file, sizeof opts->chan_in_file, "%s", cfg->trunk_chan_csv);
            opts->chan_in_file[sizeof opts->chan_in_file - 1] = '\0';
        }
        if (cfg->trunk_group_csv[0]) {
            snprintf(opts->group_in_file, sizeof opts->group_in_file, "%s", cfg->trunk_group_csv);
            opts->group_in_file[sizeof opts->group_in_file - 1] = '\0';
        }
        opts->trunk_use_allow_list = cfg->trunk_use_allow_list ? 1 : 0;
        opts->trunk_tune_group_calls = cfg->trunk_tune_group_calls ? 1 : 0;
        opts->trunk_tune_private_calls = cfg->trunk_tune_private_calls ? 1 : 0;
        opts->trunk_tune_data_calls = cfg->trunk_tune_data_calls ? 1 : 0;
        opts->trunk_tune_enc_calls = cfg->trunk_tune_enc_calls ? 1 : 0;
    }

    if (cfg->has_logging) {
        snprintf(opts->event_out_file, sizeof opts->event_out_file, "%s", cfg->event_log);
        opts->event_out_file[sizeof opts->event_out_file - 1] = '\0';
    }

    if (cfg->has_recording) {
        if (cfg->per_call_wav_dir[0]) {
            snprintf(opts->wav_out_dir, sizeof opts->wav_out_dir, "%s", cfg->per_call_wav_dir);
            opts->wav_out_dir[sizeof opts->wav_out_dir - 1] = '\0';
        }

        // Per-call and static WAV are mutually exclusive (mirror CLI behavior).
        if (cfg->per_call_wav) {
            opts->dmr_stereo_wav = 1;
            opts->static_wav_file = 0;
        } else if (cfg->static_wav_path[0]) {
            opts->dmr_stereo_wav = 0;
            opts->static_wav_file = 1;
            snprintf(opts->wav_out_file, sizeof opts->wav_out_file, "%s", cfg->static_wav_path);
            opts->wav_out_file[sizeof opts->wav_out_file - 1] = '\0';
        } else {
            opts->dmr_stereo_wav = 0;
            opts->static_wav_file = 0;
        }

        if (cfg->raw_wav_path[0]) {
            snprintf(opts->wav_out_file_raw, sizeof opts->wav_out_file_raw, "%s", cfg->raw_wav_path);
            opts->wav_out_file_raw[sizeof opts->wav_out_file_raw - 1] = '\0';
        } else {
            opts->wav_out_file_raw[0] = '\0';
        }
    }

    if (cfg->has_dsp) {
        if (getenv("DSD_NEO_IQ_BALANCE") == NULL) {
            dsd_setenv("DSD_NEO_IQ_BALANCE", cfg->iq_balance ? "1" : "0", 0);
        }
        if (getenv("DSD_NEO_IQ_DC_BLOCK") == NULL) {
            dsd_setenv("DSD_NEO_IQ_DC_BLOCK", cfg->iq_dc_block ? "1" : "0", 0);
        }
    }
}

void
dsd_snapshot_opts_to_user_config(const dsd_opts* opts, const dsd_state* state, dsdneoUserConfig* cfg) {
    if (!opts || !state || !cfg) {
        return;
    }

    user_cfg_reset(cfg);

    // Input snapshot: infer from audio_in_dev prefix and parse fields where
    // possible so that a rendered INI can faithfully recreate the source.
    cfg->has_input = 1;
    if (strncmp(opts->audio_in_dev, "rtl:", 4) == 0) {
        cfg->input_source = DSDCFG_INPUT_RTL;
        char buf[1024];
        snprintf(buf, sizeof buf, "%.*s", (int)(sizeof buf - 1), opts->audio_in_dev);
        buf[sizeof buf - 1] = '\0';
        char* save = NULL;
        char* tok = dsd_strtok_r(buf, ":", &save); // "rtl"
        int idx = 0;
        while (tok) {
            if (idx == 1) {
                cfg->rtl_device = atoi(tok);
            } else if (idx == 2) {
                snprintf(cfg->rtl_freq, sizeof cfg->rtl_freq, "%s", tok);
                cfg->rtl_freq[sizeof cfg->rtl_freq - 1] = '\0';
            } else if (idx == 3) {
                cfg->rtl_gain = atoi(tok);
            } else if (idx == 4) {
                cfg->rtl_ppm = atoi(tok);
            } else if (idx == 5) {
                cfg->rtl_bw_khz = atoi(tok);
            } else if (idx == 6) {
                cfg->rtl_sql = atoi(tok);
            } else if (idx == 7) {
                cfg->rtl_volume = atoi(tok);
            }
            tok = dsd_strtok_r(NULL, ":", &save);
            idx++;
        }
        // Override with live opts values for settings that can change at runtime
        cfg->rtl_gain = opts->rtl_gain_value;
        cfg->rtl_ppm = opts->rtlsdr_ppm_error;
        cfg->rtl_bw_khz = opts->rtl_dsp_bw_khz;
        cfg->rtl_sql = (int)local_pwr_to_dB(opts->rtl_squelch_level);
        cfg->rtl_volume = opts->rtl_volume_multiplier;
        // Update frequency from live value (stored as Hz in opts)
        if (opts->rtlsdr_center_freq > 0) {
            snprintf(cfg->rtl_freq, sizeof cfg->rtl_freq, "%u", opts->rtlsdr_center_freq);
            cfg->rtl_freq[sizeof cfg->rtl_freq - 1] = '\0';
        }
    } else if (strncmp(opts->audio_in_dev, "rtltcp:", 7) == 0) {
        cfg->input_source = DSDCFG_INPUT_RTLTCP;
        char buf[1024];
        snprintf(buf, sizeof buf, "%.*s", (int)(sizeof buf - 1), opts->audio_in_dev);
        buf[sizeof buf - 1] = '\0';
        char* save = NULL;
        char* tok = dsd_strtok_r(buf, ":", &save); // "rtltcp"
        int idx = 0;
        while (tok) {
            if (idx == 1) {
                snprintf(cfg->rtltcp_host, sizeof cfg->rtltcp_host, "%s", tok);
                cfg->rtltcp_host[sizeof cfg->rtltcp_host - 1] = '\0';
            } else if (idx == 2) {
                cfg->rtltcp_port = atoi(tok);
            } else if (idx == 3) {
                snprintf(cfg->rtl_freq, sizeof cfg->rtl_freq, "%s", tok);
                cfg->rtl_freq[sizeof cfg->rtl_freq - 1] = '\0';
            } else if (idx == 4) {
                cfg->rtl_gain = atoi(tok);
            } else if (idx == 5) {
                cfg->rtl_ppm = atoi(tok);
            } else if (idx == 6) {
                cfg->rtl_bw_khz = atoi(tok);
            } else if (idx == 7) {
                cfg->rtl_sql = atoi(tok);
            } else if (idx == 8) {
                cfg->rtl_volume = atoi(tok);
            }
            tok = dsd_strtok_r(NULL, ":", &save);
            idx++;
        }
        // Override with live opts values for settings that can change at runtime
        cfg->rtl_gain = opts->rtl_gain_value;
        cfg->rtl_ppm = opts->rtlsdr_ppm_error;
        cfg->rtl_bw_khz = opts->rtl_dsp_bw_khz;
        cfg->rtl_sql = (int)local_pwr_to_dB(opts->rtl_squelch_level);
        cfg->rtl_volume = opts->rtl_volume_multiplier;
        // Update frequency from live value (stored as Hz in opts)
        if (opts->rtlsdr_center_freq > 0) {
            snprintf(cfg->rtl_freq, sizeof cfg->rtl_freq, "%u", opts->rtlsdr_center_freq);
            cfg->rtl_freq[sizeof cfg->rtl_freq - 1] = '\0';
        }
    } else if (strncmp(opts->audio_in_dev, "tcp:", 4) == 0) {
        cfg->input_source = DSDCFG_INPUT_TCP;
        char buf[512];
        snprintf(buf, sizeof buf, "%.*s", (int)(sizeof buf - 1), opts->audio_in_dev);
        buf[sizeof buf - 1] = '\0';
        char* save = NULL;
        char* tok = dsd_strtok_r(buf, ":", &save); // "tcp"
        int idx = 0;
        while (tok) {
            if (idx == 1) {
                snprintf(cfg->tcp_host, sizeof cfg->tcp_host, "%s", tok);
                cfg->tcp_host[sizeof cfg->tcp_host - 1] = '\0';
            } else if (idx == 2) {
                cfg->tcp_port = atoi(tok);
            }
            tok = dsd_strtok_r(NULL, ":", &save);
            idx++;
        }
    } else if (strncmp(opts->audio_in_dev, "udp:", 4) == 0) {
        cfg->input_source = DSDCFG_INPUT_UDP;
        char buf[512];
        snprintf(buf, sizeof buf, "%.*s", (int)(sizeof buf - 1), opts->audio_in_dev);
        buf[sizeof buf - 1] = '\0';
        char* save = NULL;
        char* tok = dsd_strtok_r(buf, ":", &save); // "udp"
        int idx = 0;
        while (tok) {
            if (idx == 1) {
                snprintf(cfg->udp_addr, sizeof cfg->udp_addr, "%s", tok);
                cfg->udp_addr[sizeof cfg->udp_addr - 1] = '\0';
            } else if (idx == 2) {
                cfg->udp_port = atoi(tok);
            }
            tok = dsd_strtok_r(NULL, ":", &save);
            idx++;
        }
    } else if (strncmp(opts->audio_in_dev, "pulse", 5) == 0) {
        cfg->input_source = DSDCFG_INPUT_PULSE;
        const char* dev = NULL;
        if (opts->audio_in_dev[5] == ':') {
            dev = opts->audio_in_dev + 6;
        }
        if (dev && *dev) {
            snprintf(cfg->pulse_input, sizeof cfg->pulse_input, "%s", dev);
            cfg->pulse_input[sizeof cfg->pulse_input - 1] = '\0';
        }
    } else {
        cfg->input_source = DSDCFG_INPUT_FILE;
        snprintf(cfg->file_path, sizeof cfg->file_path, "%.*s", (int)(sizeof cfg->file_path - 1), opts->audio_in_dev);
        cfg->file_path[sizeof cfg->file_path - 1] = '\0';
        cfg->file_sample_rate = opts->wav_sample_rate;
    }

    if (cfg->input_source == DSDCFG_INPUT_RTL || cfg->input_source == DSDCFG_INPUT_RTLTCP) {
        cfg->rtl_auto_ppm = opts->rtl_auto_ppm ? 1 : 0;
    }

    // Output snapshot: backend + UI
    cfg->has_output = 1;
    if (strncmp(opts->audio_out_dev, "pulse", 5) == 0) {
        cfg->output_backend = DSDCFG_OUTPUT_PULSE;
        const char* dev = NULL;
        if (opts->audio_out_dev[5] == ':') {
            dev = opts->audio_out_dev + 6;
        }
        if (dev && *dev) {
            snprintf(cfg->pulse_output, sizeof cfg->pulse_output, "%s", dev);
            cfg->pulse_output[sizeof cfg->pulse_output - 1] = '\0';
        }
    } else if (strcmp(opts->audio_out_dev, "null") == 0) {
        cfg->output_backend = DSDCFG_OUTPUT_NULL;
    } else {
        cfg->output_backend = DSDCFG_OUTPUT_UNSET;
    }
    cfg->ncurses_ui = opts->use_ncurses_terminal ? 1 : 0;

    // Mode snapshot: try to recognize common presets by flags.
    cfg->has_mode = 1;
    if (opts->analog_only && opts->monitor_input_audio) {
        cfg->decode_mode = DSDCFG_MODE_ANALOG;
    } else if (opts->frame_p25p1 && opts->frame_p25p2 && opts->frame_dmr && !opts->frame_dstar && !opts->frame_ysf
               && !opts->frame_nxdn48 && !opts->frame_nxdn96 && !opts->frame_provoice && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_TDMA;
    } else if (opts->frame_dmr && !opts->frame_p25p1 && !opts->frame_p25p2 && !opts->frame_dstar && !opts->frame_ysf
               && !opts->frame_nxdn48 && !opts->frame_nxdn96 && !opts->frame_provoice && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_DMR;
    } else if (opts->frame_p25p1 && !opts->frame_p25p2 && !opts->frame_dmr && !opts->frame_dstar && !opts->frame_ysf
               && !opts->frame_nxdn48 && !opts->frame_nxdn96 && !opts->frame_provoice && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_P25P1;
    } else if (opts->frame_p25p2 && !opts->frame_p25p1 && !opts->frame_dmr && !opts->frame_dstar && !opts->frame_ysf
               && !opts->frame_nxdn48 && !opts->frame_nxdn96 && !opts->frame_provoice && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_P25P2;
    } else if (opts->frame_nxdn48 && !opts->frame_nxdn96 && !opts->frame_p25p1 && !opts->frame_p25p2 && !opts->frame_dmr
               && !opts->frame_dstar && !opts->frame_ysf && !opts->frame_provoice && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_NXDN48;
    } else if (!opts->frame_nxdn48 && opts->frame_nxdn96 && !opts->frame_p25p1 && !opts->frame_p25p2 && !opts->frame_dmr
               && !opts->frame_dstar && !opts->frame_ysf && !opts->frame_provoice && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_NXDN96;
    } else if (opts->frame_x2tdma && !opts->frame_p25p1 && !opts->frame_p25p2 && !opts->frame_dmr && !opts->frame_dstar
               && !opts->frame_ysf && !opts->frame_nxdn48 && !opts->frame_nxdn96 && !opts->frame_provoice
               && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_X2TDMA;
    } else if (opts->frame_ysf && !opts->frame_dstar && !opts->frame_p25p1 && !opts->frame_p25p2 && !opts->frame_dmr
               && !opts->frame_nxdn48 && !opts->frame_nxdn96 && !opts->frame_provoice && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_YSF;
    } else if (opts->frame_dstar && !opts->frame_ysf && !opts->frame_p25p1 && !opts->frame_p25p2 && !opts->frame_dmr
               && !opts->frame_nxdn48 && !opts->frame_nxdn96 && !opts->frame_provoice && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_DSTAR;
    } else if (opts->frame_provoice && !opts->frame_dstar && !opts->frame_ysf && !opts->frame_p25p1
               && !opts->frame_p25p2 && !opts->frame_dmr && !opts->frame_nxdn48 && !opts->frame_nxdn96
               && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_EDACS_PV;
    } else if (opts->frame_dpmr && !opts->frame_dstar && !opts->frame_ysf && !opts->frame_p25p1 && !opts->frame_p25p2
               && !opts->frame_dmr && !opts->frame_nxdn48 && !opts->frame_nxdn96 && !opts->frame_provoice
               && !opts->frame_m17) {
        cfg->decode_mode = DSDCFG_MODE_DPMR;
    } else if (opts->frame_m17 && !opts->frame_dstar && !opts->frame_ysf && !opts->frame_p25p1 && !opts->frame_p25p2
               && !opts->frame_dmr && !opts->frame_nxdn48 && !opts->frame_nxdn96 && !opts->frame_provoice) {
        cfg->decode_mode = DSDCFG_MODE_M17;
    } else {
        cfg->decode_mode = DSDCFG_MODE_AUTO;
    }

    // Demod path snapshot (capture explicit CLI/UI locks only)
    if (opts->mod_cli_lock) {
        cfg->has_demod = 1;
        if (opts->mod_gfsk) {
            cfg->demod_path = DSDCFG_DEMOD_GFSK;
        } else if (opts->mod_qpsk) {
            cfg->demod_path = DSDCFG_DEMOD_QPSK;
        } else if (opts->mod_c4fm) {
            cfg->demod_path = DSDCFG_DEMOD_C4FM;
        } else {
            cfg->demod_path = DSDCFG_DEMOD_AUTO;
        }
    }

    // Trunking snapshot
    cfg->has_trunking = 1;
    cfg->trunk_enabled = (opts->p25_trunk || opts->trunk_enable) ? 1 : 0;
    snprintf(cfg->trunk_chan_csv, sizeof cfg->trunk_chan_csv, "%s", opts->chan_in_file);
    cfg->trunk_chan_csv[sizeof cfg->trunk_chan_csv - 1] = '\0';
    snprintf(cfg->trunk_group_csv, sizeof cfg->trunk_group_csv, "%s", opts->group_in_file);
    cfg->trunk_group_csv[sizeof cfg->trunk_group_csv - 1] = '\0';
    cfg->trunk_use_allow_list = opts->trunk_use_allow_list ? 1 : 0;
    cfg->trunk_tune_group_calls = opts->trunk_tune_group_calls ? 1 : 0;
    cfg->trunk_tune_private_calls = opts->trunk_tune_private_calls ? 1 : 0;
    cfg->trunk_tune_data_calls = opts->trunk_tune_data_calls ? 1 : 0;
    cfg->trunk_tune_enc_calls = opts->trunk_tune_enc_calls ? 1 : 0;

    // Logging snapshot
    cfg->has_logging = 1;
    snprintf(cfg->event_log, sizeof cfg->event_log, "%s", opts->event_out_file);
    cfg->event_log[sizeof cfg->event_log - 1] = '\0';

    // Recording snapshot
    cfg->has_recording = 1;
    cfg->per_call_wav = opts->dmr_stereo_wav ? 1 : 0;
    snprintf(cfg->per_call_wav_dir, sizeof cfg->per_call_wav_dir, "%s", opts->wav_out_dir);
    cfg->per_call_wav_dir[sizeof cfg->per_call_wav_dir - 1] = '\0';
    if (opts->static_wav_file && opts->wav_out_file[0] != '\0') {
        snprintf(cfg->static_wav_path, sizeof cfg->static_wav_path, "%s", opts->wav_out_file);
        cfg->static_wav_path[sizeof cfg->static_wav_path - 1] = '\0';
    } else {
        cfg->static_wav_path[0] = '\0';
    }
    snprintf(cfg->raw_wav_path, sizeof cfg->raw_wav_path, "%s", opts->wav_out_file_raw);
    cfg->raw_wav_path[sizeof cfg->raw_wav_path - 1] = '\0';

    // DSP snapshot (persist runtime toggles via env for the next run)
    cfg->has_dsp = 1;
    const char* iqb = getenv("DSD_NEO_IQ_BALANCE");
    cfg->iq_balance = (iqb && *iqb && atoi(iqb) != 0) ? 1 : 0;
    const char* dcb = getenv("DSD_NEO_IQ_DC_BLOCK");
    cfg->iq_dc_block = (dcb && *dcb && atoi(dcb) != 0) ? 1 : 0;
}

// Template generation ---------------------------------------------------------

void
dsd_user_config_render_template(FILE* stream) {
    if (!stream) {
        return;
    }

    fprintf(stream, "# DSD-neo configuration template\n");
    fprintf(stream, "# Generated by: dsd-neo --dump-config-template\n");
    fprintf(stream, "#\n");
    fprintf(stream, "# Uncomment and modify values as needed.\n");
    fprintf(stream, "# Lines starting with # are comments.\n");
    fprintf(stream, "#\n");
    fprintf(stream, "# Precedence: CLI arguments > environment variables > config file > defaults\n");
    fprintf(stream, "\n");
    fprintf(stream, "version = 1\n\n");

    const char* sections[16];
    int num_sections = dsdcfg_schema_sections(sections, 16);

    for (int s = 0; s < num_sections; s++) {
        const char* section = sections[s];
        fprintf(stream, "[%s]\n", section);

        int schema_count = dsdcfg_schema_count();
        for (int i = 0; i < schema_count; i++) {
            const dsdcfg_schema_entry_t* e = dsdcfg_schema_get(i);
            if (!e || dsd_strcasecmp(e->section, section) != 0) {
                continue;
            }

            /* Skip deprecated keys in template */
            if (e->deprecated) {
                continue;
            }

            /* Print description */
            fprintf(stream, "# %s\n", e->description);

            /* Print type info and constraints */
            switch (e->type) {
                case DSDCFG_TYPE_ENUM:
                    if (e->allowed) {
                        fprintf(stream, "# Allowed: %s\n", e->allowed);
                    }
                    break;
                case DSDCFG_TYPE_INT:
                    if (e->max_val > 0) {
                        fprintf(stream, "# Range: %d to %d\n", e->min_val, e->max_val);
                    } else if (e->min_val != 0) {
                        fprintf(stream, "# Minimum: %d\n", e->min_val);
                    }
                    break;
                case DSDCFG_TYPE_BOOL: fprintf(stream, "# Values: true, false\n"); break;
                case DSDCFG_TYPE_PATH: fprintf(stream, "# Path (supports ~ and $VAR expansion)\n"); break;
                case DSDCFG_TYPE_FREQ: fprintf(stream, "# Frequency (supports K/M/G suffix)\n"); break;
                default: break;
            }

            /* Print commented-out default value */
            if (e->default_str && e->default_str[0]) {
                if (e->type == DSDCFG_TYPE_STRING || e->type == DSDCFG_TYPE_ENUM || e->type == DSDCFG_TYPE_PATH
                    || e->type == DSDCFG_TYPE_FREQ) {
                    fprintf(stream, "# %s = \"%s\"\n", e->key, e->default_str);
                } else {
                    fprintf(stream, "# %s = %s\n", e->key, e->default_str);
                }
            } else {
                fprintf(stream, "# %s = \n", e->key);
            }
            fprintf(stream, "\n");
        }
    }

    /* Add profile section example */
    fprintf(stream, "# --- Profiles ---\n");
    fprintf(stream, "# Define named profiles to quickly switch between configurations.\n");
    fprintf(stream, "# Use: dsd-neo --config config.ini --profile <name>\n");
    fprintf(stream, "#\n");
    fprintf(stream, "# [profile.example]\n");
    fprintf(stream, "# mode.decode = \"p25p1\"\n");
    fprintf(stream, "# trunking.enabled = true\n");
    fprintf(stream, "# input.source = \"rtl\"\n");
    fprintf(stream, "# input.rtl_freq = \"851.375M\"\n");
}

// Validation ------------------------------------------------------------------

static int
validate_bool_value(const char* val) {
    if (!val || !*val) {
        return -1;
    }
    if (dsd_strcasecmp(val, "1") == 0 || dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0
        || dsd_strcasecmp(val, "on") == 0 || dsd_strcasecmp(val, "0") == 0 || dsd_strcasecmp(val, "false") == 0
        || dsd_strcasecmp(val, "no") == 0 || dsd_strcasecmp(val, "off") == 0) {
        return 0;
    }
    return -1;
}

static int
validate_int_value(const char* val, int* out_val) {
    if (!val || !*val) {
        return -1;
    }
    char* end = NULL;
    long x = strtol(val, &end, 10);
    if (end == val || *end != '\0') {
        return -1;
    }
    if (out_val) {
        *out_val = (int)x;
    }
    return 0;
}

static int
validate_enum_value(const char* val, const char* allowed) {
    if (!val || !allowed) {
        return -1;
    }

    /* Parse pipe-separated allowed values */
    char buf[256];
    snprintf(buf, sizeof buf, "%s", allowed);
    buf[sizeof buf - 1] = '\0';

    char* save = NULL;
    char* tok = dsd_strtok_r(buf, "|", &save);
    while (tok) {
        if (dsd_strcasecmp(val, tok) == 0) {
            return 0;
        }
        tok = dsd_strtok_r(NULL, "|", &save);
    }
    return -1;
}

int
dsd_user_config_validate(const char* path, dsdcfg_diagnostics_t* diags) {
    if (!diags) {
        return -1;
    }

    dsdcfg_diags_init(diags);

    if (!path || !*path) {
        dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, 0, "", "", "No config path provided");
        return -1;
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        char msg[256];
        snprintf(msg, sizeof msg, "Cannot open file: %s", strerror(errno));
        dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, 0, "", "", msg);
        return -1;
    }

    char line[1024];
    char current_section[64];
    current_section[0] = '\0';
    int line_num = 0;
    int is_profile_section = 0;

    while (fgets(line, sizeof line, fp)) {
        line_num++;

        /* Trim whitespace */
        char* p = line;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        size_t n = strlen(p);
        while (n > 0 && isspace((unsigned char)p[n - 1])) {
            p[--n] = '\0';
        }

        /* Skip empty lines and comments */
        if (p[0] == '\0' || p[0] == '#' || p[0] == ';') {
            continue;
        }

        /* Section header */
        if (p[0] == '[') {
            char* end = strchr(p, ']');
            if (!end) {
                dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, line_num, "", "", "Malformed section header");
                continue;
            }
            *end = '\0';
            snprintf(current_section, sizeof current_section, "%s", p + 1);

            /* Lowercase for comparison */
            for (char* c = current_section; *c; ++c) {
                *c = (char)tolower((unsigned char)*c);
            }

            /* Check if this is a profile section */
            is_profile_section = (strncmp(current_section, "profile.", 8) == 0);

            /* Validate known sections (skip profile sections) */
            if (!is_profile_section) {
                const char* known_sections[] = {"input",   "output",    "mode", "trunking",
                                                "logging", "recording", "dsp",  NULL};
                int found = 0;
                for (int i = 0; known_sections[i]; i++) {
                    if (strcmp(current_section, known_sections[i]) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    char msg[128];
                    snprintf(msg, sizeof msg, "Unknown section [%s]", current_section);
                    dsdcfg_diags_add(diags, DSDCFG_DIAG_WARNING, line_num, current_section, "", msg);
                }
            }
            continue;
        }

        /* Key=value parsing */
        char* eq = strchr(p, '=');
        if (!eq) {
            dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, line_num, current_section, "",
                             "Line is not a comment, section, or key=value");
            continue;
        }

        *eq = '\0';
        char* key = p;
        char* val = eq + 1;

        /* Trim key and value */
        while (*key && isspace((unsigned char)*key)) {
            key++;
        }
        n = strlen(key);
        while (n > 0 && isspace((unsigned char)key[n - 1])) {
            key[--n] = '\0';
        }
        while (*val && isspace((unsigned char)*val)) {
            val++;
        }
        n = strlen(val);
        while (n > 0 && isspace((unsigned char)val[n - 1])) {
            val[--n] = '\0';
        }

        /* Remove quotes from value */
        size_t val_len = strlen(val);
        if (val_len >= 2 && val[0] == '"' && val[val_len - 1] == '"') {
            memmove(val, val + 1, val_len - 2);
            val[val_len - 2] = '\0';
        }

        /* Top-level keys */
        if (current_section[0] == '\0') {
            if (dsd_strcasecmp(key, "version") == 0) {
                int ver = 0;
                if (validate_int_value(val, &ver) != 0) {
                    dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, line_num, "", key, "version must be an integer");
                }
            } else if (dsd_strcasecmp(key, "include") == 0) {
                /* Include directive - just validate path is not empty */
                if (!val[0]) {
                    dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, line_num, "", key, "include path is empty");
                }
            } else {
                std::string msg = std::string("Unknown top-level key '") + key + "'";
                dsdcfg_diags_add(diags, DSDCFG_DIAG_WARNING, line_num, "", key, msg.c_str());
            }
            continue;
        }

        /* Profile sections use dotted key syntax */
        if (is_profile_section) {
            /* Profile keys are section.key format - validate the target exists */
            char* dot = strchr(key, '.');
            if (!dot) {
                std::string msg = std::string("Profile key '") + key + "' should use section.key format";
                dsdcfg_diags_add(diags, DSDCFG_DIAG_WARNING, line_num, current_section, key, msg.c_str());
            } else {
                /* Extract target section.key and validate key exists */
                *dot = '\0';
                const char* target_sec = key;
                const char* target_key = dot + 1;
                const dsdcfg_schema_entry_t* e = dsdcfg_schema_find(target_sec, target_key);
                if (!e) {
                    std::string msg = std::string("Unknown key '") + target_sec + "." + target_key + "' in profile";
                    dsdcfg_diags_add(diags, DSDCFG_DIAG_WARNING, line_num, current_section, key, msg.c_str());
                } else {
                    /* Validate value type/range/enum same as normal sections */
                    switch (e->type) {
                        case DSDCFG_TYPE_BOOL:
                            if (validate_bool_value(val) != 0) {
                                char msg[128];
                                snprintf(msg, sizeof msg, "Invalid boolean value '%s' (use true/false/yes/no/1/0)",
                                         val);
                                dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, line_num, current_section, key, msg);
                            }
                            break;
                        case DSDCFG_TYPE_INT: {
                            int int_val = 0;
                            if (validate_int_value(val, &int_val) != 0) {
                                char msg[128];
                                snprintf(msg, sizeof msg, "Invalid integer value '%s'", val);
                                dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, line_num, current_section, key, msg);
                            } else if ((e->min_val != 0 || e->max_val != 0)
                                       && (int_val < e->min_val || int_val > e->max_val)) {
                                char msg[128];
                                snprintf(msg, sizeof msg, "Value %d is out of range [%d, %d]", int_val, e->min_val,
                                         e->max_val);
                                dsdcfg_diags_add(diags, DSDCFG_DIAG_WARNING, line_num, current_section, key, msg);
                            }
                            break;
                        }
                        case DSDCFG_TYPE_ENUM:
                            if (e->allowed && validate_enum_value(val, e->allowed) != 0) {
                                char msg[256];
                                snprintf(msg, sizeof msg, "Invalid value '%s' (allowed: %s)", val, e->allowed);
                                dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, line_num, current_section, key, msg);
                            }
                            break;
                        default: /* STRING, PATH, FREQ - accept any value */ break;
                    }
                }
                *dot = '.'; /* restore for any later use */
            }
            continue;
        }

        /* Lowercase key for lookup */
        std::string key_lc = key;
        for (char& c : key_lc) {
            c = (char)tolower((unsigned char)c);
        }

        /* Look up in schema */
        const dsdcfg_schema_entry_t* entry = dsdcfg_schema_find(current_section, key_lc.c_str());
        if (!entry) {
            std::string msg = std::string("Unknown key '") + key + "' in section [" + current_section + "]";
            dsdcfg_diags_add(diags, DSDCFG_DIAG_WARNING, line_num, current_section, key, msg.c_str());
            continue;
        }

        /* Check for deprecated */
        if (entry->deprecated) {
            std::string msg = std::string("Key '") + key + "' is deprecated";
            dsdcfg_diags_add(diags, DSDCFG_DIAG_INFO, line_num, current_section, key, msg.c_str());
        }

        /* Validate value type */
        switch (entry->type) {
            case DSDCFG_TYPE_BOOL:
                if (validate_bool_value(val) != 0) {
                    char msg[128];
                    snprintf(msg, sizeof msg, "Invalid boolean value '%s' (use true/false/yes/no/1/0)", val);
                    dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, line_num, current_section, key, msg);
                }
                break;
            case DSDCFG_TYPE_INT: {
                int int_val = 0;
                if (validate_int_value(val, &int_val) != 0) {
                    char msg[128];
                    snprintf(msg, sizeof msg, "Invalid integer value '%s'", val);
                    dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, line_num, current_section, key, msg);
                } else if ((entry->min_val != 0 || entry->max_val != 0)
                           && (int_val < entry->min_val || int_val > entry->max_val)) {
                    char msg[128];
                    snprintf(msg, sizeof msg, "Value %d is out of range [%d, %d]", int_val, entry->min_val,
                             entry->max_val);
                    dsdcfg_diags_add(diags, DSDCFG_DIAG_WARNING, line_num, current_section, key, msg);
                }
                break;
            }
            case DSDCFG_TYPE_ENUM:
                if (entry->allowed && validate_enum_value(val, entry->allowed) != 0) {
                    char msg[256];
                    snprintf(msg, sizeof msg, "Invalid value '%s' (allowed: %s)", val, entry->allowed);
                    dsdcfg_diags_add(diags, DSDCFG_DIAG_ERROR, line_num, current_section, key, msg);
                }
                break;
            default: /* STRING, PATH, FREQ - accept any value */ break;
        }
    }

    fclose(fp);
    return diags->error_count > 0 ? -1 : 0;
}

void
dsd_user_config_diags_free(dsdcfg_diagnostics_t* diags) {
    dsdcfg_diags_free(diags);
}

// Profile support -------------------------------------------------------------

/* Helper to apply a single dotted key (e.g., "input.source") to config */
static void
apply_profile_key(dsdneoUserConfig* cfg, const char* dotted_key, const char* val) {
    if (!cfg || !dotted_key || !val) {
        return;
    }

    /* Profile loader reads lines into a 1024-byte buffer; keep this scratch buffer at least that large to avoid
     * truncating dotted keys during parsing (and to silence -Wformat-truncation under _FORTIFY_SOURCE builds). */
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", dotted_key);
    buf[sizeof buf - 1] = '\0';

    char* dot = strchr(buf, '.');
    if (!dot) {
        return;
    }
    *dot = '\0';
    const char* section = buf;
    const char* key = dot + 1;

    /* Lowercase for comparison */
    for (char* c = buf; *c; ++c) {
        *c = (char)tolower((unsigned char)*c);
    }
    for (char* c = (char*)key; *c; ++c) {
        *c = (char)tolower((unsigned char)*c);
    }

    /* Apply based on section.key */
    if (strcmp(section, "input") == 0) {
        cfg->has_input = 1;
        if (strcmp(key, "source") == 0) {
            if (dsd_strcasecmp(val, "pulse") == 0) {
                cfg->input_source = DSDCFG_INPUT_PULSE;
            } else if (dsd_strcasecmp(val, "rtl") == 0) {
                cfg->input_source = DSDCFG_INPUT_RTL;
            } else if (dsd_strcasecmp(val, "rtltcp") == 0) {
                cfg->input_source = DSDCFG_INPUT_RTLTCP;
            } else if (dsd_strcasecmp(val, "file") == 0) {
                cfg->input_source = DSDCFG_INPUT_FILE;
            } else if (dsd_strcasecmp(val, "tcp") == 0) {
                cfg->input_source = DSDCFG_INPUT_TCP;
            } else if (dsd_strcasecmp(val, "udp") == 0) {
                cfg->input_source = DSDCFG_INPUT_UDP;
            }
        } else if (strcmp(key, "pulse_source") == 0 || strcmp(key, "pulse_input") == 0) {
            snprintf(cfg->pulse_input, sizeof cfg->pulse_input, "%s", val);
        } else if (strcmp(key, "rtl_device") == 0) {
            cfg->rtl_device = atoi(val);
        } else if (strcmp(key, "rtl_freq") == 0) {
            snprintf(cfg->rtl_freq, sizeof cfg->rtl_freq, "%s", val);
        } else if (strcmp(key, "rtl_gain") == 0) {
            cfg->rtl_gain = atoi(val);
        } else if (strcmp(key, "rtl_ppm") == 0) {
            cfg->rtl_ppm = atoi(val);
        } else if (strcmp(key, "rtl_bw_khz") == 0) {
            cfg->rtl_bw_khz = atoi(val);
        } else if (strcmp(key, "rtl_sql") == 0) {
            cfg->rtl_sql = atoi(val);
        } else if (strcmp(key, "rtl_volume") == 0) {
            cfg->rtl_volume = atoi(val);
        } else if (strcmp(key, "auto_ppm") == 0 || strcmp(key, "rtl_auto_ppm") == 0) {
            cfg->rtl_auto_ppm =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcmp(key, "rtltcp_host") == 0) {
            snprintf(cfg->rtltcp_host, sizeof cfg->rtltcp_host, "%s", val);
        } else if (strcmp(key, "rtltcp_port") == 0) {
            cfg->rtltcp_port = atoi(val);
        } else if (strcmp(key, "file_path") == 0) {
            copy_path_expanded(cfg->file_path, sizeof cfg->file_path, val);
        } else if (strcmp(key, "file_sample_rate") == 0) {
            cfg->file_sample_rate = atoi(val);
        } else if (strcmp(key, "tcp_host") == 0) {
            snprintf(cfg->tcp_host, sizeof cfg->tcp_host, "%s", val);
        } else if (strcmp(key, "tcp_port") == 0) {
            cfg->tcp_port = atoi(val);
        } else if (strcmp(key, "udp_addr") == 0) {
            snprintf(cfg->udp_addr, sizeof cfg->udp_addr, "%s", val);
        } else if (strcmp(key, "udp_port") == 0) {
            cfg->udp_port = atoi(val);
        }
    } else if (strcmp(section, "output") == 0) {
        cfg->has_output = 1;
        if (strcmp(key, "backend") == 0) {
            if (dsd_strcasecmp(val, "pulse") == 0) {
                cfg->output_backend = DSDCFG_OUTPUT_PULSE;
            } else if (dsd_strcasecmp(val, "null") == 0) {
                cfg->output_backend = DSDCFG_OUTPUT_NULL;
            }
        } else if (strcmp(key, "pulse_sink") == 0 || strcmp(key, "pulse_output") == 0) {
            snprintf(cfg->pulse_output, sizeof cfg->pulse_output, "%s", val);
        } else if (strcmp(key, "ncurses_ui") == 0) {
            cfg->ncurses_ui =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        }
    } else if (strcmp(section, "mode") == 0) {
        cfg->has_mode = 1;
        if (strcmp(key, "decode") == 0) {
            if (dsd_strcasecmp(val, "auto") == 0) {
                cfg->decode_mode = DSDCFG_MODE_AUTO;
            } else if (dsd_strcasecmp(val, "p25p1") == 0) {
                cfg->decode_mode = DSDCFG_MODE_P25P1;
            } else if (dsd_strcasecmp(val, "p25p2") == 0) {
                cfg->decode_mode = DSDCFG_MODE_P25P2;
            } else if (dsd_strcasecmp(val, "dmr") == 0) {
                cfg->decode_mode = DSDCFG_MODE_DMR;
            } else if (dsd_strcasecmp(val, "nxdn48") == 0) {
                cfg->decode_mode = DSDCFG_MODE_NXDN48;
            } else if (dsd_strcasecmp(val, "nxdn96") == 0) {
                cfg->decode_mode = DSDCFG_MODE_NXDN96;
            } else if (dsd_strcasecmp(val, "x2tdma") == 0) {
                cfg->decode_mode = DSDCFG_MODE_X2TDMA;
            } else if (dsd_strcasecmp(val, "ysf") == 0) {
                cfg->decode_mode = DSDCFG_MODE_YSF;
            } else if (dsd_strcasecmp(val, "dstar") == 0) {
                cfg->decode_mode = DSDCFG_MODE_DSTAR;
            } else if (dsd_strcasecmp(val, "edacs_pv") == 0 || dsd_strcasecmp(val, "edacs") == 0
                       || dsd_strcasecmp(val, "provoice") == 0) {
                cfg->decode_mode = DSDCFG_MODE_EDACS_PV;
            } else if (dsd_strcasecmp(val, "dpmr") == 0) {
                cfg->decode_mode = DSDCFG_MODE_DPMR;
            } else if (dsd_strcasecmp(val, "m17") == 0) {
                cfg->decode_mode = DSDCFG_MODE_M17;
            } else if (dsd_strcasecmp(val, "tdma") == 0) {
                cfg->decode_mode = DSDCFG_MODE_TDMA;
            } else if (dsd_strcasecmp(val, "analog") == 0) {
                cfg->decode_mode = DSDCFG_MODE_ANALOG;
            }
        } else if (strcmp(key, "demod") == 0) {
            cfg->has_demod = 1;
            if (dsd_strcasecmp(val, "auto") == 0) {
                cfg->demod_path = DSDCFG_DEMOD_AUTO;
            } else if (dsd_strcasecmp(val, "c4fm") == 0) {
                cfg->demod_path = DSDCFG_DEMOD_C4FM;
            } else if (dsd_strcasecmp(val, "gfsk") == 0) {
                cfg->demod_path = DSDCFG_DEMOD_GFSK;
            } else if (dsd_strcasecmp(val, "qpsk") == 0) {
                cfg->demod_path = DSDCFG_DEMOD_QPSK;
            }
        }
    } else if (strcmp(section, "trunking") == 0) {
        cfg->has_trunking = 1;
        if (strcmp(key, "enabled") == 0) {
            cfg->trunk_enabled =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcmp(key, "chan_csv") == 0) {
            copy_path_expanded(cfg->trunk_chan_csv, sizeof cfg->trunk_chan_csv, val);
        } else if (strcmp(key, "group_csv") == 0) {
            copy_path_expanded(cfg->trunk_group_csv, sizeof cfg->trunk_group_csv, val);
        } else if (strcmp(key, "allow_list") == 0) {
            cfg->trunk_use_allow_list =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcmp(key, "tune_group_calls") == 0) {
            cfg->trunk_tune_group_calls =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcmp(key, "tune_private_calls") == 0) {
            cfg->trunk_tune_private_calls =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcmp(key, "tune_data_calls") == 0) {
            cfg->trunk_tune_data_calls =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcmp(key, "tune_enc_calls") == 0) {
            cfg->trunk_tune_enc_calls =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        }
    } else if (strcmp(section, "logging") == 0) {
        cfg->has_logging = 1;
        if (strcmp(key, "event_log") == 0 || strcmp(key, "event_log_file") == 0) {
            copy_path_expanded(cfg->event_log, sizeof cfg->event_log, val);
        }
    } else if (strcmp(section, "recording") == 0) {
        cfg->has_recording = 1;
        if (strcmp(key, "per_call_wav") == 0) {
            cfg->per_call_wav =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcmp(key, "per_call_wav_dir") == 0) {
            copy_path_expanded(cfg->per_call_wav_dir, sizeof cfg->per_call_wav_dir, val);
        } else if (strcmp(key, "static_wav") == 0) {
            copy_path_expanded(cfg->static_wav_path, sizeof cfg->static_wav_path, val);
        } else if (strcmp(key, "raw_wav") == 0) {
            copy_path_expanded(cfg->raw_wav_path, sizeof cfg->raw_wav_path, val);
        }
    } else if (strcmp(section, "dsp") == 0) {
        cfg->has_dsp = 1;
        if (strcmp(key, "iq_balance") == 0) {
            cfg->iq_balance =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        } else if (strcmp(key, "iq_dc_block") == 0) {
            cfg->iq_dc_block =
                (dsd_strcasecmp(val, "true") == 0 || dsd_strcasecmp(val, "yes") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
        }
    }
}

/* Internal: load config with include and profile support */
static int load_config_internal(const char* path, const char* profile_name, dsdneoUserConfig* cfg, int depth,
                                const char** include_stack, int include_stack_size);

/* Process include directives */
static int
process_includes(const char* path, dsdneoUserConfig* cfg, int depth, const char** include_stack,
                 int include_stack_size) {
    if (depth >= 3) {
        return 0; /* max depth reached */
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    char line[1024];
    while (fgets(line, sizeof line, fp)) {
        /* Trim */
        char* p = line;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        size_t n = strlen(p);
        while (n > 0 && isspace((unsigned char)p[n - 1])) {
            p[--n] = '\0';
        }

        /* Stop at first section - includes must be before sections */
        if (p[0] == '[') {
            break;
        }

        /* Skip comments and empty */
        if (p[0] == '\0' || p[0] == '#' || p[0] == ';') {
            continue;
        }

        /* Look for include = "path" */
        if (dsd_strncasecmp(p, "include", 7) != 0) {
            continue;
        }
        p += 7;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p != '=') {
            continue;
        }
        p++;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        /* Extract path */
        char inc_path[1024];
        n = strlen(p);
        if (n >= 2 && p[0] == '"' && p[n - 1] == '"') {
            snprintf(inc_path, sizeof inc_path, "%.*s", (int)(n - 2), p + 1);
        } else {
            snprintf(inc_path, sizeof inc_path, "%s", p);
        }
        inc_path[sizeof inc_path - 1] = '\0';

        /* Expand path */
        char expanded[1024];
        if (dsd_config_expand_path(inc_path, expanded, sizeof expanded) == 0) {
            snprintf(inc_path, sizeof inc_path, "%s", expanded);
        }

        /* Check for circular include */
        int circular = 0;
        for (int i = 0; i < include_stack_size; i++) {
            if (include_stack[i] && strcmp(include_stack[i], inc_path) == 0) {
                circular = 1;
                break;
            }
        }
        if (circular) {
            continue; /* skip circular include */
        }

        /* First process any nested includes in the included file */
        const char* nested_stack[8];
        int nested_stack_size = 0;
        for (int i = 0; i < include_stack_size && i < 7; i++) {
            nested_stack[nested_stack_size++] = include_stack[i];
        }
        nested_stack[nested_stack_size++] = inc_path;
        process_includes(inc_path, cfg, depth + 1, nested_stack, nested_stack_size);

        /* Then load the included file's config values */
        user_config_load_no_reset(inc_path, cfg);
    }

    fclose(fp);
    return 0;
}

static int
load_config_internal(const char* path, const char* profile_name, dsdneoUserConfig* cfg, int depth,
                     const char** include_stack, int include_stack_size) {
    (void)depth;
    (void)include_stack;
    (void)include_stack_size;

    if (!path || !cfg) {
        return -1;
    }

    /* Includes are already processed by dsd_user_config_load_profile() before
     * calling this function. This function only extracts profile overlay keys. */

    FILE* fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    char line[1024];
    char current_section[64];
    current_section[0] = '\0';
    char target_profile[64];
    target_profile[0] = '\0';
    if (profile_name) {
        snprintf(target_profile, sizeof target_profile, "profile.%s", profile_name);
        for (char* c = target_profile; *c; ++c) {
            *c = (char)tolower((unsigned char)*c);
        }
    }

    int in_target_profile = 0;
    int profile_found = 0;

    while (fgets(line, sizeof line, fp)) {
        /* Trim */
        char* p = line;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        size_t n = strlen(p);
        while (n > 0 && isspace((unsigned char)p[n - 1])) {
            p[--n] = '\0';
        }

        if (p[0] == '\0' || p[0] == '#' || p[0] == ';') {
            continue;
        }

        /* Section header */
        if (p[0] == '[') {
            char* end = strchr(p, ']');
            if (!end) {
                continue;
            }
            *end = '\0';
            snprintf(current_section, sizeof current_section, "%s", p + 1);
            for (char* c = current_section; *c; ++c) {
                *c = (char)tolower((unsigned char)*c);
            }

            /* Check if we entered the target profile section */
            in_target_profile = (target_profile[0] && strcmp(current_section, target_profile) == 0);
            if (in_target_profile) {
                profile_found = 1;
            }
            continue;
        }

        /* Key=value */
        char* eq = strchr(p, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char* key = p;
        char* val = eq + 1;

        /* Trim */
        while (*key && isspace((unsigned char)*key)) {
            key++;
        }
        n = strlen(key);
        while (n > 0 && isspace((unsigned char)key[n - 1])) {
            key[--n] = '\0';
        }
        while (*val && isspace((unsigned char)*val)) {
            val++;
        }
        n = strlen(val);
        while (n > 0 && isspace((unsigned char)val[n - 1])) {
            val[--n] = '\0';
        }

        /* Unquote */
        size_t val_len = strlen(val);
        if (val_len >= 2 && val[0] == '"' && val[val_len - 1] == '"') {
            memmove(val, val + 1, val_len - 2);
            val[val_len - 2] = '\0';
        }

        /* Handle profile section keys */
        if (in_target_profile) {
            apply_profile_key(cfg, key, val);
            continue;
        }

        /* Skip other profile sections */
        if (strncmp(current_section, "profile.", 8) == 0) {
            continue;
        }

        /* Skip include directives (already processed) */
        if (current_section[0] == '\0' && dsd_strcasecmp(key, "include") == 0) {
            continue;
        }

        /* Regular section keys are handled by dsd_user_config_load() which is called
         * before this function for profile loading. This function only handles
         * profile overlay keys (in_target_profile case above). */
    }

    fclose(fp);

    /* If a profile was requested but not found, return error */
    if (profile_name && *profile_name && !profile_found) {
        return -1;
    }

    return 0;
}

int
dsd_user_config_load_profile(const char* path, const char* profile_name, dsdneoUserConfig* cfg) {
    if (!cfg) {
        return -1;
    }

    /* Reset config once at start */
    user_cfg_reset(cfg);

    /* Process includes first (they provide base values that can be overridden) */
    const char* stack[1] = {path};
    process_includes(path, cfg, 0, stack, 1);

    /* Now load the main config (which overrides included values) */
    int rc = user_config_load_no_reset(path, cfg);
    if (rc != 0) {
        return rc;
    }

    /* If no profile requested, we're done */
    if (!profile_name || !*profile_name) {
        return 0;
    }

    /* Now overlay profile settings */
    return load_config_internal(path, profile_name, cfg, 0, stack, 1);
}

int
dsd_user_config_list_profiles(const char* path, const char** names, char* names_buf, size_t names_buf_size,
                              int max_names) {
    if (!path || !names || !names_buf || names_buf_size == 0 || max_names <= 0) {
        return -1;
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    int count = 0;
    size_t buf_used = 0;
    char line[1024];

    while (fgets(line, sizeof line, fp) && count < max_names) {
        /* Trim */
        char* p = line;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        size_t n = strlen(p);
        while (n > 0 && isspace((unsigned char)p[n - 1])) {
            p[--n] = '\0';
        }

        /* Look for [profile.NAME] */
        if (p[0] != '[') {
            continue;
        }
        char* end = strchr(p, ']');
        if (!end) {
            continue;
        }
        *end = '\0';
        const char* section = p + 1;

        if (dsd_strncasecmp(section, "profile.", 8) != 0) {
            continue;
        }

        const char* profile_name = section + 8;
        size_t name_len = strlen(profile_name);
        if (name_len == 0) {
            continue;
        }

        /* Copy to buffer */
        if (buf_used + name_len + 1 > names_buf_size) {
            break; /* buffer full */
        }

        names[count] = names_buf + buf_used;
        memcpy(names_buf + buf_used, profile_name, name_len + 1);
        buf_used += name_len + 1;
        count++;
    }

    fclose(fp);
    return count;
}
