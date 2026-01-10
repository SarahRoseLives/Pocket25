// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Path expansion for configuration values.
 *
 * Supports shell-like expansion:
 *   ~       -> $HOME or platform home directory
 *   $VAR    -> environment variable VAR
 *   ${VAR}  -> environment variable VAR (braced form)
 *
 * Missing variables expand to empty string (no error).
 */

#include <dsd-neo/runtime/config.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/**
 * @brief Get the user's home directory.
 * @return Pointer to static buffer with home path, or NULL if unavailable.
 */
static const char*
get_home_dir(void) {
    static char home_buf[1024];
    static int cached = 0;

    if (cached) {
        return home_buf[0] ? home_buf : NULL;
    }

    home_buf[0] = '\0';
    cached = 1;

#if defined(_WIN32)
    /* Try USERPROFILE first, then HOMEDRIVE+HOMEPATH */
    const char* userprofile = getenv("USERPROFILE");
    if (userprofile && *userprofile) {
        snprintf(home_buf, sizeof(home_buf), "%s", userprofile);
        home_buf[sizeof(home_buf) - 1] = '\0';
        return home_buf;
    }

    const char* homedrive = getenv("HOMEDRIVE");
    const char* homepath = getenv("HOMEPATH");
    if (homedrive && homepath) {
        snprintf(home_buf, sizeof(home_buf), "%s%s", homedrive, homepath);
        home_buf[sizeof(home_buf) - 1] = '\0';
        return home_buf;
    }
#else
    /* Try $HOME first */
    const char* home = getenv("HOME");
    if (home && *home) {
        snprintf(home_buf, sizeof(home_buf), "%s", home);
        home_buf[sizeof(home_buf) - 1] = '\0';
        return home_buf;
    }

    /* Fall back to passwd entry */
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir && *pw->pw_dir) {
        snprintf(home_buf, sizeof(home_buf), "%s", pw->pw_dir);
        home_buf[sizeof(home_buf) - 1] = '\0';
        return home_buf;
    }
#endif

    return NULL;
}

/**
 * @brief Check if character is valid in an environment variable name.
 */
static int
is_var_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

int
dsd_config_expand_path(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return -1;
    }

    const char* src = input;
    char* dst = output;
    char* dst_end = output + output_size - 1; /* leave room for NUL */

    while (*src && dst < dst_end) {
        /* Handle tilde expansion at start or after separator */
        if (*src == '~') {
            /* Only expand ~ at start of path or after path separator */
            if (src == input || src[-1] == '/' || src[-1] == '\\') {
                /* Check for ~/... or ~\... pattern */
                char next = src[1];
                if (next == '\0' || next == '/' || next == '\\') {
                    const char* home = get_home_dir();
                    if (home) {
                        size_t home_len = strlen(home);
                        if (dst + home_len <= dst_end) {
                            memcpy(dst, home, home_len);
                            dst += home_len;
                        } else {
                            /* truncation */
                            output[output_size - 1] = '\0';
                            return -1;
                        }
                    }
                    src++; /* skip ~ */
                    continue;
                }
            }
            /* Not a tilde expansion context, copy literally */
            *dst++ = *src++;
            continue;
        }

        /* Handle environment variable expansion */
        if (*src == '$') {
            const char* var_start = NULL;
            const char* var_end = NULL;
            int braced = 0;

            if (src[1] == '{') {
                /* ${VAR} form */
                braced = 1;
                var_start = src + 2;
                var_end = strchr(var_start, '}');
                if (!var_end) {
                    /* Malformed ${..., copy $ literally */
                    *dst++ = *src++;
                    continue;
                }
            } else if (is_var_char(src[1])) {
                /* $VAR form */
                var_start = src + 1;
                var_end = var_start;
                while (*var_end && is_var_char(*var_end)) {
                    var_end++;
                }
            } else {
                /* $ followed by non-variable character, copy literally */
                *dst++ = *src++;
                continue;
            }

            /* Extract variable name */
            size_t var_len = (size_t)(var_end - var_start);
            if (var_len > 0 && var_len < 256) {
                char var_name[256];
                memcpy(var_name, var_start, var_len);
                var_name[var_len] = '\0';

                const char* var_val = getenv(var_name);
                if (var_val) {
                    size_t val_len = strlen(var_val);
                    if (dst + val_len <= dst_end) {
                        memcpy(dst, var_val, val_len);
                        dst += val_len;
                    } else {
                        /* truncation */
                        output[output_size - 1] = '\0';
                        return -1;
                    }
                }
                /* If var not found, expand to empty (no error) */
            }

            /* Advance past variable reference */
            if (braced) {
                src = var_end + 1; /* skip closing } */
            } else {
                src = var_end;
            }
            continue;
        }

        /* Regular character, copy as-is */
        *dst++ = *src++;
    }

    *dst = '\0';

    /* Check for truncation */
    if (*src != '\0') {
        return -1;
    }

    return 0;
}
