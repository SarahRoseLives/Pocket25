// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Runtime memory management interface for aligned allocations.
 *
 * Declares `dsd_neo_aligned_malloc` and `dsd_neo_aligned_free`, providing a
 * default alignment of `DSD_NEO_ALIGN` for DSP-intensive buffers.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default alignment for hot DSP buffers */
#ifndef DSD_NEO_ALIGN
#define DSD_NEO_ALIGN 64
#endif

/**
 * @brief Allocate memory aligned to `DSD_NEO_ALIGN`.
 *
 * Falls back to `malloc` if an aligned allocation API is unavailable.
 *
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure or when `size` is 0.
 */
void* dsd_neo_aligned_malloc(size_t size);

/**
 * @brief Free memory allocated by `dsd_neo_aligned_malloc`.
 *
 * Also valid for memory allocated by the plain `malloc` fallback.
 *
 * @param ptr Pointer previously returned by `dsd_neo_aligned_malloc`.
 */
void dsd_neo_aligned_free(void* ptr);

#ifdef __cplusplus
}
#endif
