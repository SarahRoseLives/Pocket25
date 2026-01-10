// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Minimal smoke test for UI_CMD_CONFIG_APPLY runtime behavior.
 *
 * This does not spawn the full ncurses UI; it exercises the config apply
 * command handler with a fake dsd_opts/dsd_state to ensure that applying a
 * config that changes basic fields does not crash and updates core fields as
 * expected. Backend-specific restarts (RTL/RTLTCP/TCP/UDP/Pulse) are covered
 * indirectly by existing integration paths and are intentionally not mocked
 * here to keep this test simple and portable.
 */

#include <dsd-neo/runtime/config.h>
#include <dsd-neo/ui/ui_async.h>
#include <dsd-neo/ui/ui_cmd.h>

#include <dsd-neo/core/init.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>

int
main(void) {
    dsd_opts opts;
    dsd_state state;
    initOpts(&opts);
    initState(&state);

    // Start from a known input/output so that config apply has something to
    // mutate. Use Pulse I/O to avoid depending on RTL or network resources.
    snprintf(opts.audio_in_dev, sizeof opts.audio_in_dev, "%s", "pulse");
    opts.audio_in_type = AUDIO_IN_PULSE;
    snprintf(opts.audio_out_dev, sizeof opts.audio_out_dev, "%s", "pulse");
    opts.audio_out_type = 0;

    dsdneoUserConfig cfg;
    cfg.version = 1;
    cfg.has_input = 1;
    cfg.input_source = DSDCFG_INPUT_PULSE;
    snprintf(cfg.pulse_input, sizeof cfg.pulse_input, "%s", "test-source");
    cfg.has_output = 1;
    cfg.output_backend = DSDCFG_OUTPUT_PULSE;
    snprintf(cfg.pulse_output, sizeof cfg.pulse_output, "%s", "test-sink");
    cfg.ncurses_ui = 1;

    // Public API: ui_post_cmd() enqueues; ui_drain_cmds() is called from the
    // demod loop to apply pending commands. For the purposes of this test we
    // call both directly.
    ui_post_cmd(UI_CMD_CONFIG_APPLY, &cfg, sizeof cfg);
    ui_drain_cmds(&opts, &state);

    // Basic invariants: ncurses flag set, audio devs still pulse-based.
    if (!opts.use_ncurses_terminal) {
        return 2;
    }
    if (strncmp(opts.audio_in_dev, "pulse", 5) != 0) {
        return 3;
    }
    if (strncmp(opts.audio_out_dev, "pulse", 5) != 0) {
        return 4;
    }

    return 0;
}
