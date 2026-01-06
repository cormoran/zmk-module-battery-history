/**
 * Battery History - Custom Studio RPC Handler
 *
 * This file implements a custom RPC subsystem for ZMK Studio
 * to retrieve and manage battery consumption history.
 */

#include <pb_decode.h>
#include <pb_encode.h>
#include <zmk/studio/custom.h>
#include <zmk/template/custom.pb.h>
#include <zmk/battery_history.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/**
 * Metadata for the custom subsystem.
 * - ui_urls: URLs where the custom UI can be loaded from
 * - security: Security level for the RPC handler
 */
static struct zmk_rpc_custom_subsystem_meta battery_history_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("http://localhost:5173"),
    // Unsecured is suggested by default to avoid unlocking in unreliable
    // environments.
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

/**
 * Register the custom RPC subsystem.
 * The first argument is the subsystem name used to route requests from the
 * frontend. Format: <namespace>__<feature> (double underscore)
 */
ZMK_RPC_CUSTOM_SUBSYSTEM(zmk__battery_history, &battery_history_meta,
                         battery_history_rpc_handle_request);

ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(zmk__battery_history, zmk_template_Response);

static int handle_get_battery_history_request(const zmk_template_GetBatteryHistoryRequest *req,
                                               zmk_template_Response *resp);
static int handle_clear_battery_history_request(const zmk_template_ClearBatteryHistoryRequest *req,
                                                 zmk_template_Response *resp);

/**
 * Main request handler for the custom RPC subsystem.
 * Sets up the encoding callback for the response.
 */
static bool
battery_history_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                    pb_callback_t *encode_response) {
  zmk_template_Response *resp =
      ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(zmk__battery_history,
                                                        encode_response);

  zmk_template_Request req = zmk_template_Request_init_zero;

  // Decode the incoming request from the raw payload
  pb_istream_t req_stream = pb_istream_from_buffer(raw_request->payload.bytes,
                                                   raw_request->payload.size);
  if (!pb_decode(&req_stream, zmk_template_Request_fields, &req)) {
    LOG_WRN("Failed to decode battery history request: %s", PB_GET_ERROR(&req_stream));
    zmk_template_ErrorResponse err = zmk_template_ErrorResponse_init_zero;
    snprintf(err.message, sizeof(err.message), "Failed to decode request");
    resp->which_response_type = zmk_template_Response_error_tag;
    resp->response_type.error = err;
    return true;
  }

  int rc = 0;
  switch (req.which_request_type) {
  case zmk_template_Request_get_battery_history_tag:
    rc = handle_get_battery_history_request(&req.request_type.get_battery_history, resp);
    break;
  case zmk_template_Request_clear_battery_history_tag:
    rc = handle_clear_battery_history_request(&req.request_type.clear_battery_history, resp);
    break;
  default:
    LOG_WRN("Unsupported battery history request type: %d", req.which_request_type);
    rc = -1;
  }

  if (rc != 0) {
    zmk_template_ErrorResponse err = zmk_template_ErrorResponse_init_zero;
    snprintf(err.message, sizeof(err.message), "Failed to process request");
    resp->which_response_type = zmk_template_Response_error_tag;
    resp->response_type.error = err;
  }
  return true;
}

/**
 * Handle GetBatteryHistoryRequest and populate the response.
 */
static int handle_get_battery_history_request(const zmk_template_GetBatteryHistoryRequest *req,
                                               zmk_template_Response *resp) {
  LOG_DBG("Received get battery history request");

  zmk_template_GetBatteryHistoryResponse result = zmk_template_GetBatteryHistoryResponse_init_zero;

  // Get current battery level
  result.current_battery = battery_history_get_current_battery();

  // Get history entries
  int count = battery_history_get_count();
  result.total_entries = count;

  // Allocate temporary buffer for entries
  struct battery_history_entry entries[CONFIG_ZMK_BATTERY_HISTORY_MAX_ENTRIES];
  int retrieved = battery_history_get_entries(entries, CONFIG_ZMK_BATTERY_HISTORY_MAX_ENTRIES);

  if (retrieved < 0) {
    LOG_ERR("Failed to get battery history entries: %d", retrieved);
    return retrieved;
  }

  // Copy entries to response
  result.entries_count = retrieved;
  for (int i = 0; i < retrieved; i++) {
    result.entries[i].timestamp = entries[i].timestamp;
    result.entries[i].battery_percentage = entries[i].battery_percentage;
  }

  LOG_DBG("Returning %d battery history entries (current: %d%%)", 
          retrieved, result.current_battery);

  resp->which_response_type = zmk_template_Response_battery_history_tag;
  resp->response_type.battery_history = result;
  return 0;
}

/**
 * Handle ClearBatteryHistoryRequest and populate the response.
 */
static int handle_clear_battery_history_request(const zmk_template_ClearBatteryHistoryRequest *req,
                                                 zmk_template_Response *resp) {
  LOG_DBG("Received clear battery history request");

  zmk_template_ClearBatteryHistoryResponse result = zmk_template_ClearBatteryHistoryResponse_init_zero;

  int rc = battery_history_clear();
  result.success = (rc == 0);

  if (rc == 0) {
    LOG_INF("Battery history cleared successfully");
  } else {
    LOG_ERR("Failed to clear battery history: %d", rc);
  }

  resp->which_response_type = zmk_template_Response_clear_battery_history_tag;
  resp->response_type.clear_battery_history = result;
  return 0;
}