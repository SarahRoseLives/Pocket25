// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief ANSI terminal color escape macros.
 *
 * These macros are used across legacy code paths to colorize stderr/stdout
 * output. Colorization can be toggled at build time via PRETTY_COLORS_LOGS.
 */

#pragma once

// ANSI Color Characters in Terminal -- Disable by using cmake -DCOLORSLOGS=Off ..
#ifdef PRETTY_COLORS_LOGS
#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"
#else
#define KNRM ""
#define KRED ""
#define KGRN ""
#define KYEL ""
#define KBLU ""
#define KMAG ""
#define KCYN ""
#define KWHT ""
#endif
