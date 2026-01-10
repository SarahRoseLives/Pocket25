// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Lightweight condition helpers shared across runtime components.
 */

#pragma once

#include <dsd-neo/platform/threading.h>

/** @brief Signal a condition variable while holding its mutex. */
#define safe_cond_signal(n, m)                                                                                         \
    dsd_mutex_lock(m);                                                                                                 \
    dsd_cond_signal(n);                                                                                                \
    dsd_mutex_unlock(m)

/** @brief Wait on a condition variable while holding its mutex. */
#define safe_cond_wait(n, m)                                                                                           \
    dsd_mutex_lock(m);                                                                                                 \
    dsd_cond_wait(n, m);                                                                                               \
    dsd_mutex_unlock(m)
