// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief EDACS helper interfaces.
 */

#pragma once

#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

int getAfsString(dsd_state* state, char* buffer, int a, int f, int s);
int getAfsStringLength(dsd_state* state);

#ifdef __cplusplus
}
#endif
