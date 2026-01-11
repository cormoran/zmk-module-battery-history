/*
 * Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zmk/battery.h>
#include <zmk/usb.h>
#include <zmk/battery_history/battery_history.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/events/split_peripheral_status_changed.h>
#endif
#endif

LOG_MODULE_REGISTER(zmk_battery_history, CONFIG_ZMK_LOG_LEVEL);

#define MAX_ENTRIES CONFIG_ZMK_BATTERY_HISTORY_MAX_ENTRIES
#define RECORDING_INTERVAL_MS (CONFIG_ZMK_BATTERY_HISTORY_INTERVAL_MINUTES * 60 * 1000)
#define SAVE_INTERVAL_SEC (CONFIG_ZMK_BATTERY_HISTORY_SAVE_INTERVAL_MINUTES * 60)
#define SAVE_LEVEL_THRESHOLD CONFIG_ZMK_BATTERY_HISTORY_SAVE_LEVEL_THRESHOLD

// Minimum time interval (in seconds) before recording same battery level
// We use 4x the recording interval to reduce redundant entries when battery is
// stable For example: with 5min interval, we skip same-level records unless 20
// minutes have passed
#define MIN_SAME_LEVEL_INTERVAL_SEC (CONFIG_ZMK_BATTERY_HISTORY_INTERVAL_MINUTES * 60 * 4)

// Define source count based on split configuration
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define SOURCE_COUNT (1 + ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT)
#else
#define SOURCE_COUNT 1
#endif

// Per-source battery history data
struct battery_source_history {
    struct zmk_battery_history_entry buffer[MAX_ENTRIES];
    int head;
    int count;
    int unsaved_count;
    int first_unsaved_idx;
    uint8_t last_saved_level;
    uint16_t last_saved_timestamp;
    uint8_t current_level;
    bool first_record_after_boot;
    bool head_changed_since_save;
};

static struct battery_source_history source_history[SOURCE_COUNT];

// Legacy compatibility - macros will reference central (source 0) data
#define history_buffer (source_history[0].buffer)
#define history_head (source_history[0].head)
#define history_count (source_history[0].count)
#define unsaved_count (source_history[0].unsaved_count)
#define first_unsaved_idx (source_history[0].first_unsaved_idx)
#define last_saved_battery_level (source_history[0].last_saved_level)
#define last_saved_timestamp (source_history[0].last_saved_timestamp)
#define current_battery_level (source_history[0].current_level)
#define first_record_after_boot (source_history[0].first_record_after_boot)
#define head_changed_since_save (source_history[0].head_changed_since_save)

// Work item for periodic recording
static void battery_history_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_history_work, battery_history_work_handler);

// Track if initialization is done, meaning settings have been loaded
static bool initialization_done = false;

// Settings handling
static int battery_history_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                        void *cb_arg);
static int battery_history_settings_commit(void);

SETTINGS_STATIC_HANDLER_DEFINE(battery_history, "battery_history", NULL,
                               battery_history_settings_set, battery_history_settings_commit, NULL);

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
        head_changed_since_save = true;
    }

    history_buffer[write_idx].timestamp = timestamp;
    history_buffer[write_idx].battery_level = level;

    // Track first unsaved entry index
    if (first_unsaved_idx < 0) {
        first_unsaved_idx = write_idx;
    }
    unsaved_count++;

    LOG_DBG("Added battery history entry: timestamp=%u, level=%u, idx=%d "
            "(total=%d, unsaved=%d)",
            timestamp, level, write_idx, history_count, unsaved_count);
}

/**
 * Set a single entry in settings (without immediate flush)
 */
static int set_single_entry(int buffer_idx) {
    char key[32];
    snprintf(key, sizeof(key), "battery_history/e%d", buffer_idx);

    int rc = settings_runtime_set(key, &history_buffer[buffer_idx],
                                  sizeof(struct zmk_battery_history_entry));
    if (rc < 0) {
        LOG_ERR("Failed to set entry %d: %d", buffer_idx, rc);
    }
    return rc;
}

/**
 * Save history to persistent storage (incremental save)
 * Uses settings_runtime_set for each item, then a single flush at the end
 */
static int save_history(void) {
    if (!initialization_done) {
        LOG_WRN("Settings not loaded yet, skipping battery history save");
        return 0;
    }
    if (unsaved_count == 0) {
        return 0;
    }
    // TODO: take locks
    LOG_INF("Saving battery history to flash (count=%d, unsaved=%d, "
            "head_changed=%d)",
            history_count, unsaved_count, head_changed_since_save);

    int rc;

    // Set head and count (small data, always needed)
    rc = settings_runtime_set("battery_history/head", &history_head, sizeof(history_head));
    if (rc < 0) {
        LOG_ERR("Failed to set history head: %d", rc);
        return rc;
    }

    rc = settings_runtime_set("battery_history/count", &history_count, sizeof(history_count));
    if (rc < 0) {
        LOG_ERR("Failed to set history count: %d", rc);
        return rc;
    }

    // Set only the entries that have changed
    // Note that since zephyr skips unchanged entries during settings_save(),
    // tracking which entries changed is not strictly necessary?
    if (first_unsaved_idx >= 0) {
        int entries_to_save = unsaved_count;
        int idx = first_unsaved_idx;

        LOG_DBG("Incremental save: %d entries starting from idx %d", entries_to_save, idx);

        for (int i = 0; i < entries_to_save; i++) {
            rc = set_single_entry(idx);
            if (rc < 0) {
                return rc;
            }
            idx = (idx + 1) % MAX_ENTRIES;
        }
    }

    // Single flush to commit all changes to storage
    rc = settings_save();
    if (rc < 0) {
        LOG_ERR("Failed to flush settings: %d", rc);
        return rc;
    }
    first_unsaved_idx = -1;
    unsaved_count = 0;
    head_changed_since_save = false;
    last_saved_battery_level = current_battery_level;
    last_saved_timestamp = k_uptime_get() / 1000;

    LOG_INF("Battery history saved successfully (incremental)");
    return 0;
}

/**
 * Check if we should save based on battery level drop
 * Returns true if battery has dropped by threshold since last save
 */
static bool should_save_entries(uint16_t timestamp, uint8_t level) {
    uint8_t level_gap = last_saved_battery_level > level
                            ? last_saved_battery_level - level
                            : level - last_saved_battery_level;
    if (level_gap >= SAVE_LEVEL_THRESHOLD) {
        LOG_DBG("Save triggered by level threshold");
        return true;
    }
    uint16_t time_gap = timestamp - last_saved_timestamp;
    if (time_gap >= SAVE_INTERVAL_SEC) {
        LOG_DBG("Save triggered by time threshold");
        return true;
    }
    LOG_DBG("Skipped to save");
    return false;
}

/**
 * Check if we should record based on battery level change
 * Returns true if we should add a new entry
 */
static bool should_record_entry(uint16_t timestamp, uint8_t level) {
#ifdef CONFIG_ZMK_BATTERY_SKIP_IF_USB_POWERED
    if (zmk_usb_is_powered()) {
        LOG_DBG("USB powered, skipping battery history record");
        return false;
    }
#endif
    // Always record the first entry after boot
    if (first_record_after_boot) {
        first_record_after_boot = false;
        LOG_DBG("Recording first entry after boot");
        return true;
    }

    // Get the last recorded entry
    struct zmk_battery_history_entry last_entry;
    if (!get_last_entry(&last_entry)) {
        // No previous entry, record this one
        LOG_DBG("No previous entry, recording new entry");
        return true;
    }

    // Always record if battery level changed
    if (last_entry.battery_level != level) {
        LOG_DBG("Recording entry: level changed from %d%% to %d%%", last_entry.battery_level,
                level);
        return true;
    }

#ifdef CONFIG_ZMK_BATTERY_IGNORE_ZERO_LEVEL
    if (level == 0) {
        // Ignore since 0 may indicate uninitialized value
        LOG_DBG("Battery level is 0%%, skipping record");
        return false;
    }
#endif

    // If level is the same, only record if enough time has passed
    // This reduces redundant entries when battery is stable
    // Note: Since timestamp resets on boot, wrap-around is not a concern here
    // as we always record first entry after boot
    uint16_t time_diff = timestamp - last_entry.timestamp;
    if (time_diff >= MIN_SAME_LEVEL_INTERVAL_SEC) {
        LOG_DBG("Recording entry: time threshold passed (%u sec)", time_diff);
        return true;
    }

    LOG_DBG("Skipping record: level unchanged (%d%%), time_diff=%u < threshold=%d", level,
            time_diff, MIN_SAME_LEVEL_INTERVAL_SEC);
    return false;
}

/**
 * Record current battery level to history
 */
static void record_battery_level() {
    if (!initialization_done) {
        LOG_WRN("Settings not loaded yet, skipping battery record");
        return;
    }
    // Get current timestamp (seconds since boot)
    uint16_t timestamp = (uint16_t)(k_uptime_get() / 1000);

    // Get current battery level
    int level = zmk_battery_state_of_charge();
    if (level < 0) {
        LOG_WRN("Failed to get battery level: %d", level);
        return;
    }
#ifdef CONFIG_ZMK_BATTERY_IGNORE_ZERO_LEVEL
    if (level == 0) {
        // Ignore since 0 may indicate uninitialized value
        LOG_DBG("Battery level is 0%%, skipping record");
        return;
    }
#endif

    current_battery_level = (uint8_t)level;

    // Check if we should add this entry
    if (!should_record_entry(timestamp, current_battery_level)) {
        return;
    }

    add_history_entry(timestamp, current_battery_level);

    // Save to flash if battery level has dropped by threshold
    if (should_save_entries(timestamp, current_battery_level)) {
        save_history();
    }
}

/**
 * Work handler for periodic battery recording
 */
static void battery_history_work_handler(struct k_work *work) {
    if (!initialization_done) {
        // Settings not yet loaded, skip recording
        k_work_schedule(&battery_history_work, K_MSEC(1000));
        return;
    }
    record_battery_level();

    // Schedule next recording
    k_work_schedule(&battery_history_work, K_MSEC(RECORDING_INTERVAL_MS));
}

/**
 * Settings load handler
 */
static int battery_history_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                        void *cb_arg) {
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

    // individual entries with "eN" keys
    if (name[0] == 'e') {
        int idx = atoi(name + 1);
        if (idx >= 0 && idx < MAX_ENTRIES) {
            if (len != sizeof(struct zmk_battery_history_entry)) {
                return -EINVAL;
            }
            return read_cb(cb_arg, &history_buffer[idx], sizeof(struct zmk_battery_history_entry));
        }
    }

    return -ENOENT;
}

/**
 * Settings commit handler - called after all settings are loaded
 */
static int battery_history_settings_commit(void) {
    LOG_INF("Battery history loaded: count=%d, head=%d", history_count, history_head);
    // Initialize last_saved_battery_level from the most recent entry if
    // available
    struct zmk_battery_history_entry last_entry;
    if (get_last_entry(&last_entry)) {
        last_saved_battery_level = last_entry.battery_level;
        // Note: last_saved_timestamp is not restored from storage since timestamp
        // resets on boot
    }
    initialization_done = true;
    return 0;
}

/**
 * Handle battery state change events
 */
static int battery_history_event_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *bev = as_zmk_battery_state_changed(eh);
    if (bev) {
        k_work_reschedule(&battery_history_work, K_NO_WAIT);
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
#ifdef CONFIG_ZMK_BATTERY_HISTORY_FORCE_SAVE_ON_SLEEP
        // Force save any unsaved data
        if (unsaved_count > 0) {
            save_history();
        }
#endif
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
    LOG_INF("Max entries: %d, Recording interval: %d minutes, Save level "
            "threshold: %d%%",
            MAX_ENTRIES, CONFIG_ZMK_BATTERY_HISTORY_INTERVAL_MINUTES, SAVE_LEVEL_THRESHOLD);

    // Initialize all source histories
    // Temporarily undefine macros to avoid conflicts
    #undef history_buffer
    #undef history_head
    #undef history_count
    #undef unsaved_count
    #undef first_unsaved_idx
    #undef last_saved_battery_level
    #undef last_saved_timestamp
    #undef current_battery_level
    #undef first_record_after_boot
    #undef head_changed_since_save
    
    for (int i = 0; i < SOURCE_COUNT; i++) {
        source_history[i].head = 0;
        source_history[i].count = 0;
        source_history[i].unsaved_count = 0;
        source_history[i].first_unsaved_idx = -1;
        source_history[i].last_saved_level = 100;
        source_history[i].last_saved_timestamp = 0;
        source_history[i].current_level = 0;
        source_history[i].first_record_after_boot = true;
        source_history[i].head_changed_since_save = false;
        memset(source_history[i].buffer, 0, sizeof(source_history[i].buffer));
    }
    
    // Redefine macros for backward compatibility
    #define history_buffer (source_history[0].buffer)
    #define history_head (source_history[0].head)
    #define history_count (source_history[0].count)
    #define unsaved_count (source_history[0].unsaved_count)
    #define first_unsaved_idx (source_history[0].first_unsaved_idx)
    #define last_saved_battery_level (source_history[0].last_saved_level)
    #define last_saved_timestamp (source_history[0].last_saved_timestamp)
    #define current_battery_level (source_history[0].current_level)
    #define first_record_after_boot (source_history[0].first_record_after_boot)
    #define head_changed_since_save (source_history[0].head_changed_since_save)

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    LOG_INF("Split keyboard support enabled: %d sources (1 central + %d peripherals)",
            SOURCE_COUNT, ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT);
#else
    LOG_INF("Non-split keyboard: 1 source");
#endif

    // Start
    k_work_schedule(&battery_history_work, K_NO_WAIT);

    return 0;
}

// Initialize after settings are loaded
SYS_INIT(battery_history_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* Public API implementation */

int zmk_battery_history_get_count(void) { return history_count; }

int zmk_battery_history_get_entry(int index, struct zmk_battery_history_entry *entry) {
    if (index < 0 || index >= history_count || entry == NULL) {
        return -EINVAL;
    }

    int buffer_idx = get_buffer_index(index);
    *entry = history_buffer[buffer_idx];
    return 0;
}

int zmk_battery_history_get_current_level(void) { return current_battery_level; }

int zmk_battery_history_clear(void) {
    int cleared = history_count;

    history_head = 0;
    history_count = 0;
    unsaved_count = 0;
    first_unsaved_idx = -1;
    first_record_after_boot = true;
    head_changed_since_save = false;
    last_saved_battery_level = current_battery_level;
    memset(history_buffer, 0, sizeof(history_buffer));

    // Save the cleared state using runtime_set + flush
    settings_runtime_set("battery_history/head", &history_head, sizeof(history_head));
    settings_runtime_set("battery_history/count", &history_count, sizeof(history_count));
    settings_save();

    LOG_INF("Battery history cleared: %d entries removed", cleared);
    return cleared;
}

int zmk_battery_history_get_interval(void) { return CONFIG_ZMK_BATTERY_HISTORY_INTERVAL_MINUTES; }

int zmk_battery_history_get_max_entries(void) { return MAX_ENTRIES; }

int zmk_battery_history_save(void) { return save_history(); }

int zmk_battery_history_get_source_count(void) { return SOURCE_COUNT; }

int zmk_battery_history_get_count_for_source(uint8_t source) {
    if (source >= SOURCE_COUNT) {
        return -EINVAL;
    }
    return source_history[source].count;
}

int zmk_battery_history_get_entry_for_source(uint8_t source, int index,
                                              struct zmk_battery_history_entry *entry) {
    if (source >= SOURCE_COUNT || index < 0 || entry == NULL) {
        return -EINVAL;
    }

    struct battery_source_history *src = &source_history[source];
    if (index >= src->count) {
        return -EINVAL;
    }

    int buffer_idx = (src->head + index) % MAX_ENTRIES;
    *entry = src->buffer[buffer_idx];
    return 0;
}

int zmk_battery_history_get_current_level_for_source(uint8_t source) {
    if (source >= SOURCE_COUNT) {
        return -EINVAL;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) && \
    IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    // For peripheral sources, query the split central
    if (source > 0) {
        uint8_t level = 0;
        int rc = zmk_split_central_get_peripheral_battery_level(source - 1, &level);
        if (rc == 0) {
            return level;
        }
        return rc;
    }
#endif

    // For source 0 (central) or non-split, return cached value
    return source_history[source].current_level;
}
