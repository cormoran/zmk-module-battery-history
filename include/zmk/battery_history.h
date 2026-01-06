/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/**
 * Battery history entry structure
 */
struct battery_history_entry {
    uint32_t timestamp;           // Unix timestamp
    uint8_t battery_percentage;   // 0-100
};

/**
 * Get the number of battery history entries stored
 * @return Number of entries
 */
int battery_history_get_count(void);

/**
 * Get battery history entries
 * @param entries Array to store entries
 * @param max_entries Maximum number of entries to retrieve
 * @return Number of entries retrieved, or negative error code
 */
int battery_history_get_entries(struct battery_history_entry *entries, int max_entries);

/**
 * Clear all battery history
 * @return 0 on success, negative error code on failure
 */
int battery_history_clear(void);

/**
 * Get current battery percentage
 * @return Battery percentage 0-100
 */
uint8_t battery_history_get_current_battery(void);
