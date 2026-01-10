// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*
 * Copyright (C) 2010 DSD Author
 * GPG Key ID: 0x3F1D7FD0 (74EF 430D F7F2 0A48 FCE6  F630 FAA2 635D 3F1D 7FD0)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/colors.h>

#include <stdio.h>

void
printFrameInfo(dsd_opts* opts, dsd_state* state) {

    UNUSED(opts);

    fprintf(stderr, "%s", KCYN);
    if (state->p2_wacn != 0) {
        fprintf(stderr, "WACN: %05llX; ", state->p2_wacn);
    }
    if (state->p2_sysid != 0) {
        fprintf(stderr, "SYS: %03llX; ", state->p2_sysid);
    }
    if (state->p2_cc != 0) {
        fprintf(stderr, "NAC/CC: %03llX; ", state->p2_cc);
    } else {
        fprintf(stderr, "NAC: %03X; ", state->nac);
    }

    if (state->p2_rfssid != 0) {
        fprintf(stderr, "RFSS: %03lld; ", state->p2_rfssid);
    }
    if (state->p2_siteid != 0) {
        fprintf(stderr, "Site: %03lld; ", state->p2_siteid);
    }
    fprintf(stderr, "%s", KNRM);
}
