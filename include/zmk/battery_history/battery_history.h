/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief A single battery history entry
 */
struct zmk_battery_history_entry {
    uint16_t timestamp;      // Seconds since boot (resets on restart, max ~18 hours)
    uint8_t battery_level;   // Battery percentage (0-100)
};

/**
 * @brief Get the number of stored battery history entries
 * @return Number of entries currently stored
 */
int zmk_battery_history_get_count(void);

/**
 * @brief Get a battery history entry by index
 * @param index Index of the entry (0 = oldest)
 * @param entry Pointer to store the entry
 * @return 0 on success, negative error code on failure
 */
int zmk_battery_history_get_entry(int index, struct zmk_battery_history_entry *entry);

/**
 * @brief Get the current battery level
 * @return Current battery percentage (0-100), or negative error code
 */
int zmk_battery_history_get_current_level(void);

/**
 * @brief Clear all battery history entries
 * @return Number of entries cleared
 */
int zmk_battery_history_clear(void);

/**
 * @brief Get the recording interval in minutes
 * @return Recording interval in minutes
 */
int zmk_battery_history_get_interval(void);

/**
 * @brief Get the maximum number of entries that can be stored
 * @return Maximum number of entries
 */
int zmk_battery_history_get_max_entries(void);

/**
 * @brief Force save current entries to persistent storage
 * @return 0 on success, negative error code on failure
 */
int zmk_battery_history_save(void);
