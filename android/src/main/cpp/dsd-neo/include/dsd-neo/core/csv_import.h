// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief CSV import helpers for runtime/UI one-shot actions.
 *
 * Declares CSV import entrypoints implemented in core.
 */

#pragma once

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

int csvGroupImport(dsd_opts* opts, dsd_state* state);
int csvLCNImport(dsd_opts* opts, dsd_state* state);
int csvChanImport(dsd_opts* opts, dsd_state* state);
int csvKeyImportDec(dsd_opts* opts, dsd_state* state);
int csvKeyImportHex(dsd_opts* opts, dsd_state* state);

#ifdef __cplusplus
}
#endif
