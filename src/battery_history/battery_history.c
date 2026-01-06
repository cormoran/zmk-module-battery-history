/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/battery_history/battery_history.h>

#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(zmk_battery_history, CONFIG_ZMK_LOG_LEVEL);

#define MAX_ENTRIES CONFIG_ZMK_BATTERY_HISTORY_MAX_ENTRIES
#define RECORDING_INTERVAL_MS (CONFIG_ZMK_BATTERY_HISTORY_INTERVAL_MINUTES * 60 * 1000)
#define SAVE_THRESHOLD CONFIG_ZMK_BATTERY_HISTORY_SAVE_THRESHOLD

// Circular buffer for battery history
static struct zmk_battery_history_entry history_buffer[MAX_ENTRIES];
static int history_head = 0;    // Index of the oldest entry
static int history_count = 0;   // Number of valid entries
static int unsaved_count = 0;   // Number of entries not yet saved to flash

// Work item for periodic recording
static void battery_history_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_history_work, battery_history_work_handler);

// Current battery level cache
static uint8_t current_battery_level = 0;

// Settings handling
static int battery_history_settings_set(const char *name, size_t len,
                                        settings_read_cb read_cb, void *cb_arg);
static int battery_history_settings_commit(void);

SETTINGS_STATIC_HANDLER_DEFINE(battery_history, "battery_history",
                               NULL, battery_history_settings_set, 
                               battery_history_settings_commit, NULL);

/**
 * Get the absolute index in the circular buffer
 */
static int get_buffer_index(int logical_index) {
    return (history_head + logical_index) % MAX_ENTRIES;
}

/**
 * Add a new entry to the history buffer
 */
static void add_history_entry(uint32_t timestamp, uint8_t level) {
    int write_idx;
    
    if (history_count < MAX_ENTRIES) {
        // Buffer not full, append at end
        write_idx = (history_head + history_count) % MAX_ENTRIES;
        history_count++;
    } else {
        // Buffer full, overwrite oldest entry
        write_idx = history_head;
        history_head = (history_head + 1) % MAX_ENTRIES;
    }
    
    history_buffer[write_idx].timestamp = timestamp;
    history_buffer[write_idx].battery_level = level;
    unsaved_count++;
    
    LOG_DBG("Added battery history entry: timestamp=%u, level=%u (total=%d, unsaved=%d)",
            timestamp, level, history_count, unsaved_count);
}

/**
 * Save history to persistent storage
 */
static int save_history(void) {
    if (unsaved_count == 0) {
        return 0;
    }
    
    LOG_INF("Saving battery history to flash (count=%d)", history_count);
    
    // Save the circular buffer state
    int rc = settings_save_one("battery_history/head", &history_head, sizeof(history_head));
    if (rc < 0) {
        LOG_ERR("Failed to save history head: %d", rc);
        return rc;
    }
    
    rc = settings_save_one("battery_history/count", &history_count, sizeof(history_count));
    if (rc < 0) {
        LOG_ERR("Failed to save history count: %d", rc);
        return rc;
    }
    
    // Save the buffer data
    rc = settings_save_one("battery_history/data", history_buffer, sizeof(history_buffer));
    if (rc < 0) {
        LOG_ERR("Failed to save history data: %d", rc);
        return rc;
    }
    
    unsaved_count = 0;
    LOG_INF("Battery history saved successfully");
    return 0;
}

/**
 * Record current battery level to history
 */
static void record_battery_level(void) {
    // Get current timestamp (seconds since boot, or RTC if available)
    uint32_t timestamp = (uint32_t)(k_uptime_get() / 1000);
    
    // Get current battery level
    int level = zmk_battery_state_of_charge();
    if (level < 0) {
        LOG_WRN("Failed to get battery level: %d", level);
        return;
    }
    
    current_battery_level = (uint8_t)level;
    add_history_entry(timestamp, current_battery_level);
    
    // Save to flash if threshold reached
    if (unsaved_count >= SAVE_THRESHOLD) {
        save_history();
    }
}

/**
 * Work handler for periodic battery recording
 */
static void battery_history_work_handler(struct k_work *work) {
    record_battery_level();
    
    // Schedule next recording
    k_work_schedule(&battery_history_work, K_MSEC(RECORDING_INTERVAL_MS));
}

/**
 * Settings load handler
 */
static int battery_history_settings_set(const char *name, size_t len,
                                        settings_read_cb read_cb, void *cb_arg) {
    if (!strcmp(name, "head")) {
        if (len != sizeof(history_head)) {
            return -EINVAL;
        }
        return read_cb(cb_arg, &history_head, sizeof(history_head));
    }
    
    if (!strcmp(name, "count")) {
        if (len != sizeof(history_count)) {
            return -EINVAL;
        }
        return read_cb(cb_arg, &history_count, sizeof(history_count));
    }
    
    if (!strcmp(name, "data")) {
        if (len != sizeof(history_buffer)) {
            return -EINVAL;
        }
        return read_cb(cb_arg, history_buffer, sizeof(history_buffer));
    }
    
    return -ENOENT;
}

/**
 * Settings commit handler - called after all settings are loaded
 */
static int battery_history_settings_commit(void) {
    LOG_INF("Battery history loaded: count=%d, head=%d", history_count, history_head);
    return 0;
}

/**
 * Handle battery state change events
 */
static int battery_history_event_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (ev) {
        current_battery_level = ev->state_of_charge;
        LOG_DBG("Battery level updated: %d%%", current_battery_level);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(battery_history, battery_history_event_listener);
ZMK_SUBSCRIPTION(battery_history, zmk_battery_state_changed);

/**
 * Initialize battery history module
 */
static int battery_history_init(void) {
    LOG_INF("Initializing battery history module");
    LOG_INF("Max entries: %d, Recording interval: %d minutes, Save threshold: %d",
            MAX_ENTRIES, CONFIG_ZMK_BATTERY_HISTORY_INTERVAL_MINUTES, SAVE_THRESHOLD);
    
    // Record initial battery level
    int level = zmk_battery_state_of_charge();
    if (level >= 0) {
        current_battery_level = (uint8_t)level;
    }
    
    // Start periodic recording
    k_work_schedule(&battery_history_work, K_MSEC(RECORDING_INTERVAL_MS));
    
    return 0;
}

// Initialize after settings are loaded
SYS_INIT(battery_history_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* Public API implementation */

int zmk_battery_history_get_count(void) {
    return history_count;
}

int zmk_battery_history_get_entry(int index, struct zmk_battery_history_entry *entry) {
    if (index < 0 || index >= history_count || entry == NULL) {
        return -EINVAL;
    }
    
    int buffer_idx = get_buffer_index(index);
    *entry = history_buffer[buffer_idx];
    return 0;
}

int zmk_battery_history_get_current_level(void) {
    return current_battery_level;
}

int zmk_battery_history_clear(void) {
    int cleared = history_count;
    
    history_head = 0;
    history_count = 0;
    unsaved_count = 0;
    memset(history_buffer, 0, sizeof(history_buffer));
    
    // Save the cleared state
    save_history();
    
    LOG_INF("Battery history cleared: %d entries removed", cleared);
    return cleared;
}

int zmk_battery_history_get_interval(void) {
    return CONFIG_ZMK_BATTERY_HISTORY_INTERVAL_MINUTES;
}

int zmk_battery_history_get_max_entries(void) {
    return MAX_ENTRIES;
}

int zmk_battery_history_save(void) {
    return save_history();
}
