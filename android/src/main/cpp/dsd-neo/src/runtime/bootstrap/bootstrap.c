// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/runtime/bootstrap.h>

#include <dsd-neo/runtime/cli.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/git_ver.h>
#include <dsd-neo/runtime/log.h>

#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/posix_compat.h>

#include <mbelib.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
bootstrap_set_exit_rc(int* out_exit_rc, int rc) {
    if (out_exit_rc) {
        *out_exit_rc = rc;
    }
}

static const char*
bootstrap_default_or_env_config_path(const char* cli_path, const char* env_path) {
    if (cli_path && *cli_path) {
        return cli_path;
    }
    if (env_path && *env_path) {
        return env_path;
    }
    return dsd_user_config_default_path();
}

static int
bootstrap_is_ini_path(const char* path) {
    if (!path) {
        return 0;
    }
    size_t n = strlen(path);
    if (n < 4) {
        return 0;
    }
    const char* ext = path + (n - 4);
    return ext[0] == '.' && tolower((unsigned char)ext[1]) == 'i' && tolower((unsigned char)ext[2]) == 'n'
           && tolower((unsigned char)ext[3]) == 'i';
}

int
dsd_runtime_bootstrap(int argc, char** argv, dsd_opts* opts, dsd_state* state, int* out_argc_effective,
                      int* out_exit_rc) {
    if (!opts || !state || argc < 0 || !argv) {
        bootstrap_set_exit_rc(out_exit_rc, 1);
        return DSD_BOOTSTRAP_ERROR;
    }

    int argc_effective = argc;

    // Optional: user configuration file (INI) -----------------------------
    int enable_config_cli = 0;
    int force_bootstrap_cli = 0;
    int print_config_cli = 0;
    int dump_template_cli = 0;
    int validate_config_cli = 0;
    int strict_config_cli = 0;
    int list_profiles_cli = 0;
    const char* config_path_cli = NULL;
    const char* profile_cli = NULL;
    const char* validate_path_cli = NULL;

    int cfg_path_positional_ini = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            enable_config_cli = 1;
            // Optional path argument (if next arg doesn't start with '-')
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                config_path_cli = argv[++i];
            }
        } else if (strcmp(argv[i], "--interactive-setup") == 0) {
            force_bootstrap_cli = 1;
        } else if (strcmp(argv[i], "--print-config") == 0) {
            print_config_cli = 1;
        } else if (strcmp(argv[i], "--dump-config-template") == 0) {
            dump_template_cli = 1;
        } else if (strcmp(argv[i], "--validate-config") == 0) {
            validate_config_cli = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                validate_path_cli = argv[++i];
            }
        } else if (strcmp(argv[i], "--strict-config") == 0) {
            strict_config_cli = 1;
        } else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            profile_cli = argv[++i];
        } else if (strcmp(argv[i], "--list-profiles") == 0) {
            list_profiles_cli = 1;
        }
    }

    // Back-compat/UX: treat a single positional *.ini argument as "--config <path>".
    // This keeps "dsd-neo ~/.config/dsd-neo/foo.ini" from silently falling back to defaults.
    if (!enable_config_cli && argc == 2 && argv[1] != NULL && argv[1][0] != '-' && bootstrap_is_ini_path(argv[1])) {
        enable_config_cli = 1;
        config_path_cli = argv[1];
        cfg_path_positional_ini = 1;
    }

    dsd_neo_config_init(opts);
    const dsdneoRuntimeConfig* rcfg = dsd_neo_get_config();
    const char* config_env = (rcfg && rcfg->config_path_is_set) ? rcfg->config_path : NULL;

    int user_cfg_loaded = 0;
    dsdneoUserConfig user_cfg;
    memset(&user_cfg, 0, sizeof user_cfg);

    // Default to no autosave unless a config is actually in play for this run.
    state->config_autosave_enabled = 0;
    state->config_autosave_path[0] = '\0';

    /* Config loading is opt-in: only load if --config is passed (with or
     * without a path) or if DSD_NEO_CONFIG env var is set. CLI takes
     * precedence: --config without a path uses the default, ignoring env. */
    if (enable_config_cli || (config_env && *config_env)) {
        const char* cfg_path = NULL;
        if (config_path_cli && *config_path_cli) {
            cfg_path = config_path_cli;
        } else if (enable_config_cli) {
            cfg_path = dsd_user_config_default_path();
        } else if (config_env && *config_env) {
            cfg_path = config_env;
        }

        if (cfg_path && *cfg_path) {
            // Remember the path so we can autosave the effective config later.
            state->config_autosave_enabled = 1;
            snprintf(state->config_autosave_path, sizeof state->config_autosave_path, "%s", cfg_path);
            state->config_autosave_path[sizeof state->config_autosave_path - 1] = '\0';

            int load_rc;
            if (profile_cli && *profile_cli) {
                load_rc = dsd_user_config_load_profile(cfg_path, profile_cli, &user_cfg);
            } else {
                load_rc = dsd_user_config_load(cfg_path, &user_cfg);
            }

            if (load_rc == 0) {
                dsd_apply_user_config_to_opts(&user_cfg, opts, state);
                user_cfg_loaded = 1;
                if (profile_cli && *profile_cli) {
                    LOG_NOTICE("Loaded user config from %s (profile: %s)\n", cfg_path, profile_cli);
                } else {
                    LOG_NOTICE("Loaded user config from %s\n", cfg_path);
                }
            } else if (profile_cli && *profile_cli) {
                // Missing profile is fatal when --profile is specified
                LOG_ERROR("Profile '%s' not found in config file %s\n", profile_cli, cfg_path);
                bootstrap_set_exit_rc(out_exit_rc, 1);
                return DSD_BOOTSTRAP_ERROR;
            } else if (config_path_cli || config_env || enable_config_cli) {
                LOG_WARNING("Failed to load config file from %s; proceeding without config.\n", cfg_path);
            }
        }
    } else {
        // Config loading was not requested; do not autosave either.
        state->config_autosave_enabled = 0;
        state->config_autosave_path[0] = '\0';
    }

    {
        int parse_exit_rc = 1;
        const int parse_argc = cfg_path_positional_ini ? 1 : argc;
        int parse_rc = dsd_parse_args(parse_argc, argv, opts, state, &argc_effective, &parse_exit_rc);
        if (parse_rc == DSD_PARSE_ONE_SHOT) {
            bootstrap_set_exit_rc(out_exit_rc, parse_exit_rc);
            return DSD_BOOTSTRAP_EXIT;
        }
        if (parse_rc == DSD_PARSE_ERROR) {
            bootstrap_set_exit_rc(out_exit_rc, parse_exit_rc);
            return DSD_BOOTSTRAP_ERROR;
        }
        if (parse_rc != DSD_PARSE_CONTINUE) {
            bootstrap_set_exit_rc(out_exit_rc, 1);
            return DSD_BOOTSTRAP_ERROR;
        }
        // Keep original argc for UI bootstrap heuristics; use argc_effective
        // only when iterating argv for file playback (-r).
    }

    state->cli_argc_effective = argc_effective;
    state->cli_argv = argv;
    if (out_argc_effective) {
        *out_argc_effective = argc_effective;
    }

    /* Re-parse env-derived config after CLI mapping (CLI sets DSD_NEO_* env overrides). */
    dsd_neo_config_init(opts);
    dsd_apply_runtime_config_to_opts(dsd_neo_get_config(), opts, state);

    // If a user config enabled trunking but this process was started with
    // any effective CLI arguments and none of them explicitly enabled/disabled
    // trunk (via -T / -Y), fall back to the built-in default of trunking
    // disabled for this run. This keeps CLI-driven sessions from inheriting
    // trunk enable solely from the config file, while allowing config-driven
    // runs like: dsd-neo --config <path>
    if (argc_effective > 1 && user_cfg_loaded && !opts->trunk_cli_seen) {
        opts->p25_trunk = 0;
        opts->trunk_enable = 0;
    }

    // If a user config specified a non-48kHz file/RAW input and the CLI did
    // not override its sample rate, apply the corresponding symbol timing
    // scaling after all CLI/env parsing so that mode presets are adjusted
    // correctly. This mirrors legacy "-s" behavior without requiring users
    // to manage option ordering manually when using the config file.
    if (user_cfg_loaded && user_cfg.has_input && user_cfg.input_source == DSDCFG_INPUT_FILE
        && user_cfg.file_sample_rate > 0 && user_cfg.file_sample_rate != 48000 && opts->wav_decimator != 0
        && user_cfg.file_path[0] != '\0' && strcmp(opts->audio_in_dev, user_cfg.file_path) == 0
        && opts->wav_sample_rate == user_cfg.file_sample_rate) {
        opts->wav_interpolator = opts->wav_sample_rate / opts->wav_decimator;
        state->samplesPerSymbol = state->samplesPerSymbol * opts->wav_interpolator;
        state->symbolCenter = state->symbolCenter * opts->wav_interpolator;
    }

    if (print_config_cli) {
        dsdneoUserConfig eff;
        dsd_snapshot_opts_to_user_config(opts, state, &eff);
        dsd_user_config_render_ini(&eff, stdout);
        bootstrap_set_exit_rc(out_exit_rc, 0);
        return DSD_BOOTSTRAP_EXIT;
    }

    // --dump-config-template: print commented template and exit
    if (dump_template_cli) {
        dsd_user_config_render_template(stdout);
        bootstrap_set_exit_rc(out_exit_rc, 0);
        return DSD_BOOTSTRAP_EXIT;
    }

    // --validate-config: validate config file and exit
    if (validate_config_cli) {
        const char* vpath = validate_path_cli;
        if (!vpath || !*vpath) {
            // Use default or explicit config path
            vpath = bootstrap_default_or_env_config_path(config_path_cli, config_env);
        }
        if (!vpath || !*vpath) {
            fprintf(stderr, "No config file path specified or found.\n");
            bootstrap_set_exit_rc(out_exit_rc, 1);
            return DSD_BOOTSTRAP_ERROR;
        }

        dsdcfg_diagnostics_t diags;
        int rc = dsd_user_config_validate(vpath, &diags);

        if (diags.count > 0) {
            dsdcfg_diags_print(&diags, stderr, vpath);
        } else {
            fprintf(stderr, "%s: OK\n", vpath);
        }

        int exit_code = 0;
        if (rc != 0 || diags.error_count > 0) {
            exit_code = 1;
        } else if (strict_config_cli && diags.warning_count > 0) {
            exit_code = 2;
        }

        dsd_user_config_diags_free(&diags);
        bootstrap_set_exit_rc(out_exit_rc, exit_code);
        return DSD_BOOTSTRAP_EXIT;
    }

    // --list-profiles: list available profiles and exit
    if (list_profiles_cli) {
        const char* lpath = bootstrap_default_or_env_config_path(config_path_cli, config_env);
        if (!lpath || !*lpath) {
            fprintf(stderr, "No config file path specified or found.\n");
            bootstrap_set_exit_rc(out_exit_rc, 1);
            return DSD_BOOTSTRAP_EXIT;
        }

        const char* names[32];
        char names_buf[1024];
        int count = dsd_user_config_list_profiles(lpath, names, names_buf, sizeof names_buf, 32);

        if (count < 0) {
            fprintf(stderr, "Failed to read config file: %s\n", lpath);
            bootstrap_set_exit_rc(out_exit_rc, 1);
            return DSD_BOOTSTRAP_EXIT;
        }

        if (count == 0) {
            printf("No profiles found in %s\n", lpath);
        } else {
            printf("Profiles in %s:\n", lpath);
            for (int i = 0; i < count; i++) {
                printf("  %s\n", names[i]);
            }
        }
        bootstrap_set_exit_rc(out_exit_rc, 0);
        return DSD_BOOTSTRAP_EXIT;
    }

    // Print banner only if not a one-shot action
    LOG_NOTICE("------------------------------------------------------------------------------\n");
    LOG_NOTICE("| Digital Speech Decoder: DSD-neo %s (%s) \n", GIT_TAG, GIT_HASH);
    LOG_NOTICE("------------------------------------------------------------------------------\n");

    const char* versionstr = mbe_versionString();
    LOG_NOTICE("MBElib-neo Version: %s\n", versionstr);

#ifdef USE_CODEC2
    LOG_NOTICE("CODEC2 Support Enabled\n");
#endif

    // If user requested it explicitly, or if there are no CLI args and no
    // user config, offer interactive bootstrap. The CLI flag overrides
    // any env-based skip (DSD_NEO_NO_BOOTSTRAP).
    if (force_bootstrap_cli || (argc <= 1 && !user_cfg_loaded)) {
        if (force_bootstrap_cli) {
            dsd_unsetenv("DSD_NEO_NO_BOOTSTRAP");
            dsd_neo_config_init(opts);
        }
        dsd_bootstrap_interactive(opts, state);
    }

    bootstrap_set_exit_rc(out_exit_rc, 0);
    return DSD_BOOTSTRAP_CONTINUE;
}
