// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Protocol dispatch interface for mapping synctypes to handlers.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dsd_protocol_handler {
    const char* name;
    int (*matches_synctype)(int synctype);
    void (*handle_frame)(dsd_opts* opts, dsd_state* state);
    void (*on_reset)(dsd_opts* opts, dsd_state* state);
} dsd_protocol_handler;

extern const dsd_protocol_handler dsd_protocol_handlers[];

#ifdef __cplusplus
}
#endif
