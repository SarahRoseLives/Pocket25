// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Configuration schema data and accessor implementation.
 *
 * Defines all valid configuration keys, their types, descriptions,
 * and constraints for validation and template generation.
 */

#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/runtime/config_schema.h>

#include <stdlib.h>
#include <string.h>

/* Schema data for all configuration keys */
static const dsdcfg_schema_entry_t s_schema[] = {
    /* [input] section */
    {"input", "source", "Input source type", "pulse", "pulse|rtl|rtltcp|file|tcp|udp", DSDCFG_TYPE_ENUM, 0, 0, 0},
    {"input", "pulse_source", "PulseAudio source device name", "", NULL, DSDCFG_TYPE_STRING, 0, 0, 0},
    {"input", "pulse_input", "PulseAudio input device (alias for pulse_source)", "", NULL, DSDCFG_TYPE_STRING, 0, 0,
     1}, /* deprecated alias */
    {"input", "rtl_device", "RTL-SDR device index (0-based)", "0", NULL, DSDCFG_TYPE_INT, 0, 255, 0},
    {"input", "rtl_freq", "RTL-SDR frequency (supports K/M/G suffix)", "851.375M", NULL, DSDCFG_TYPE_FREQ, 0, 0, 0},
    {"input", "rtl_gain", "RTL-SDR gain in dB (0 for AGC)", "0", NULL, DSDCFG_TYPE_INT, 0, 49, 0},
    {"input", "rtl_ppm", "RTL-SDR frequency correction in PPM", "0", NULL, DSDCFG_TYPE_INT, -1000, 1000, 0},
    {"input", "rtl_bw_khz", "RTL-SDR DSP bandwidth in kHz", "48", NULL, DSDCFG_TYPE_INT, 4, 48, 0},
    {"input", "rtl_sql", "RTL-SDR squelch level in dB (0 to disable)", "0", NULL, DSDCFG_TYPE_INT, -100, 0, 0},
    {"input", "rtl_volume", "RTL-SDR volume multiplier", "2", NULL, DSDCFG_TYPE_INT, 1, 3, 0},
    {"input", "auto_ppm", "Enable spectrum-based RTL auto-PPM correction", "false", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},
    {"input", "rtl_auto_ppm", "Enable spectrum-based RTL auto-PPM correction (alias for auto_ppm)", "false", NULL,
     DSDCFG_TYPE_BOOL, 0, 0, 1}, /* deprecated alias */
    {"input", "rtltcp_host", "RTL-TCP server hostname or IP", "127.0.0.1", NULL, DSDCFG_TYPE_STRING, 0, 0, 0},
    {"input", "rtltcp_port", "RTL-TCP server port", "1234", NULL, DSDCFG_TYPE_INT, 1, 65535, 0},
    {"input", "file_path", "Input audio file path", "", NULL, DSDCFG_TYPE_PATH, 0, 0, 0},
    {"input", "file_sample_rate", "Input file sample rate in Hz", "48000", NULL, DSDCFG_TYPE_INT, 8000, 192000, 0},
    {"input", "tcp_host", "TCP direct input hostname", "127.0.0.1", NULL, DSDCFG_TYPE_STRING, 0, 0, 0},
    {"input", "tcp_port", "TCP direct input port", "7355", NULL, DSDCFG_TYPE_INT, 1, 65535, 0},
    {"input", "udp_addr", "UDP input bind address", "127.0.0.1", NULL, DSDCFG_TYPE_STRING, 0, 0, 0},
    {"input", "udp_port", "UDP input port", "7355", NULL, DSDCFG_TYPE_INT, 1, 65535, 0},

    /* [output] section */
    {"output", "backend", "Audio output backend", "pulse", "pulse|null", DSDCFG_TYPE_ENUM, 0, 0, 0},
    {"output", "pulse_sink", "PulseAudio sink device name", "", NULL, DSDCFG_TYPE_STRING, 0, 0, 0},
    {"output", "pulse_output", "PulseAudio output device (alias for pulse_sink)", "", NULL, DSDCFG_TYPE_STRING, 0, 0,
     1}, /* deprecated alias */
    {"output", "ncurses_ui", "Enable ncurses terminal UI", "false", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},

    /* [mode] section */
    {"mode", "decode", "Decode mode preset", "auto",
     "auto|p25p1|p25p2|dmr|nxdn48|nxdn96|x2tdma|ysf|dstar|edacs_pv|dpmr|m17|tdma|analog", DSDCFG_TYPE_ENUM, 0, 0, 0},
    {"mode", "demod", "Demodulator path (auto/C4FM/GFSK/QPSK)", "auto", "auto|c4fm|gfsk|qpsk", DSDCFG_TYPE_ENUM, 0, 0,
     0},

    /* [trunking] section */
    {"trunking", "enabled", "Enable trunking support", "false", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},
    {"trunking", "chan_csv", "Channel map CSV file path", "", NULL, DSDCFG_TYPE_PATH, 0, 0, 0},
    {"trunking", "group_csv", "Group list CSV file path", "", NULL, DSDCFG_TYPE_PATH, 0, 0, 0},
    {"trunking", "allow_list", "Use group.csv as allow list (vs block list)", "false", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},
    {"trunking", "tune_group_calls", "Tune to group voice calls", "true", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},
    {"trunking", "tune_private_calls", "Tune to private/individual calls", "true", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},
    {"trunking", "tune_data_calls", "Tune to data channel grants", "false", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},
    {"trunking", "tune_enc_calls", "Tune to encrypted calls", "true", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},

    /* [logging] section */
    {"logging", "event_log", "Event history log file path", "", NULL, DSDCFG_TYPE_PATH, 0, 0, 0},

    /* [recording] section */
    {"recording", "per_call_wav", "Enable per-call WAV output", "false", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},
    {"recording", "per_call_wav_dir", "Per-call WAV output directory", "./WAV", NULL, DSDCFG_TYPE_PATH, 0, 0, 0},
    {"recording", "static_wav", "Static decoded voice WAV output file", "", NULL, DSDCFG_TYPE_PATH, 0, 0, 0},
    {"recording", "raw_wav", "Raw (48 kHz) audio WAV output file", "", NULL, DSDCFG_TYPE_PATH, 0, 0, 0},

    /* [dsp] section */
    {"dsp", "iq_balance", "Enable RTL IQ balance (image suppression)", "false", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},
    {"dsp", "iq_dc_block", "Enable RTL I/Q DC blocker", "false", NULL, DSDCFG_TYPE_BOOL, 0, 0, 0},
};

static const int s_schema_count = sizeof(s_schema) / sizeof(s_schema[0]);

int
dsdcfg_schema_count(void) {
    return s_schema_count;
}

const dsdcfg_schema_entry_t*
dsdcfg_schema_get(int index) {
    if (index < 0 || index >= s_schema_count) {
        return NULL;
    }
    return &s_schema[index];
}

const dsdcfg_schema_entry_t*
dsdcfg_schema_find(const char* section, const char* key) {
    if (!section || !key) {
        return NULL;
    }
    for (int i = 0; i < s_schema_count; i++) {
        if (dsd_strcasecmp(s_schema[i].section, section) == 0 && dsd_strcasecmp(s_schema[i].key, key) == 0) {
            return &s_schema[i];
        }
    }
    return NULL;
}

const char*
dsd_config_key_description(const char* section, const char* key) {
    const dsdcfg_schema_entry_t* e = dsdcfg_schema_find(section, key);
    return e ? e->description : NULL;
}

int
dsd_config_key_is_deprecated(const char* section, const char* key) {
    const dsdcfg_schema_entry_t* e = dsdcfg_schema_find(section, key);
    return e ? e->deprecated : 0;
}

const char*
dsdcfg_type_name(dsdcfg_type_t type) {
    switch (type) {
        case DSDCFG_TYPE_STRING: return "string";
        case DSDCFG_TYPE_INT: return "int";
        case DSDCFG_TYPE_BOOL: return "bool";
        case DSDCFG_TYPE_ENUM: return "enum";
        case DSDCFG_TYPE_PATH: return "path";
        case DSDCFG_TYPE_FREQ: return "freq";
        default: return "unknown";
    }
}

void
dsdcfg_diags_init(dsdcfg_diagnostics_t* diags) {
    if (!diags) {
        return;
    }
    diags->items = NULL;
    diags->count = 0;
    diags->capacity = 0;
    diags->error_count = 0;
    diags->warning_count = 0;
}

void
dsdcfg_diags_add(dsdcfg_diagnostics_t* diags, dsdcfg_diag_level_t level, int line, const char* section, const char* key,
                 const char* message) {
    if (!diags) {
        return;
    }

    /* Grow array if needed */
    if (diags->count >= diags->capacity) {
        int new_cap = diags->capacity == 0 ? 8 : diags->capacity * 2;
        dsdcfg_diagnostic_t* new_items =
            (dsdcfg_diagnostic_t*)realloc(diags->items, (size_t)new_cap * sizeof(*new_items));
        if (!new_items) {
            return; /* allocation failed, drop diagnostic */
        }
        diags->items = new_items;
        diags->capacity = new_cap;
    }

    dsdcfg_diagnostic_t* d = &diags->items[diags->count++];
    d->level = level;
    d->line_number = line;

    d->section[0] = '\0';
    if (section) {
        snprintf(d->section, sizeof(d->section), "%s", section);
        d->section[sizeof(d->section) - 1] = '\0';
    }

    d->key[0] = '\0';
    if (key) {
        snprintf(d->key, sizeof(d->key), "%s", key);
        d->key[sizeof(d->key) - 1] = '\0';
    }

    d->message[0] = '\0';
    if (message) {
        snprintf(d->message, sizeof(d->message), "%s", message);
        d->message[sizeof(d->message) - 1] = '\0';
    }

    if (level == DSDCFG_DIAG_ERROR) {
        diags->error_count++;
    } else if (level == DSDCFG_DIAG_WARNING) {
        diags->warning_count++;
    }
}

void
dsdcfg_diags_free(dsdcfg_diagnostics_t* diags) {
    if (!diags) {
        return;
    }
    free(diags->items);
    diags->items = NULL;
    diags->count = 0;
    diags->capacity = 0;
    diags->error_count = 0;
    diags->warning_count = 0;
}

void
dsdcfg_diags_print(const dsdcfg_diagnostics_t* diags, FILE* stream, const char* path) {
    if (!diags || !stream) {
        return;
    }

    for (int i = 0; i < diags->count; i++) {
        const dsdcfg_diagnostic_t* d = &diags->items[i];
        const char* level_str = "info";
        if (d->level == DSDCFG_DIAG_WARNING) {
            level_str = "warning";
        } else if (d->level == DSDCFG_DIAG_ERROR) {
            level_str = "error";
        }

        if (path && d->line_number > 0) {
            fprintf(stream, "%s:%d: %s", path, d->line_number, level_str);
        } else if (d->line_number > 0) {
            fprintf(stream, "line %d: %s", d->line_number, level_str);
        } else {
            fprintf(stream, "%s", level_str);
        }

        if (d->section[0] && d->key[0]) {
            fprintf(stream, " [%s] %s: ", d->section, d->key);
        } else if (d->section[0]) {
            fprintf(stream, " [%s]: ", d->section);
        } else {
            fprintf(stream, ": ");
        }

        fprintf(stream, "%s\n", d->message);
    }

    if (diags->error_count > 0 || diags->warning_count > 0) {
        fprintf(stream, "\nSummary: %d error(s), %d warning(s)\n", diags->error_count, diags->warning_count);
    }
}

int
dsdcfg_schema_sections(const char** sections, int max_sections) {
    if (!sections || max_sections <= 0) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < s_schema_count && count < max_sections; i++) {
        const char* sec = s_schema[i].section;

        /* Check if section already in output list */
        int found = 0;
        for (int j = 0; j < count; j++) {
            if (dsd_strcasecmp(sections[j], sec) == 0) {
                found = 1;
                break;
            }
        }

        if (!found) {
            sections[count++] = sec;
        }
    }

    return count;
}
