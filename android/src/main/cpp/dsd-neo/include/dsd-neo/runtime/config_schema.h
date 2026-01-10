// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Configuration schema types and accessor API.
 *
 * Provides schema definitions for user configuration keys, enabling
 * validation, template generation, and documentation.
 */

#ifndef DSD_NEO_RUNTIME_CONFIG_SCHEMA_H
#define DSD_NEO_RUNTIME_CONFIG_SCHEMA_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration value types.
 */
typedef enum {
    DSDCFG_TYPE_STRING = 0, /**< Arbitrary string value */
    DSDCFG_TYPE_INT,        /**< Integer value with optional min/max */
    DSDCFG_TYPE_BOOL,       /**< Boolean (true/false, yes/no, 1/0) */
    DSDCFG_TYPE_ENUM,       /**< String from a set of allowed values */
    DSDCFG_TYPE_PATH,       /**< File path (supports ~ and $VAR expansion) */
    DSDCFG_TYPE_FREQ        /**< Frequency string (supports K/M/G suffix) */
} dsdcfg_type_t;

/**
 * @brief Schema entry for a configuration key.
 */
typedef struct {
    const char* section;     /**< Section name (e.g., "input", "output") */
    const char* key;         /**< Key name (e.g., "source", "rtl_device") */
    const char* description; /**< Human-readable description */
    const char* default_str; /**< Default value as string (for template) */
    const char* allowed;     /**< Pipe-separated allowed values (for ENUM) */
    dsdcfg_type_t type;      /**< Value type */
    int min_val;             /**< Minimum value (for INT type) */
    int max_val;             /**< Maximum value (for INT type, 0 = no max) */
    int deprecated;          /**< Non-zero if key is deprecated */
} dsdcfg_schema_entry_t;

/**
 * @brief Diagnostic severity levels.
 */
typedef enum {
    DSDCFG_DIAG_INFO = 0, /**< Informational (e.g., deprecated key usage) */
    DSDCFG_DIAG_WARNING,  /**< Warning (e.g., unknown key, out of range) */
    DSDCFG_DIAG_ERROR     /**< Error (e.g., type mismatch, parse failure) */
} dsdcfg_diag_level_t;

/**
 * @brief Single diagnostic message from config validation.
 */
typedef struct {
    dsdcfg_diag_level_t level; /**< Severity level */
    int line_number;           /**< Line number in config file (0 if N/A) */
    char section[64];          /**< Section name where issue occurred */
    char key[64];              /**< Key name where issue occurred */
    char message[256];         /**< Human-readable diagnostic message */
} dsdcfg_diagnostic_t;

/**
 * @brief Collection of diagnostic messages from validation.
 */
typedef struct {
    dsdcfg_diagnostic_t* items; /**< Array of diagnostics (heap-allocated) */
    int count;                  /**< Number of diagnostics */
    int capacity;               /**< Allocated capacity */
    int error_count;            /**< Number of error-level diagnostics */
    int warning_count;          /**< Number of warning-level diagnostics */
} dsdcfg_diagnostics_t;

/**
 * @brief Get the number of schema entries.
 * @return Total number of defined configuration keys.
 */
int dsdcfg_schema_count(void);

/**
 * @brief Get a schema entry by index.
 * @param index Index into schema array (0 to count-1).
 * @return Pointer to schema entry, or NULL if index out of range.
 */
const dsdcfg_schema_entry_t* dsdcfg_schema_get(int index);

/**
 * @brief Find a schema entry by section and key name.
 * @param section Section name (case-insensitive).
 * @param key Key name (case-insensitive).
 * @return Pointer to schema entry, or NULL if not found.
 */
const dsdcfg_schema_entry_t* dsdcfg_schema_find(const char* section, const char* key);

/**
 * @brief Get human-readable description for a config key.
 * @param section Section name.
 * @param key Key name.
 * @return Description string or NULL if key unknown.
 */
const char* dsd_config_key_description(const char* section, const char* key);

/**
 * @brief Check if a config key is deprecated.
 * @param section Section name.
 * @param key Key name.
 * @return 1 if deprecated, 0 otherwise (including unknown keys).
 */
int dsd_config_key_is_deprecated(const char* section, const char* key);

/**
 * @brief Get the type name as a string for display.
 * @param type Schema type enum value.
 * @return String name (e.g., "string", "int", "bool").
 */
const char* dsdcfg_type_name(dsdcfg_type_t type);

/**
 * @brief Initialize a diagnostics collection.
 * @param diags Diagnostics structure to initialize.
 */
void dsdcfg_diags_init(dsdcfg_diagnostics_t* diags);

/**
 * @brief Add a diagnostic message to the collection.
 * @param diags Diagnostics collection.
 * @param level Severity level.
 * @param line Line number (0 if not applicable).
 * @param section Section name (may be empty).
 * @param key Key name (may be empty).
 * @param message Diagnostic message.
 */
void dsdcfg_diags_add(dsdcfg_diagnostics_t* diags, dsdcfg_diag_level_t level, int line, const char* section,
                      const char* key, const char* message);

/**
 * @brief Free resources in a diagnostics collection.
 * @param diags Diagnostics structure to free.
 */
void dsdcfg_diags_free(dsdcfg_diagnostics_t* diags);

/**
 * @brief Print diagnostics to a stream.
 * @param diags Diagnostics collection.
 * @param stream Output stream (e.g., stderr).
 * @param path Config file path for context (may be NULL).
 */
void dsdcfg_diags_print(const dsdcfg_diagnostics_t* diags, FILE* stream, const char* path);

/**
 * @brief Get list of unique section names in schema.
 * @param sections Output array of section name pointers (caller provides).
 * @param max_sections Maximum number of sections to return.
 * @return Number of unique sections found.
 */
int dsdcfg_schema_sections(const char** sections, int max_sections);

#ifdef __cplusplus
}
#endif

#endif /* DSD_NEO_RUNTIME_CONFIG_SCHEMA_H */
