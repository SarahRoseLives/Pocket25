// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Unit tests for config template generation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/runtime/config.h>

static char*
read_tmpfile_contents(FILE* tmp, size_t* out_size) {
    if (fseek(tmp, 0, SEEK_END) != 0) {
        return NULL;
    }
    long size_long = ftell(tmp);
    if (size_long < 0) {
        return NULL;
    }
    if (fseek(tmp, 0, SEEK_SET) != 0) {
        return NULL;
    }

    size_t size = (size_t)size_long;
    char* content = (char*)malloc(size + 1);
    if (!content) {
        return NULL;
    }

    size_t read = fread(content, 1, size, tmp);
    if (read != size && ferror(tmp)) {
        free(content);
        return NULL;
    }

    content[read] = '\0';
    if (out_size) {
        *out_size = read;
    }
    return content;
}

static int
test_template_generates_output(void) {
    FILE* tmp = tmpfile();
    if (!tmp) {
        fprintf(stderr, "FAIL: tmpfile() failed\n");
        return 1;
    }

    dsd_user_config_render_template(tmp);

    // Check that something was written
    if (fseek(tmp, 0, SEEK_END) != 0) {
        fprintf(stderr, "FAIL: fseek() failed\n");
        fclose(tmp);
        return 1;
    }
    long size = ftell(tmp);
    if (size <= 0) {
        fprintf(stderr, "FAIL: template output is empty\n");
        fclose(tmp);
        return 1;
    }

    fclose(tmp);
    return 0;
}

static int
test_template_contains_sections(void) {
    FILE* tmp = tmpfile();
    if (!tmp) {
        fprintf(stderr, "FAIL: tmpfile() failed\n");
        return 1;
    }

    dsd_user_config_render_template(tmp);

    // Read back the content
    size_t content_size = 0;
    char* content = read_tmpfile_contents(tmp, &content_size);
    if (!content) {
        fclose(tmp);
        return 1;
    }
    fclose(tmp);

    int rc = 0;

    // Check for required sections
    if (!strstr(content, "[input]")) {
        fprintf(stderr, "FAIL: template missing [input] section\n");
        rc = 1;
    }
    if (!strstr(content, "[output]")) {
        fprintf(stderr, "FAIL: template missing [output] section\n");
        rc = 1;
    }
    if (!strstr(content, "[mode]")) {
        fprintf(stderr, "FAIL: template missing [mode] section\n");
        rc = 1;
    }
    if (!strstr(content, "[trunking]")) {
        fprintf(stderr, "FAIL: template missing [trunking] section\n");
        rc = 1;
    }

    free(content);
    return rc;
}

static int
test_template_contains_keys(void) {
    FILE* tmp = tmpfile();
    if (!tmp) {
        fprintf(stderr, "FAIL: tmpfile() failed\n");
        return 1;
    }

    dsd_user_config_render_template(tmp);

    // Read back the content
    size_t content_size = 0;
    char* content = read_tmpfile_contents(tmp, &content_size);
    if (!content) {
        fclose(tmp);
        return 1;
    }
    fclose(tmp);

    int rc = 0;

    // Check for some key configuration keys (commented out in template)
    if (!strstr(content, "# source")) {
        fprintf(stderr, "FAIL: template missing commented source key\n");
        rc = 1;
    }
    if (!strstr(content, "# backend")) {
        fprintf(stderr, "FAIL: template missing commented backend key\n");
        rc = 1;
    }
    if (!strstr(content, "# decode")) {
        fprintf(stderr, "FAIL: template missing commented decode key\n");
        rc = 1;
    }
    if (!strstr(content, "# enabled")) {
        fprintf(stderr, "FAIL: template missing commented enabled key\n");
        rc = 1;
    }

    free(content);
    return rc;
}

static int
test_template_contains_descriptions(void) {
    FILE* tmp = tmpfile();
    if (!tmp) {
        fprintf(stderr, "FAIL: tmpfile() failed\n");
        return 1;
    }

    dsd_user_config_render_template(tmp);

    // Read back the content
    size_t content_size = 0;
    char* content = read_tmpfile_contents(tmp, &content_size);
    if (!content) {
        fclose(tmp);
        return 1;
    }
    fclose(tmp);

    int rc = 0;

    // Check for descriptions (should have comment lines with descriptions)
    // These should be present based on the schema
    if (!strstr(content, "Input source type")) {
        fprintf(stderr, "FAIL: template missing 'Input source type' description\n");
        rc = 1;
    }

    free(content);
    return rc;
}

static int
test_template_is_valid_ini(void) {
    FILE* tmp = tmpfile();
    if (!tmp) {
        fprintf(stderr, "FAIL: tmpfile() failed\n");
        return 1;
    }

    dsd_user_config_render_template(tmp);

    // Read back the content
    size_t content_size = 0;
    char* content = read_tmpfile_contents(tmp, &content_size);
    if (!content) {
        fclose(tmp);
        return 1;
    }
    fclose(tmp);

    int rc = 0;

    // Basic INI validity checks:
    // 1. Lines starting with # are comments
    // 2. Lines starting with [ are section headers and must end with ]
    // 3. Non-empty, non-comment lines should have = or be section headers

    char* line = strtok(content, "\n");
    int line_num = 0;
    while (line) {
        line_num++;

        // Skip empty lines
        if (line[0] == '\0' || (line[0] == '\r' && line[1] == '\0')) {
            line = strtok(NULL, "\n");
            continue;
        }

        // Skip comments
        if (line[0] == '#') {
            line = strtok(NULL, "\n");
            continue;
        }

        // Section headers
        if (line[0] == '[') {
            if (!strchr(line, ']')) {
                fprintf(stderr, "FAIL: line %d: malformed section header: %s\n", line_num, line);
                rc = 1;
            }
            line = strtok(NULL, "\n");
            continue;
        }

        // Key = value lines
        if (!strchr(line, '=')) {
            fprintf(stderr, "FAIL: line %d: missing '=' in key-value: %s\n", line_num, line);
            rc = 1;
        }

        line = strtok(NULL, "\n");
    }

    free(content);
    return rc;
}

int
main(void) {
    int rc = 0;

    rc |= test_template_generates_output();
    rc |= test_template_contains_sections();
    rc |= test_template_contains_keys();
    rc |= test_template_contains_descriptions();
    rc |= test_template_is_valid_ini();

    if (rc == 0) {
        printf("All config_template tests passed\n");
    }

    return rc;
}
