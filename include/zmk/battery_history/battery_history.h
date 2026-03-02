/*
 * Copyright (c) Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief A single battery history entry
 */
struct __attribute__((packed)) zmk_battery_history_entry {
    uint16_t timestamp;    // Seconds since boot (resets on each restart)
    uint8_t battery_level; // Battery percentage (0-100)
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

/**
 * @brief Trigger sending battery history entries as events
 *
 * This function is called on peripheral devices when they receive
 * a request to send battery history. It triggers events for each
 * entry in the local buffer.
 *
 * @return 0 on success, negative error code on failure
 */
int zmk_battery_history_trigger_send(void);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_HISTORY_STUDIO_RPC)
/**
 * @brief Send a battery history notification for a single entry
 *
 * This function sends an RPC notification with a battery history entry.
 * Used for streaming battery history data to the host UI.
 *
 * @param source_id Source ID (0 for central, 1+ for peripherals)
 * @param entry Battery history entry to send
 * @param entry_index Index of this entry in the batch (0-based)
 * @param total_entries Total number of entries in the batch
 * @param is_last True if this is the last entry in the batch
 * @return 0 on success, negative error code on failure
 */
int zmk_battery_history_send_notification(uint8_t source_id,
                                          const struct zmk_battery_history_entry *entry,
                                          uint8_t entry_index, uint8_t total_entries, bool is_last);
#endif
