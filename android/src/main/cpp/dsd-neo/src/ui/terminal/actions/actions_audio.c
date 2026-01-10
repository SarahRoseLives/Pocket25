// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* UI command actions — audio domain */

#include <string.h>
#include <time.h>

#include <dsd-neo/core/audio.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/ui/ui_cmd_dispatch.h>

#include <stdio.h>

static int
ui_handle_toggle_mute(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)c;
    opts->audio_out = (opts->audio_out == 0) ? 1 : 0;
    const char* msg = (opts->audio_out == 0) ? "Output: Muted" : "Output: On";
    if (opts->audio_out == 1) {
        if (opts->audio_out_type == 0) {
            closeAudioOutput(opts);
            if (openAudioOutput(opts) != 0) {
                opts->audio_out = 0;
                msg = "Output: open failed";
            }
        }
    }
    if (state) {
        snprintf(state->ui_msg, sizeof state->ui_msg, "%s", msg);
        state->ui_msg_expire = time(NULL) + 3;
    }
    return 1;
}

static inline void
apply_gain_delta(dsd_opts* opts, dsd_state* state, int d) {
    int g = opts->audio_gain + d;
    if (g < 0) {
        g = 0;
    }
    if (g > 50) {
        g = 50;
    }
    opts->audio_gain = g;
    state->aout_gain = opts->audio_gain;
    state->aout_gainR = opts->audio_gain;
    opts->audio_gainR = opts->audio_gain;
    if (opts->audio_gain == 0) {
        state->aout_gain = 25;
        state->aout_gainR = 25;
    }
}

static int
ui_handle_gain_delta(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    int delta = 0;
    if (c->n >= (int)sizeof(int32_t)) {
        int32_t d = 0;
        memcpy(&d, c->data, sizeof(int32_t));
        delta = d;
    }
    apply_gain_delta(opts, state, delta);
    return 1;
}

static int
ui_handle_again_delta(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    int32_t d = 0;
    if (c->n >= (int)sizeof(int32_t)) {
        memcpy(&d, c->data, sizeof(int32_t));
    }
    int g = opts->audio_gainA + d;
    if (g < 0) {
        g = 0;
    }
    if (g > 50) {
        g = 50;
    }
    opts->audio_gainA = g;
    return 1;
}

static int
ui_handle_gain_set(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    int32_t g = 0;
    if (c->n >= (int)sizeof(int32_t)) {
        memcpy(&g, c->data, sizeof g);
    }
    if (g < 0) {
        g = 0;
    }
    if (g > 50) {
        g = 50;
    }
    opts->audio_gain = g;
    state->aout_gain = opts->audio_gain;
    state->aout_gainR = opts->audio_gain;
    opts->audio_gainR = opts->audio_gain;
    if (opts->audio_gain == 0) {
        state->aout_gain = 25;
        state->aout_gainR = 25;
    }
    return 1;
}

static int
ui_handle_again_set(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    int32_t g = 0;
    if (c->n >= (int)sizeof(int32_t)) {
        memcpy(&g, c->data, sizeof g);
    }
    if (g < 0) {
        g = 0;
    }
    if (g > 50) {
        g = 50;
    }
    opts->audio_gainA = g;
    return 1;
}

static int
ui_handle_input_warn_db_set(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    double v = 0.0;
    if (c->n >= (int)sizeof(double)) {
        memcpy(&v, c->data, sizeof v);
    }
    if (v < -200.0) {
        v = -200.0;
    }
    if (v > 0.0) {
        v = 0.0;
    }
    opts->input_warn_db = v;
    return 1;
}

static int
ui_handle_input_monitor_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    (void)c;
    opts->monitor_input_audio = opts->monitor_input_audio ? 0 : 1;
    return 1;
}

static int
ui_handle_cosine_filter_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    (void)c;
    opts->use_cosine_filter = opts->use_cosine_filter ? 0 : 1;
    return 1;
}

static int
ui_handle_input_vol_cycle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)c;
    if (opts->audio_in_type == AUDIO_IN_RTL) {
        if (opts->rtl_volume_multiplier == 1 || opts->rtl_volume_multiplier == 2) {
            opts->rtl_volume_multiplier++;
        } else {
            opts->rtl_volume_multiplier = 1;
        }
        if (state) {
            snprintf(state->ui_msg, sizeof state->ui_msg, "RTL Volume: %dX", opts->rtl_volume_multiplier);
            state->ui_msg_expire = time(NULL) + 2;
        }
    } else {
        if (opts->input_volume_multiplier == 1 || opts->input_volume_multiplier == 2) {
            opts->input_volume_multiplier++;
        } else {
            opts->input_volume_multiplier = 1;
        }
        if (state) {
            snprintf(state->ui_msg, sizeof state->ui_msg, "Input Volume: %dX", opts->input_volume_multiplier);
            state->ui_msg_expire = time(NULL) + 2;
        }
    }
    return 1;
}

static int
ui_handle_input_vol_set(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    if (opts && c->n >= (int)sizeof(int32_t)) {
        int32_t v = 1;
        memcpy(&v, c->data, sizeof v);
        if (v < 1) {
            v = 1;
        }
        if (v > 16) {
            v = 16;
        }
        opts->input_volume_multiplier = v;
    }
    return 1;
}

// Public registry
const struct UiCmdReg ui_actions_audio[] = {
    {UI_CMD_TOGGLE_MUTE, ui_handle_toggle_mute},
    {UI_CMD_GAIN_DELTA, ui_handle_gain_delta},
    {UI_CMD_AGAIN_DELTA, ui_handle_again_delta},
    {UI_CMD_GAIN_SET, ui_handle_gain_set},
    {UI_CMD_AGAIN_SET, ui_handle_again_set},
    {UI_CMD_INPUT_WARN_DB_SET, ui_handle_input_warn_db_set},
    {UI_CMD_INPUT_MONITOR_TOGGLE, ui_handle_input_monitor_toggle},
    {UI_CMD_COSINE_FILTER_TOGGLE, ui_handle_cosine_filter_toggle},
    {UI_CMD_INPUT_VOL_CYCLE, ui_handle_input_vol_cycle},
    {UI_CMD_INPUT_VOL_SET, ui_handle_input_vol_set},
    {0, NULL},
};
