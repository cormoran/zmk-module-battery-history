/*
 * Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 *
 * Behavior to request battery history from peripheral.
 * When triggered on central, it sends a command to the specified peripheral
 * to send its battery history entries back to central.
 * When triggered on peripheral, it sends battery history entries to central.
 */

#define DT_DRV_COMPAT zmk_behavior_battery_history_request

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/battery_history/battery_history.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    uint8_t peripheral_id = binding->param1;
    LOG_INF("Battery history request behavior pressed, peripheral_id=%d", peripheral_id);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    // On central: send command to peripheral to trigger battery history send
    // The behavior will be invoked on the peripheral with same params
    int rc = zmk_split_central_invoke_behavior(peripheral_id, binding, event, true);
    if (rc < 0) {
        LOG_ERR("Failed to invoke behavior on peripheral %d: %d", peripheral_id, rc);
        return rc;
    }
    LOG_DBG("Sent battery history request to peripheral %d", peripheral_id);
#else
    // On peripheral: trigger sending battery history entries
    int rc = zmk_battery_history_trigger_send();
    if (rc < 0) {
        LOG_ERR("Failed to trigger battery history send: %d", rc);
        return rc;
    }
    LOG_DBG("Triggered battery history send on peripheral");
#endif

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_battery_history_request_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_battery_history_request_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
