/**
 * Battery History Service
 *
 * This service monitors battery state changes and maintains a history
 * of battery levels over time, stored in persistent storage.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/settings/settings.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/battery.h>
#include <zmk/battery_history.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Maximum number of battery history entries
#define MAX_HISTORY_ENTRIES CONFIG_ZMK_BATTERY_HISTORY_MAX_ENTRIES

// Save interval in milliseconds
#define SAVE_INTERVAL_MS (CONFIG_ZMK_BATTERY_HISTORY_SAVE_INTERVAL_MINUTES * 60 * 1000)

// Storage key for battery history
#define SETTINGS_KEY "battery_history"
#define SETTINGS_KEY_COUNT SETTINGS_KEY "/count"
#define SETTINGS_KEY_ENTRIES SETTINGS_KEY "/entries"

// Battery history storage
static struct battery_history_entry history[MAX_HISTORY_ENTRIES];
static int history_count = 0;
static struct k_work_delayable save_work;

// Forward declarations
static void save_battery_state(struct k_work *work);

/**
 * Get current Unix timestamp
 * Note: This is a simplified implementation. In production, you'd want
 * to use a proper RTC or sync time from the host.
 */
static uint32_t get_current_timestamp(void) {
    // For now, use uptime in seconds as a simple timestamp
    // In a real implementation, you'd sync this with actual time from the host
    return (uint32_t)(k_uptime_get() / 1000);
}

/**
 * Add a battery history entry
 */
static void add_history_entry(uint32_t timestamp, uint8_t battery_percentage) {
    // If we're at max capacity, shift entries left to make room
    if (history_count >= MAX_HISTORY_ENTRIES) {
        for (int i = 0; i < MAX_HISTORY_ENTRIES - 1; i++) {
            history[i] = history[i + 1];
        }
        history_count = MAX_HISTORY_ENTRIES - 1;
    }

    // Add new entry
    history[history_count].timestamp = timestamp;
    history[history_count].battery_percentage = battery_percentage;
    history_count++;

    LOG_DBG("Added battery history entry: %d%% at timestamp %u (total: %d)",
            battery_percentage, timestamp, history_count);
}

/**
 * Save battery history to persistent storage
 */
static int save_to_storage(void) {
    int rc;

    // Save count
    rc = settings_save_one(SETTINGS_KEY_COUNT, &history_count, sizeof(history_count));
    if (rc != 0) {
        LOG_ERR("Failed to save history count: %d", rc);
        return rc;
    }

    // Save entries
    if (history_count > 0) {
        rc = settings_save_one(SETTINGS_KEY_ENTRIES, history,
                               history_count * sizeof(struct battery_history_entry));
        if (rc != 0) {
            LOG_ERR("Failed to save history entries: %d", rc);
            return rc;
        }
    }

    LOG_DBG("Saved %d battery history entries to storage", history_count);
    return 0;
}

/**
 * Work handler to save battery state periodically
 */
static void save_battery_state(struct k_work *work) {
    uint8_t battery = zmk_battery_state_of_charge();
    uint32_t timestamp = get_current_timestamp();

    add_history_entry(timestamp, battery);
    save_to_storage();

    // Schedule next save
    k_work_reschedule(&save_work, K_MSEC(SAVE_INTERVAL_MS));
}

/**
 * Settings load handler
 */
static int settings_load_handler(const char *key, size_t len,
                                  settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(key, "count") == 0) {
        if (len != sizeof(history_count)) {
            LOG_WRN("Invalid history count size: %zu", len);
            return -EINVAL;
        }
        return read_cb(cb_arg, &history_count, sizeof(history_count));
    } else if (strcmp(key, "entries") == 0) {
        if (len > sizeof(history)) {
            LOG_WRN("History entries too large: %zu", len);
            return -EINVAL;
        }
        int rc = read_cb(cb_arg, history, len);
        if (rc >= 0) {
            LOG_INF("Loaded %d battery history entries from storage", history_count);
        }
        return rc;
    }

    return -ENOENT;
}

/**
 * Battery state changed event listener
 * Records significant battery changes immediately
 */
static int battery_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (ev == NULL) {
        return 0;
    }

    LOG_DBG("Battery state changed: %d%%", ev->state_of_charge);

    // Only record if this is a significant change (5% or more from last entry)
    if (history_count == 0) {
        uint32_t timestamp = get_current_timestamp();
        add_history_entry(timestamp, ev->state_of_charge);
        save_to_storage();
    } else {
        int diff = (int)ev->state_of_charge - (int)history[history_count - 1].battery_percentage;
        if (diff < 0) diff = -diff;
        if (diff >= 5) {
            uint32_t timestamp = get_current_timestamp();
            add_history_entry(timestamp, ev->state_of_charge);
            save_to_storage();
        }
    }

    return 0;
}

ZMK_LISTENER(battery_history_listener, battery_state_changed_listener);
ZMK_SUBSCRIPTION(battery_history_listener, zmk_battery_state_changed);

/**
 * Initialize battery history service
 */
static int battery_history_init(void) {
    int rc;

    // Register settings handler
    static struct settings_handler cfg = {
        .name = SETTINGS_KEY,
        .h_set = settings_load_handler,
    };
    rc = settings_register(&cfg);
    if (rc != 0) {
        LOG_ERR("Failed to register settings handler: %d", rc);
        return rc;
    }

    // Load settings
    settings_load_subtree(SETTINGS_KEY);

    // Initialize periodic save work
    k_work_init_delayable(&save_work, save_battery_state);

    // Schedule first save
    k_work_reschedule(&save_work, K_MSEC(SAVE_INTERVAL_MS));

    LOG_INF("Battery history service initialized (interval: %d min, max entries: %d)",
            CONFIG_ZMK_BATTERY_HISTORY_SAVE_INTERVAL_MINUTES, MAX_HISTORY_ENTRIES);

    return 0;
}

SYS_INIT(battery_history_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

// Public API implementations

int battery_history_get_count(void) {
    return history_count;
}

int battery_history_get_entries(struct battery_history_entry *entries, int max_entries) {
    if (entries == NULL || max_entries <= 0) {
        return -EINVAL;
    }

    int count = history_count < max_entries ? history_count : max_entries;
    memcpy(entries, history, count * sizeof(struct battery_history_entry));
    return count;
}

int battery_history_clear(void) {
    history_count = 0;
    int rc = save_to_storage();
    if (rc == 0) {
        LOG_INF("Battery history cleared");
    }
    return rc;
}

uint8_t battery_history_get_current_battery(void) {
    return zmk_battery_state_of_charge();
}
