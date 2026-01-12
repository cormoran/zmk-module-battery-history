/*
 * Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 *
 * Battery History Split Support
 *
 * This file implements the split keyboard support for battery history.
 * - On peripherals: Listens for battery history entry events and reports them to central
 * - On central: Receives battery history entry events from peripherals and raises RPC notifications
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/battery_history/battery_history.h>
#include <zmk/battery_history/events/battery_history_entry_event.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#include <zmk/split/transport/central.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL)
#include <zmk/split/transport/peripheral.h>
#endif

LOG_MODULE_REGISTER(zmk_battery_history_split, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL)

/**
 * Listener for battery history entry events on peripheral.
 * When a battery history entry event is raised, report it to central.
 */
static int battery_history_peripheral_listener(const zmk_event_t *eh) {
    struct zmk_battery_history_entry_event *ev = as_zmk_battery_history_entry_event(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    LOG_DBG("Peripheral: battery history entry event received, idx=%d/%d",
            ev->entry_index, ev->total_entries);

    // Create a peripheral event to send to central
    // We use a custom event type for battery history entries
    struct zmk_split_transport_peripheral_event peripheral_ev = {
        .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_BATTERY_HISTORY_ENTRY,
        .data = {
            .battery_history_entry = {
                .timestamp = ev->entry.timestamp,
                .battery_level = ev->entry.battery_level,
                .entry_index = ev->entry_index,
                .total_entries = ev->total_entries,
                .is_last = ev->is_last,
            },
        },
    };

    int rc = zmk_split_peripheral_report_event(&peripheral_ev);
    if (rc < 0) {
        LOG_ERR("Failed to report battery history entry to central: %d", rc);
    }

    return ZMK_EV_EVENT_HANDLED;
}

ZMK_LISTENER(battery_history_peripheral, battery_history_peripheral_listener);
ZMK_SUBSCRIPTION(battery_history_peripheral, zmk_battery_history_entry_event);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL)
