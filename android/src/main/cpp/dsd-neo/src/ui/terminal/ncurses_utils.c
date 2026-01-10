// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*
 * ncurses_utils.c
 * Shared utility functions for ncurses UI modules
 */

#include <dsd-neo/ui/ncurses_internal.h>

#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/platform/curses_compat.h>
#include <dsd-neo/runtime/unicode.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Shared state: last sync type seen by the UI (updated by ncursesPrinter) */
int ncurses_last_synctype = DSD_SYNC_NONE;

int
ui_unicode_supported(void) {
#if defined(DSD_USE_PDCURSES)
    PDC_VERSION ver;
    memset(&ver, 0, sizeof(ver));
    PDC_get_version(&ver);
    if ((ver.flags & (PDC_VFLAG_WIDE | PDC_VFLAG_UTF8)) == (PDC_VFLAG_WIDE | PDC_VFLAG_UTF8)) {
        return 1;
    }
#endif
    return dsd_unicode_supported();
}

/* Quickselect helpers for int arrays (k-th smallest in O(n)) */

void
swap_int_local(int* a, int* b) {
    int t = *a;
    *a = *b;
    *b = t;
}

int
select_k_int_local(int* a, int n, int k) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int pivot = a[mid];
        int lt = lo, i = lo, gt = hi;
        while (i <= gt) {
            if (a[i] < pivot) {
                swap_int_local(&a[i++], &a[lt++]);
            } else if (a[i] > pivot) {
                swap_int_local(&a[i], &a[gt--]);
            } else {
                i++;
            }
        }
        if (k < lt) {
            hi = lt - 1;
        } else if (k > gt) {
            lo = gt + 1;
        } else {
            return a[k];
        }
    }
    return a[k];
}

/* Comparator for ascending sort of int values (used in FSK histogram quartiles) */
int
cmp_int_asc(const void* a, const void* b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    return (ia > ib) - (ia < ib);
}

/* Small percentile helper for u8 rings (len <= 64) */
int
compute_percentiles_u8(const uint8_t* src, int len, double* p50, double* p95) {
    if (!src || len <= 0) {
        return 0;
    }
    if (len > 64) {
        len = 64;
    }
    int vals[64] = {0};
    for (int i = 0; i < len; i++) {
        vals[i] = (int)src[i];
    }
    qsort(vals, len, sizeof(int), cmp_int_asc);
    int i50 = (int)lrint(0.50 * (double)(len - 1));
    int i95 = (int)lrint(0.95 * (double)(len - 1));
    if (i50 < 0) {
        i50 = 0;
    }
    if (i95 < 0) {
        i95 = 0;
    }
    if (i50 >= len) {
        i50 = len - 1;
    }
    if (i95 >= len) {
        i95 = len - 1;
    }
    if (p50) {
        *p50 = (double)vals[i50];
    }
    if (p95) {
        *p95 = (double)vals[i95];
    }
    return 1;
}

/* Determine if an Active Channel label refers to a locked-out target.
 * Supports both "TG:" (group) and "TGT:" (target/private/data) fields.
 * Returns 1 when the referenced ID is marked with groupMode "DE" or "B". */
int
ui_is_locked_from_label(const dsd_state* state, const char* label) {
    if (!state || !label || !*label) {
        return 0;
    }
    /* Try group first ("TG:") */
    const char* pos = strstr(label, "TG:");
    if (!pos) {
        /* Fallback to generic target ("TGT:") often used for private/data */
        pos = strstr(label, "TGT:");
    }
    if (!pos) {
        return 0;
    }
    pos += 3; /* skip TG: or TGT: prefix; both are 3 chars before ':' */
    if (*pos == ':') {
        pos++;
    }
    while (*pos == ' ') {
        pos++;
    }
    char* endp = NULL;
    long id = strtol(pos, &endp, 10);
    if (endp == pos || id <= 0) {
        return 0;
    }
    for (unsigned int k = 0; k < state->group_tally; k++) {
        if (state->group_array[k].groupNumber == (unsigned long)id) {
            const char* m = state->group_array[k].groupMode;
            if (strcmp(m, "DE") == 0 || strcmp(m, "B") == 0) {
                return 1;
            }
            break;
        }
    }
    return 0;
}
