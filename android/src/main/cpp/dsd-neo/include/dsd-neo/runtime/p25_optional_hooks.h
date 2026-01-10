// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime hook table for optional P25 side effects.
 *
 * Some protocol-only builds and tests link P25 without the full core module.
 * The engine installs the real hook functions at startup; the runtime provides
 * safe no-op wrappers until then.
 */
#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*watchdog_event_current)(dsd_opts* opts, dsd_state* state, uint8_t slot);
    void (*write_event_to_log_file)(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t swrite, char* event_string);
    void (*push_event_history)(Event_History_I* event_struct);
    void (*init_event_history)(Event_History_I* event_struct, uint8_t start, uint8_t stop);
    void (*p25p2_flush_partial_audio)(dsd_opts* opts, dsd_state* state);
} dsd_p25_optional_hooks;

void dsd_p25_optional_hooks_set(dsd_p25_optional_hooks hooks);

void dsd_p25_optional_hook_watchdog_event_current(dsd_opts* opts, dsd_state* state, uint8_t slot);
void dsd_p25_optional_hook_write_event_to_log_file(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t swrite,
                                                   char* event_string);
void dsd_p25_optional_hook_push_event_history(Event_History_I* event_struct);
void dsd_p25_optional_hook_init_event_history(Event_History_I* event_struct, uint8_t start, uint8_t stop);
void dsd_p25_optional_hook_p25p2_flush_partial_audio(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
