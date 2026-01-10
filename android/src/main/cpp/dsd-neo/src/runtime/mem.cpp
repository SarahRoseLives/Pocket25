// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Aligned memory allocation utilities for DSP operations.
 *
 * Provides `dsd_neo_aligned_malloc` and `dsd_neo_aligned_free` with a default
 * alignment of `DSD_NEO_ALIGN` for SIMD-friendly and cache-efficient buffers.
 */

#include <cstdlib>

#include <dsd-neo/platform/posix_compat.h>
#include <dsd-neo/runtime/mem.h>

/**
 * @brief Allocate memory aligned to `DSD_NEO_ALIGN`.
 *
 * Uses cross-platform dsd_aligned_alloc, falling back to malloc on failure.
 *
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure or when `size` is 0.
 */
void*
dsd_neo_aligned_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    void* mem_ptr = dsd_aligned_alloc(DSD_NEO_ALIGN, size);
    if (!mem_ptr) {
        mem_ptr = std::malloc(size);
    }
    return mem_ptr;
}

/**
 * @brief Free memory allocated by `dsd_neo_aligned_malloc`.
 *
 * Uses cross-platform dsd_aligned_free for properly allocated memory.
 * Note: On Windows, memory from _aligned_malloc MUST be freed with
 * _aligned_free. The dsd_aligned_free wrapper handles this correctly.
 *
 * @param ptr Pointer previously returned by `dsd_neo_aligned_malloc`.
 */
void
dsd_neo_aligned_free(void* ptr) {
    dsd_aligned_free(ptr);
}
