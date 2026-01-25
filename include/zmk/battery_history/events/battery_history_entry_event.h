/*
 * Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>
#include <zmk/battery_history/battery_history.h>

/**
 * @brief Event for battery history entry
 *
 * This event is raised when a battery history entry needs to be sent.
 * On peripherals, this event is raised for each entry and transported to central.
 * On central, receiving this event from peripheral triggers RPC notification.
 */
struct zmk_battery_history_entry_event {
    uint8_t source;                            // Source ID: 0 for central, 1+ for peripherals
    struct zmk_battery_history_entry entry;    // Battery history entry
    uint8_t entry_index;                       // Index of this entry in the batch (0-based)
    uint8_t total_entries;                     // Total number of entries in the batch
    bool is_last;                              // True if this is the last entry
};

ZMK_EVENT_DECLARE(zmk_battery_history_entry_event);
