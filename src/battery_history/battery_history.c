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
#include <zmk/events/activity_state_changed.h>
#include <zmk/battery_history/battery_history.h>

#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(zmk_battery_history, CONFIG_ZMK_LOG_LEVEL);

#define MAX_ENTRIES CONFIG_ZMK_BATTERY_HISTORY_MAX_ENTRIES
#define RECORDING_INTERVAL_MS (CONFIG_ZMK_BATTERY_HISTORY_INTERVAL_MINUTES * 60 * 1000)
#define SAVE_THRESHOLD CONFIG_ZMK_BATTERY_HISTORY_SAVE_THRESHOLD

// Minimum time interval (in seconds) before recording same battery level
#define MIN_SAME_LEVEL_INTERVAL_SEC (CONFIG_ZMK_BATTERY_HISTORY_INTERVAL_MINUTES * 60 * 4)

// Circular buffer for battery history
static struct zmk_battery_history_entry history_buffer[MAX_ENTRIES];
static int history_head = 0;    // Index of the oldest entry
static int history_count = 0;   // Number of valid entries
static int unsaved_count = 0;   // Number of entries not yet saved to flash
static int last_saved_count = 0; // Count at last save (for incremental save)

// Work item for periodic recording
static void battery_history_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_history_work, battery_history_work_handler);

// Current battery level cache
static uint8_t current_battery_level = 0;

// Track if this is the first record after boot
static bool first_record_after_boot = true;

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
 * Get the last recorded entry (if any)
 */
static bool get_last_entry(struct zmk_battery_history_entry *entry) {
    if (history_count == 0) {
        return false;
    }
    int last_idx = get_buffer_index(history_count - 1);
    *entry = history_buffer[last_idx];
    return true;
}

/**
 * Add a new entry to the history buffer
 */
static void add_history_entry(uint16_t timestamp, uint8_t level) {
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
 * Save history to persistent storage (incremental save)
 * Only saves changed data to reduce flash writes
 */
static int save_history(void) {
    if (unsaved_count == 0) {
        return 0;
    }
    
    LOG_INF("Saving battery history to flash (count=%d, unsaved=%d)", history_count, unsaved_count);
    
    // Always save head and count (small data)
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
    
    // Save individual entries that have changed
    // For simplicity and reliability, save entire buffer when structure changes significantly
    // But use incremental naming to allow future optimization
    rc = settings_save_one("battery_history/data", history_buffer, sizeof(history_buffer));
    if (rc < 0) {
        LOG_ERR("Failed to save history data: %d", rc);
        return rc;
    }
    
    last_saved_count = history_count;
    unsaved_count = 0;
    LOG_INF("Battery history saved successfully");
    return 0;
}

/**
 * Check if we should record based on battery level change
 * Returns true if we should add a new entry
 */
static bool should_record_entry(uint16_t timestamp, uint8_t level) {
    // Always record the first entry after boot
    if (first_record_after_boot) {
        first_record_after_boot = false;
        return true;
    }
    
    // Get the last recorded entry
    struct zmk_battery_history_entry last_entry;
    if (!get_last_entry(&last_entry)) {
        // No previous entry, record this one
        return true;
    }
    
    // Always record if battery level changed
    if (last_entry.battery_level != level) {
        return true;
    }
    
    // If level is the same, only record if enough time has passed
    // This reduces redundant entries when battery is stable
    uint16_t time_diff = timestamp - last_entry.timestamp;
    if (time_diff >= MIN_SAME_LEVEL_INTERVAL_SEC) {
        return true;
    }
    
    LOG_DBG("Skipping record: level unchanged (%d%%), time_diff=%u < threshold=%d",
            level, time_diff, MIN_SAME_LEVEL_INTERVAL_SEC);
    return false;
}

/**
 * Record current battery level to history
 */
static void record_battery_level(void) {
    // Get current timestamp (seconds since boot)
    uint16_t timestamp = (uint16_t)(k_uptime_get() / 1000);
    
    // Get current battery level
    int level = zmk_battery_state_of_charge();
    if (level < 0) {
        LOG_WRN("Failed to get battery level: %d", level);
        return;
    }
    
    current_battery_level = (uint8_t)level;
    
    // Check if we should add this entry
    if (!should_record_entry(timestamp, current_battery_level)) {
        return;
    }
    
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
        int rc = read_cb(cb_arg, &history_count, sizeof(history_count));
        if (rc >= 0) {
            last_saved_count = history_count;
        }
        return rc;
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
    const struct zmk_battery_state_changed *bev = as_zmk_battery_state_changed(eh);
    if (bev) {
        current_battery_level = bev->state_of_charge;
        LOG_DBG("Battery level updated: %d%%", current_battery_level);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(battery_history, battery_history_event_listener);
ZMK_SUBSCRIPTION(battery_history, zmk_battery_state_changed);

/**
 * Handle activity state changes - save before sleep
 */
static int battery_history_activity_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *aev = as_zmk_activity_state_changed(eh);
    if (aev && aev->state == ZMK_ACTIVITY_SLEEP) {
        LOG_INF("Device entering sleep, saving battery history");
        // Record current level before sleep
        record_battery_level();
        // Force save any unsaved data
        if (unsaved_count > 0) {
            save_history();
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(battery_history_activity, battery_history_activity_listener);
ZMK_SUBSCRIPTION(battery_history_activity, zmk_activity_state_changed);

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
    last_saved_count = 0;
    first_record_after_boot = true;
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
