// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* UI command actions — trunking domain */

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/ui/ui_cmd_dispatch.h>

static int
ui_handle_trunk_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    (void)c;
    if (opts->p25_trunk == 1) {
        opts->p25_trunk = 0;
        opts->trunk_enable = 0;
    } else {
        opts->p25_trunk = 1;
        opts->trunk_enable = 1;
    }
    return 1;
}

static int
ui_handle_scanner_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)c;
    opts->scanner_mode = opts->scanner_mode ? 0 : 1;
    opts->p25_trunk = 0;
    opts->trunk_enable = 0;
    (void)state;
    return 1;
}

static int
ui_handle_trunk_group_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    (void)c;
    if (opts->p25_trunk == 1) {
        opts->trunk_tune_group_calls = opts->trunk_tune_group_calls ? 0 : 1;
    }
    return 1;
}

static int
ui_handle_tg_hold_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)opts;
    uint8_t slot = 0;
    if (c->n >= 1) {
        slot = c->data[0] & 1;
    }
    if (slot == 0) {
        if (state->tg_hold == 0) {
            state->tg_hold = state->lasttg;
        } else {
            state->tg_hold = 0;
        }
        if ((opts->frame_nxdn48 == 1 || opts->frame_nxdn96 == 1) && (state->tg_hold == 0)) {
            state->tg_hold = state->nxdn_last_tg;
        } else if (opts->frame_provoice == 1 && state->ea_mode == 0) {
            state->tg_hold = state->lastsrc;
        }
    } else {
        if (state->tg_hold == 0) {
            state->tg_hold = state->lasttgR;
        } else {
            state->tg_hold = 0;
        }
        if ((opts->frame_nxdn48 == 1 || opts->frame_nxdn96 == 1) && (state->tg_hold == 0)) {
            state->tg_hold = state->nxdn_last_tg;
        } else if (opts->frame_provoice == 1 && state->ea_mode == 0) {
            state->tg_hold = state->lastsrcR;
        }
    }
    return 1;
}

static int
ui_handle_trunk_wlist_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    (void)c;
    opts->trunk_use_allow_list = opts->trunk_use_allow_list ? 0 : 1;
    return 1;
}

static int
ui_handle_trunk_priv_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    (void)c;
    opts->trunk_tune_private_calls = opts->trunk_tune_private_calls ? 0 : 1;
    return 1;
}

static int
ui_handle_trunk_data_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    (void)c;
    opts->trunk_tune_data_calls = opts->trunk_tune_data_calls ? 0 : 1;
    return 1;
}

static int
ui_handle_trunk_enc_toggle(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    (void)c;
    opts->trunk_tune_enc_calls = opts->trunk_tune_enc_calls ? 0 : 1;
    return 1;
}

const struct UiCmdReg ui_actions_trunk[] = {
    {UI_CMD_TRUNK_TOGGLE, ui_handle_trunk_toggle},
    {UI_CMD_SCANNER_TOGGLE, ui_handle_scanner_toggle},
    {UI_CMD_TRUNK_GROUP_TOGGLE, ui_handle_trunk_group_toggle},
    {UI_CMD_TG_HOLD_TOGGLE, ui_handle_tg_hold_toggle},
    {UI_CMD_TRUNK_WLIST_TOGGLE, ui_handle_trunk_wlist_toggle},
    {UI_CMD_TRUNK_PRIV_TOGGLE, ui_handle_trunk_priv_toggle},
    {UI_CMD_TRUNK_DATA_TOGGLE, ui_handle_trunk_data_toggle},
    {UI_CMD_TRUNK_ENC_TOGGLE, ui_handle_trunk_enc_toggle},
    {0, NULL},
};
