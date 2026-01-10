// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Basic tests for INI-based user configuration.
 *
 * Exercises load/apply/snapshot behavior for a representative config
 * without touching CLI or environment precedence.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/runtime/config.h>

#include "test_support.h"

static int
write_temp_config(const char* contents, char* out_path, size_t out_sz) {
    char tmpl[DSD_TEST_PATH_MAX];
    int fd = dsd_test_mkstemp(tmpl, sizeof(tmpl), "dsdneo_config_user");
    if (fd < 0) {
        fprintf(stderr, "dsd_test_mkstemp failed: %s\n", strerror(errno));
        return 1;
    }
    size_t len = strlen(contents);
    ssize_t wr = dsd_write(fd, contents, len);
    if (wr < 0 || (size_t)wr != len) {
        fprintf(stderr, "write failed: %s\n", strerror(errno));
        (void)dsd_close(fd);
        (void)remove(tmpl);
        return 1;
    }
    (void)dsd_close(fd);
    snprintf(out_path, out_sz, "%s", tmpl);
    out_path[out_sz - 1] = '\0';
    return 0;
}

static int
test_load_and_apply_basic(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"rtl\"\n"
                             "rtl_device = 1\n"
                             "rtl_freq = \"851.375M\"\n"
                             "rtl_gain = 30\n"
                             "rtl_ppm = 5\n"
                             "rtl_bw_khz = 16\n"
                             "rtl_sql = -50\n"
                             "rtl_volume = 2\n"
                             "\n"
                             "[output]\n"
                             "backend = \"null\"\n"
                             "ncurses_ui = true\n"
                             "\n"
                             "[mode]\n"
                             "decode = \"dmr\"\n"
                             "\n"
                             "[trunking]\n"
                             "enabled = true\n"
                             "chan_csv = \"/tmp/chan.csv\"\n"
                             "group_csv = \"/tmp/group.csv\"\n"
                             "allow_list = true\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdneoUserConfig cfg;
    if (dsd_user_config_load(path, &cfg) != 0) {
        fprintf(stderr, "dsd_user_config_load failed for %s\n", path);
        (void)remove(path);
        return 1;
    }

    int rc = 0;
    if (!cfg.has_input || cfg.input_source != DSDCFG_INPUT_RTL) {
        fprintf(stderr, "input section not parsed as RTL\n");
        rc |= 1;
    }
    if (!cfg.has_output || cfg.output_backend != DSDCFG_OUTPUT_NULL || cfg.ncurses_ui != 1) {
        fprintf(stderr, "output section not parsed correctly\n");
        rc |= 1;
    }
    if (!cfg.has_mode || cfg.decode_mode != DSDCFG_MODE_DMR) {
        fprintf(stderr, "mode section not parsed as DMR\n");
        rc |= 1;
    }
    if (!cfg.has_trunking || !cfg.trunk_enabled || !cfg.trunk_use_allow_list) {
        fprintf(stderr, "trunking section not parsed correctly\n");
        rc |= 1;
    }

    static dsd_opts opts;
    static dsd_state state;
    memset(&opts, 0, sizeof opts);
    memset(&state, 0, sizeof state);

    dsd_apply_user_config_to_opts(&cfg, &opts, &state);

    if (strcmp(opts.audio_in_dev, "rtl:1:851.375M:30:5:16:-50:2") != 0) {
        fprintf(stderr, "audio_in_dev mismatch: \"%s\"\n", opts.audio_in_dev);
        rc |= 1;
    }
    if (strcmp(opts.audio_out_dev, "null") != 0) {
        fprintf(stderr, "audio_out_dev mismatch: \"%s\"\n", opts.audio_out_dev);
        rc |= 1;
    }
    if (opts.use_ncurses_terminal != 1) {
        fprintf(stderr, "use_ncurses_terminal not enabled\n");
        rc |= 1;
    }
    if (!(opts.frame_dmr == 1 && opts.frame_p25p1 == 0 && opts.frame_p25p2 == 0 && opts.frame_ysf == 0)) {
        fprintf(stderr, "DMR mode flags not applied as expected\n");
        rc |= 1;
    }
    if (!(opts.p25_trunk == 1 && opts.trunk_enable == 1)) {
        fprintf(stderr, "trunking not enabled in opts\n");
        rc |= 1;
    }
    if (strcmp(opts.chan_in_file, "/tmp/chan.csv") != 0 || strcmp(opts.group_in_file, "/tmp/group.csv") != 0) {
        fprintf(stderr, "trunk CSV paths not applied correctly\n");
        rc |= 1;
    }
    if (opts.trunk_use_allow_list != 1) {
        fprintf(stderr, "trunk_use_allow_list not set\n");
        rc |= 1;
    }

    (void)remove(path);
    return rc;
}

static int
test_snapshot_roundtrip(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[input]\n"
                             "source = \"udp\"\n"
                             "udp_addr = \"127.0.0.1\"\n"
                             "udp_port = 9000\n"
                             "\n"
                             "[output]\n"
                             "backend = \"pulse\"\n"
                             "ncurses_ui = false\n"
                             "\n"
                             "[mode]\n"
                             "decode = \"analog\"\n"
                             "\n"
                             "[trunking]\n"
                             "enabled = false\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdneoUserConfig cfg;
    if (dsd_user_config_load(path, &cfg) != 0) {
        fprintf(stderr, "dsd_user_config_load failed for %s\n", path);
        (void)remove(path);
        return 1;
    }

    static dsd_opts opts;
    static dsd_state state;
    memset(&opts, 0, sizeof opts);
    memset(&state, 0, sizeof state);

    dsd_apply_user_config_to_opts(&cfg, &opts, &state);

    dsdneoUserConfig snap;
    dsd_snapshot_opts_to_user_config(&opts, &state, &snap);

    int rc = 0;
    if (!snap.has_input || snap.input_source != DSDCFG_INPUT_UDP) {
        fprintf(stderr, "snapshot input_source mismatch\n");
        rc |= 1;
    }
    if (strcmp(snap.udp_addr, "127.0.0.1") != 0 || snap.udp_port != 9000) {
        fprintf(stderr, "snapshot udp_addr/udp_port mismatch: %s:%d\n", snap.udp_addr, snap.udp_port);
        rc |= 1;
    }
    if (!snap.has_output || snap.output_backend != DSDCFG_OUTPUT_PULSE) {
        fprintf(stderr, "snapshot output_backend mismatch\n");
        rc |= 1;
    }
    if (!snap.has_mode || snap.decode_mode != DSDCFG_MODE_ANALOG) {
        fprintf(stderr, "snapshot decode_mode mismatch\n");
        rc |= 1;
    }
    if (!snap.has_trunking) {
        fprintf(stderr, "snapshot missing trunking section\n");
        rc |= 1;
    }
    if (snap.trunk_enabled != 0) {
        fprintf(stderr, "snapshot trunk_enabled should be false for this config\n");
        rc |= 1;
    }

    // Render snapshot to an in-memory file to ensure INI output does not crash.
    FILE* tmp = tmpfile();
    if (!tmp) {
        fprintf(stderr, "tmpfile failed: %s\n", strerror(errno));
        (void)remove(path);
        return 1;
    }
    dsd_user_config_render_ini(&snap, tmp);
    fclose(tmp);

    (void)remove(path);
    return rc;
}

static int
test_apply_demod_lock(void) {
    static const char* ini = "version = 1\n"
                             "\n"
                             "[mode]\n"
                             "decode = \"auto\"\n"
                             "demod = \"qpsk\"\n";

    char path[DSD_TEST_PATH_MAX];
    if (write_temp_config(ini, path, sizeof path) != 0) {
        return 1;
    }

    dsdneoUserConfig cfg;
    if (dsd_user_config_load(path, &cfg) != 0) {
        fprintf(stderr, "dsd_user_config_load failed for %s\n", path);
        (void)remove(path);
        return 1;
    }

    static dsd_opts opts;
    static dsd_state state;
    memset(&opts, 0, sizeof opts);
    memset(&state, 0, sizeof state);

    dsd_apply_user_config_to_opts(&cfg, &opts, &state);

    int rc = 0;
    if (!(opts.mod_cli_lock == 1 && opts.mod_qpsk == 1 && opts.mod_c4fm == 0 && opts.mod_gfsk == 0)) {
        fprintf(stderr, "demod lock not applied correctly (c4fm=%d qpsk=%d gfsk=%d lock=%d)\n", opts.mod_c4fm,
                opts.mod_qpsk, opts.mod_gfsk, opts.mod_cli_lock);
        rc |= 1;
    }
    if (state.rf_mod != 1) {
        fprintf(stderr, "rf_mod should be 1 for QPSK lock, got %d\n", state.rf_mod);
        rc |= 1;
    }

    (void)remove(path);
    return rc;
}

static int
test_snapshot_persists_demod_lock(void) {
    static dsd_opts opts;
    static dsd_state state;
    memset(&opts, 0, sizeof opts);
    memset(&state, 0, sizeof state);

    snprintf(opts.audio_in_dev, sizeof opts.audio_in_dev, "pulse");
    snprintf(opts.audio_out_dev, sizeof opts.audio_out_dev, "null");
    opts.mod_cli_lock = 1;
    opts.mod_qpsk = 1;
    opts.mod_c4fm = 0;
    opts.mod_gfsk = 0;
    state.rf_mod = 1;

    dsdneoUserConfig snap;
    dsd_snapshot_opts_to_user_config(&opts, &state, &snap);

    int rc = 0;
    if (!snap.has_demod || snap.demod_path != DSDCFG_DEMOD_QPSK) {
        fprintf(stderr, "snapshot missing demod lock (has_demod=%d demod_path=%d)\n", snap.has_demod, snap.demod_path);
        rc |= 1;
    }

    FILE* tmp = tmpfile();
    if (!tmp) {
        fprintf(stderr, "tmpfile failed: %s\n", strerror(errno));
        return 1;
    }
    dsd_user_config_render_ini(&snap, tmp);
    char buf[512];
    if (fseek(tmp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "fseek failed\n");
        fclose(tmp);
        return 1;
    }
    size_t n = fread(buf, 1, sizeof buf - 1, tmp);
    if (n == 0 && ferror(tmp)) {
        fprintf(stderr, "fread failed\n");
        fclose(tmp);
        return 1;
    }
    buf[n] = '\0';
    fclose(tmp);

    if (!strstr(buf, "demod = \"qpsk\"")) {
        fprintf(stderr, "rendered INI missing demod line:\n%s\n", buf);
        rc |= 1;
    }

    return rc;
}

int
main(void) {
    int rc = 0;
    rc |= test_load_and_apply_basic();
    rc |= test_snapshot_roundtrip();
    rc |= test_apply_demod_lock();
    rc |= test_snapshot_persists_demod_lock();
    return rc;
}
