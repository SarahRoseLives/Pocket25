// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/* UI command actions — logging/history domain */

#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/ui/menu_services.h>
#include <dsd-neo/ui/ui_cmd_dispatch.h>

static int
ui_handle_eh_next(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)opts;
    (void)c;
    state->eh_index++;
    return 1;
}

static int
ui_handle_eh_prev(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)opts;
    (void)c;
    state->eh_index--;
    return 1;
}

static int
ui_handle_eh_toggle_slot(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)opts;
    (void)c;
    if (state->eh_slot == 0) {
        state->eh_slot = 1;
    } else if (state->eh_slot == 1) {
        state->eh_slot = 2;
    } else {
        state->eh_slot = 0;
    }
    state->eh_index = 0;
    return 1;
}

static int
ui_handle_ui_msg_clear(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)opts;
    (void)c;
    if (state) {
        state->ui_msg[0] = '\0';
        state->ui_msg_expire = 0;
    }
    return 1;
}

static int
ui_handle_eh_reset(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)c;
    if (state) {
        svc_reset_event_history(state);
    }
    (void)opts;
    return 1;
}

static int
ui_handle_event_log_disable(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    (void)c;
    if (opts) {
        svc_disable_event_log(opts);
    }
    return 1;
}

static int
ui_handle_event_log_set(dsd_opts* opts, dsd_state* state, const struct UiCmd* c) {
    (void)state;
    if (opts && c->n > 0) {
        char path[1024] = {0};
        size_t n = c->n < sizeof(path) ? c->n : sizeof(path) - 1;
        memcpy(path, c->data, n);
        path[n] = '\0';
        svc_set_event_log(opts, path);
    }
    return 1;
}

const struct UiCmdReg ui_actions_logging[] = {
    {UI_CMD_EH_NEXT, ui_handle_eh_next},
    {UI_CMD_EH_PREV, ui_handle_eh_prev},
    {UI_CMD_EH_TOGGLE_SLOT, ui_handle_eh_toggle_slot},
    {UI_CMD_UI_MSG_CLEAR, ui_handle_ui_msg_clear},
    {UI_CMD_EH_RESET, ui_handle_eh_reset},
    {UI_CMD_EVENT_LOG_DISABLE, ui_handle_event_log_disable},
    {UI_CMD_EVENT_LOG_SET, ui_handle_event_log_set},
    {0, NULL},
};
