// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Async prompt, chooser, and help overlay helpers for menu subsystem.
 *
 * This header is internal to src/ui/terminal/ and should NOT be installed.
 */
#pragma once

#include <stddef.h>

// ---- String/Int/Double Prompt API ----

/**
 * @brief Open a string prompt asynchronously.
 *
 * The callback is invoked with the entered text (or NULL on cancel/empty).
 *
 * @param title    Title shown in prompt window.
 * @param prefill  Optional prefilled text (may be NULL).
 * @param cap      Max buffer capacity for input.
 * @param on_done  Callback invoked when user completes (text may be NULL).
 * @param user     User context passed to callback.
 */
void ui_prompt_open_string_async(const char* title, const char* prefill, size_t cap,
                                 void (*on_done)(void* user, const char* text), void* user);

/**
 * @brief Open an integer prompt asynchronously.
 *
 * @param title    Title shown in prompt window.
 * @param initial  Initial integer value shown.
 * @param cb       Callback invoked with (user, ok, value).
 * @param user     User context passed to callback.
 */
void ui_prompt_open_int_async(const char* title, int initial, void (*cb)(void* user, int ok, int value), void* user);

/**
 * @brief Open a double prompt asynchronously.
 *
 * @param title    Title shown in prompt window.
 * @param initial  Initial double value shown.
 * @param cb       Callback invoked with (user, ok, value).
 * @param user     User context passed to callback.
 */
void ui_prompt_open_double_async(const char* title, double initial, void (*cb)(void* user, int ok, double value),
                                 void* user);

/**
 * @brief Close all active prompts (forcefully).
 */
void ui_prompt_close_all(void);

/**
 * @brief Check if a prompt is currently active.
 * @return 1 if active, 0 otherwise.
 */
int ui_prompt_active(void);

/**
 * @brief Handle a key event for the active prompt.
 * @param ch  Key code from ncurses.
 * @return 1 if handled, 0 otherwise.
 */
int ui_prompt_handle_key(int ch);

/**
 * @brief Render the active prompt overlay.
 */
void ui_prompt_render(void);

// ---- Help Overlay API ----

/**
 * @brief Open the help overlay.
 *
 * @param help  Help text to display.
 */
void ui_help_open(const char* help);

/**
 * @brief Close the help overlay.
 */
void ui_help_close(void);

/**
 * @brief Check if help overlay is currently active.
 * @return 1 if active, 0 otherwise.
 */
int ui_help_active(void);

/**
 * @brief Handle a key event for the help overlay.
 * @param ch  Key code from ncurses.
 * @return 1 if handled, 0 otherwise.
 */
int ui_help_handle_key(int ch);

/**
 * @brief Render the help overlay.
 */
void ui_help_render(void);

// ---- Chooser Overlay API ----

/**
 * @brief Start a chooser overlay with a list of items.
 *
 * @param title    Title shown in chooser window.
 * @param items    Array of string labels.
 * @param count    Number of items in the array.
 * @param on_done  Callback invoked with selected index (or -1 on cancel).
 * @param user     User context passed to callback.
 */
void ui_chooser_start(const char* title, const char* const* items, int count, void (*on_done)(void*, int), void* user);

/**
 * @brief Close the chooser overlay.
 */
void ui_chooser_close(void);

/**
 * @brief Check if chooser overlay is currently active.
 * @return 1 if active, 0 otherwise.
 */
int ui_chooser_active(void);

/**
 * @brief Handle a key event for the chooser overlay.
 * @param ch  Key code from ncurses.
 * @return 1 if handled, 0 otherwise.
 */
int ui_chooser_handle_key(int ch);

/**
 * @brief Render the chooser overlay.
 */
void ui_chooser_render(void);
