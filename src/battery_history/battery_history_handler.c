/*
 * Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 *
 * Battery History - Custom Studio RPC Handler
 *
 * This file implements the RPC subsystem for retrieving battery history
 * data from ZMK devices via ZMK Studio.
 *
 * For split keyboard support, this handler uses RPC notifications to stream
 * battery history entries. This allows the UI to receive data progressively
 * as it's collected from peripherals.
 */

#include <pb_decode.h>
#include <pb_encode.h>
#include <zmk/studio/custom.h>
#include <zmk/battery_history/battery_history.pb.h>
#include <zmk/battery_history/battery_history.h>
#include <zmk/battery_history/events/battery_history_entry_event.h>

#include <zmk/behavior.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/**
 * Metadata for the battery history custom subsystem.
 * - ui_urls: URLs where the custom UI can be loaded from
 * - security: Security level for the RPC handler
 */
static struct zmk_rpc_custom_subsystem_meta battery_history_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("http://localhost:5173"),
    // Unsecured to allow easy access for battery monitoring
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

// Forward declaration of subsystem index getter
static int get_subsystem_index(void);

/**
 * Register the custom RPC subsystem.
 * Format: <namespace>__<feature> (double underscore)
 */
ZMK_RPC_CUSTOM_SUBSYSTEM(zmk__battery_history, &battery_history_meta,
                         battery_history_rpc_handle_request);

ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(zmk__battery_history, zmk_battery_history_Response);

static int handle_get_history_request(const zmk_battery_history_GetBatteryHistoryRequest *req,
                                      zmk_battery_history_Response *resp);
static int handle_clear_history_request(const zmk_battery_history_ClearBatteryHistoryRequest *req,
                                        zmk_battery_history_Response *resp);

/**
 * Main request handler for the battery history RPC subsystem.
 */
static bool battery_history_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                               pb_callback_t *encode_response) {
    zmk_battery_history_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(zmk__battery_history, encode_response);

    zmk_battery_history_Request req = zmk_battery_history_Request_init_zero;

    // Decode the incoming request from the raw payload
    pb_istream_t req_stream =
        pb_istream_from_buffer(raw_request->payload.bytes, raw_request->payload.size);
    if (!pb_decode(&req_stream, zmk_battery_history_Request_fields, &req)) {
        LOG_WRN("Failed to decode battery history request: %s", PB_GET_ERROR(&req_stream));
        zmk_battery_history_ErrorResponse err = zmk_battery_history_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message), "Failed to decode request");
        resp->which_response_type = zmk_battery_history_Response_error_tag;
        resp->response_type.error = err;
        return true;
    }

    int rc = 0;
    switch (req.which_request_type) {
    case zmk_battery_history_Request_get_history_tag:
        rc = handle_get_history_request(&req.request_type.get_history, resp);
        break;
    case zmk_battery_history_Request_clear_history_tag:
        rc = handle_clear_history_request(&req.request_type.clear_history, resp);
        break;
    default:
        LOG_WRN("Unsupported battery history request type: %d", req.which_request_type);
        rc = -1;
    }

    if (rc != 0) {
        zmk_battery_history_ErrorResponse err = zmk_battery_history_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message), "Failed to process request");
        resp->which_response_type = zmk_battery_history_Response_error_tag;
        resp->response_type.error = err;
    }
    return true;
}

/**
 * Handle GetBatteryHistoryRequest.
 * This triggers the battery history request behavior, which:
 * - On central: sends local battery history as notifications
 * - On all peripherals (via LOCALITY_GLOBAL): sends their battery history as notifications
 * The actual data comes via notifications when each device responds.
 */
static int handle_get_history_request(const zmk_battery_history_GetBatteryHistoryRequest *req,
                                      zmk_battery_history_Response *resp) {
    LOG_INF("Received request for battery history from all devices");

    int rc = raise_zmk_battery_history_request_event((struct zmk_battery_history_request_event){
        .type = ZMK_BATTERY_HISTORY_REQUEST_EVENT_TYPE_REQUEST_ENTRIES});
    if (rc < 0) {
        return rc;
    }

    // Return an empty success response to acknowledge the request
    // The actual data will come via notifications
    zmk_battery_history_GetBatteryHistoryResponse result =
        zmk_battery_history_GetBatteryHistoryResponse_init_zero;

    resp->which_response_type = zmk_battery_history_Response_get_history_tag;
    resp->response_type.get_history = result;
    return 0;
}

/**
 * Handle ClearBatteryHistoryRequest and populate the response.
 */
static int handle_clear_history_request(const zmk_battery_history_ClearBatteryHistoryRequest *req,
                                        zmk_battery_history_Response *resp) {
    LOG_DBG("Received clear battery history request");

    zmk_battery_history_ClearBatteryHistoryResponse result =
        zmk_battery_history_ClearBatteryHistoryResponse_init_zero;

    int rc = raise_zmk_battery_history_request_event((struct zmk_battery_history_request_event){
        .type = ZMK_BATTERY_HISTORY_REQUEST_EVENT_TYPE_CLEAR_HISTORY});
    if (rc < 0) {
        return rc;
    }

    resp->which_response_type = zmk_battery_history_Response_clear_history_tag;
    resp->response_type.clear_history = result;
    return 0;
}

/**
 * Encoder function for notification payload
 */
static bool encode_notification_payload(pb_ostream_t *stream, const pb_field_t *field,
                                        void *const *arg) {
    const zmk_battery_history_Notification *notif = (const zmk_battery_history_Notification *)*arg;
    if (!pb_encode_tag_for_field(stream, field)) {
        return false;
    }

    size_t size;
    if (!pb_get_encoded_size(&size, zmk_battery_history_Notification_fields, notif)) {
        LOG_WRN("Failed to get encoded size for notification");
        return false;
    }

    if (!pb_encode_varint(stream, size)) {
        return false;
    }
    return pb_encode(stream, zmk_battery_history_Notification_fields, notif);
}

/**
 * Get the subsystem index for this custom subsystem
 */
static int get_subsystem_index(void) {
    size_t subsystem_count;
    STRUCT_SECTION_COUNT(zmk_rpc_custom_subsystem, &subsystem_count);

    for (size_t i = 0; i < subsystem_count; i++) {
        struct zmk_rpc_custom_subsystem *custom_subsys;
        STRUCT_SECTION_GET(zmk_rpc_custom_subsystem, i, &custom_subsys);
        if (strcmp(custom_subsys->identifier, "zmk__battery_history") == 0) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * Send a battery history notification for a single entry
 */
int zmk_battery_history_send_notification(uint8_t source_id,
                                          const struct zmk_battery_history_entry *entry,
                                          uint8_t entry_index, uint8_t total_entries,
                                          bool is_last) {
    int subsystem_idx = get_subsystem_index();
    if (subsystem_idx < 0) {
        LOG_ERR("Failed to get subsystem index");
        return -ENOENT;
    }
    // Buffer for notification payload
    zmk_battery_history_Notification notification_buffer =
        zmk_battery_history_Notification_init_zero;
    notification_buffer.which_notification_type =
        zmk_battery_history_Notification_battery_history_tag;
    notification_buffer.notification_type.battery_history.source_id = source_id;
    notification_buffer.notification_type.battery_history.has_entry = true;
    notification_buffer.notification_type.battery_history.entry.timestamp = entry->timestamp;
    notification_buffer.notification_type.battery_history.entry.battery_level =
        entry->battery_level;
    notification_buffer.notification_type.battery_history.entry_index = entry_index;
    notification_buffer.notification_type.battery_history.total_entries = total_entries;
    notification_buffer.notification_type.battery_history.is_last = is_last;

    struct zmk_studio_custom_notification notif = {
        .subsystem_index = (uint8_t)subsystem_idx,
        .encode_payload =
            {
                .funcs.encode = encode_notification_payload,
                .arg = &notification_buffer,
            },
    };

    LOG_DBG("Sending battery history notification: source=%d, idx=%d/%d, level=%d%%", source_id,
            entry_index, total_entries, entry->battery_level);

    return raise_zmk_studio_custom_notification(notif);
}

/**
 * Listener for battery history entry events.
 * When an entry event is raised (from local or remote), send RPC notification.
 */
static int battery_history_entry_listener(const zmk_event_t *eh) {
    struct zmk_battery_history_entry_event *ev = as_zmk_battery_history_entry_event(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint8_t source_id = ev->source == ZMK_RELAY_EVENT_SOURCE_SELF ? 0 : ev->source;

    LOG_DBG("Battery history entry event: source=%d, idx=%d/%d", source_id, ev->entry_index,
            ev->total_entries);

    // Send RPC notification for this entry
    int rc = zmk_battery_history_send_notification(source_id, &ev->entry, ev->entry_index,
                                                   ev->total_entries, ev->is_last);
    if (rc < 0) {
        LOG_ERR("Failed to send battery history notification: %d", rc);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(battery_history_entry, battery_history_entry_listener);
ZMK_SUBSCRIPTION(battery_history_entry, zmk_battery_history_entry_event);
