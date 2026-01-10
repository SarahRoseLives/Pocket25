// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Core event logging helpers.
 *
 * Declares event-history helpers implemented in core.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_event_history(Event_History_I* event_struct, uint8_t start, uint8_t stop);
void push_event_history(Event_History_I* event_struct);
void write_event_to_log_file(dsd_opts* opts, dsd_state* state, uint8_t slot, uint8_t swrite, char* event_string);
void watchdog_event_history(dsd_opts* opts, dsd_state* state, uint8_t slot);
void watchdog_event_current(dsd_opts* opts, dsd_state* state, uint8_t slot);
void watchdog_event_datacall(dsd_opts* opts, dsd_state* state, uint32_t src, uint32_t dst, char* data_string,
                             uint8_t slot);

#ifdef __cplusplus
}
#endif
