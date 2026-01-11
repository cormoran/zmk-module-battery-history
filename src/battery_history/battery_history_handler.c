/*
 * Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 *
 * Battery History - Custom Studio RPC Handler
 *
 * This file implements the RPC subsystem for retrieving battery history
 * data from ZMK devices via ZMK Studio.
 */

#include <pb_decode.h>
#include <pb_encode.h>
#include <zmk/studio/custom.h>
#include <zmk/battery_history/battery_history.pb.h>
#include <zmk/battery_history/battery_history.h>

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
 * Handle GetBatteryHistoryRequest and populate the response.
 */
static int handle_get_history_request(const zmk_battery_history_GetBatteryHistoryRequest *req,
                                      zmk_battery_history_Response *resp) {
    LOG_DBG("Received get battery history request (include_metadata=%d)", req->include_metadata);

    zmk_battery_history_GetBatteryHistoryResponse result =
        zmk_battery_history_GetBatteryHistoryResponse_init_zero;

    // Get current battery level
    int current_level = zmk_battery_history_get_current_level();
    if (current_level >= 0) {
        result.current_battery_level = (uint32_t)current_level;
    }

    // Get history entries
    int count = zmk_battery_history_get_count();
    result.entries_count = 0;

    for (int i = 0; i < count && i < CONFIG_ZMK_BATTERY_HISTORY_MAX_ENTRIES; i++) {
        struct zmk_battery_history_entry entry;
        if (zmk_battery_history_get_entry(i, &entry) == 0) {
            result.entries[result.entries_count].timestamp = entry.timestamp;
            result.entries[result.entries_count].battery_level = entry.battery_level;
            result.entries_count++;
        }
    }

    // Include metadata if requested
    if (req->include_metadata) {
        result.has_metadata = true;
        snprintf(result.metadata.device_name, sizeof(result.metadata.device_name), "ZMK Keyboard");
        result.metadata.recording_interval_minutes = (uint32_t)zmk_battery_history_get_interval();
        result.metadata.max_entries = (uint32_t)zmk_battery_history_get_max_entries();
    }

    LOG_INF("Returning battery history: %d entries, current level: %d%%", result.entries_count,
            result.current_battery_level);

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

    int cleared = zmk_battery_history_clear();
    result.entries_cleared = (uint32_t)cleared;

    LOG_INF("Cleared %d battery history entries", cleared);

    resp->which_response_type = zmk_battery_history_Response_clear_history_tag;
    resp->response_type.clear_history = result;
    return 0;
}
