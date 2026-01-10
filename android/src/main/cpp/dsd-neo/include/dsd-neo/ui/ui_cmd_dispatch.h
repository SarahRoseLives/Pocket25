// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief UI command dispatch registry and handler signature.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>
#include <dsd-neo/ui/ui_cmd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Common handler signature for UI command actions. */
typedef int (*UiCmdHandler)(dsd_opts* opts, dsd_state* state, const struct UiCmd* cmd);

/** Registry entry mapping command id -> handler function. */
struct UiCmdReg {
    int id;
    UiCmdHandler fn;
};

/** Per-domain registries (terminated with `{0, NULL}`). */
extern const struct UiCmdReg ui_actions_audio[];
extern const struct UiCmdReg ui_actions_radio[];
extern const struct UiCmdReg ui_actions_trunk[];
extern const struct UiCmdReg ui_actions_logging[];

#ifdef __cplusplus
}
#endif
