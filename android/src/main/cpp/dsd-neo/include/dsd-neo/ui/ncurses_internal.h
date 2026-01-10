// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/**
 * @file ncurses_internal.h
 * @brief Internal shared declarations for ncurses UI modules.
 *
 * Provides utility functions and shared state used across the modularized
 * ncurses display components.
 */

#pragma once

#include <stdint.h>

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared state (accessed by multiple display modules) */
extern int ncurses_last_synctype; /* renamed from static `lls` */

/* Utility functions */
void swap_int_local(int* a, int* b);
int select_k_int_local(int* a, int n, int k);
int cmp_int_asc(const void* a, const void* b);
int compute_percentiles_u8(const uint8_t* src, int len, double* p50, double* p95);
int ui_is_locked_from_label(const dsd_state* state, const char* label);
int ui_unicode_supported(void);

#ifdef __cplusplus
}
#endif
