// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
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

#define DSD_NEO_MAIN

#include <dsd-neo/core/init.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/engine/engine.h>
#include <dsd-neo/protocol/dmr/dmr_const.h>
#include <dsd-neo/protocol/dstar/dstar_const.h>
#include <dsd-neo/protocol/nxdn/nxdn_const.h>
#include <dsd-neo/protocol/p25/p25p1_const.h>
#include <dsd-neo/protocol/provoice/provoice_const.h>
#include <dsd-neo/protocol/x2tdma/x2tdma_const.h>
#include <dsd-neo/runtime/bootstrap.h>

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char** argv) {
    dsd_opts* opts = calloc(1, sizeof(dsd_opts));
    dsd_state* state = calloc(1, sizeof(dsd_state));
    if (!opts || !state) {
        fprintf(stderr, "Failed to allocate memory for opts/state\n");
        free(opts);
        free(state);
        return 1;
    }

    initOpts(opts);
    initState(state);

    int exit_rc = 1;
    int bootstrap_rc = dsd_runtime_bootstrap(argc, argv, opts, state, NULL, &exit_rc);
    if (bootstrap_rc != DSD_BOOTSTRAP_CONTINUE) {
        freeState(state);
        free(opts);
        free(state);
        return exit_rc;
    }
    int rc = dsd_engine_run(opts, state);
    freeState(state);
    free(opts);
    free(state);
    return rc;
}
