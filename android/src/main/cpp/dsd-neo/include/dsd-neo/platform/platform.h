// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

/*
 * Platform detection macros for DSD-neo
 */

/* Detect Windows */
#if defined(_WIN32) || defined(_WIN64)
#define DSD_PLATFORM_WINDOWS 1
#else
#define DSD_PLATFORM_WINDOWS 0
#endif

/* Detect Linux */
#if defined(__linux__)
#define DSD_PLATFORM_LINUX 1
#else
#define DSD_PLATFORM_LINUX 0
#endif

/* Detect macOS */
#if defined(__APPLE__) && defined(__MACH__)
#define DSD_PLATFORM_MACOS 1
#else
#define DSD_PLATFORM_MACOS 0
#endif

/* Detect POSIX-like systems */
#if DSD_PLATFORM_LINUX || DSD_PLATFORM_MACOS || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define DSD_PLATFORM_POSIX 1
#else
#define DSD_PLATFORM_POSIX 0
#endif

/* Native Windows */
#if defined(_WIN32)
#define DSD_PLATFORM_WIN_NATIVE 1
#else
#define DSD_PLATFORM_WIN_NATIVE 0
#endif

/* Compiler detection */
#if defined(_MSC_VER)
#define DSD_COMPILER_MSVC 1
#else
#define DSD_COMPILER_MSVC 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define DSD_COMPILER_GCC 1
#else
#define DSD_COMPILER_GCC 0
#endif

#if defined(__clang__)
#define DSD_COMPILER_CLANG 1
#else
#define DSD_COMPILER_CLANG 0
#endif
