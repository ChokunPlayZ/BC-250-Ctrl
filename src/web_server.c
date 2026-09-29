#include "web_server.h"

#include <stdlib.h>
#include <string.h>

#include "ble_presence.h"
#include "cJSON.h"
#include "config_store.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_service.h"
#include "mbedtls/base64.h"
#include "nvs_flash.h"
#include "ota_service.h"
#include "power_service.h"
#include "psu_i2c_service.h"
#include "status_service.h"
#include "wifi_service.h"
#include "zigbee_service.h"

static const char *TAG = "web";
static httpd_handle_t s_server;
static portMUX_TYPE s_events_lock = portMUX_INITIALIZER_UNLOCKED;
static unsigned s_event_clients;

extern const char INDEX_HTML[] asm("_binary_web_ui_html_start");

static bool authorized(httpd_req_t *request)
{
    if (bc250_wifi_is_config_ap()) return true;
    size_t header_length = httpd_req_get_hdr_value_len(request, "Authorization");
    if (header_length < 7 || header_length > 180) return false;
    char header[181];
    if (httpd_req_get_hdr_value_str(request, "Authorization", header, sizeof(header)) != ESP_OK ||
        strncmp(header, "Basic ", 6) != 0) return false;
    unsigned char decoded[128];
    size_t decoded_length = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_length,
                              (const unsigned char *)&header[6], strlen(&header[6])) != 0) return false;
    decoded[decoded_length] = '\0';
    char *separator = strchr((char *)decoded, ':');
    if (separator == NULL) return false;
    *separator = '\0';
    return strcmp((char *)decoded, "admin") == 0 && bc250_config_password_verify(separator + 1);
}

static bool require_auth(httpd_req_t *request)
{
    if (authorized(request)) return true;
    httpd_resp_set_status(request, "401 Unauthorized");
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"BC250 Controller\"");
    httpd_resp_sendstr(request, "Authentication required");
    return false;
}

static esp_err_t root_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *json = bc250_status_json();
    if (json == NULL) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return err;
}

static void close_ap_task(void *arg)
{
    (void)arg;
    /* Allow the HTTP acknowledgement to reach the connected phone first. */
    vTaskDelay(pdMS_TO_TICKS(750));
    ESP_ERROR_CHECK_WITHOUT_ABORT(bc250_wifi_close_config_ap());
    vTaskDelete(NULL);
}

static esp_err_t close_ap_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    if (!bc250_wifi_is_config_ap()) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Configuration AP is already off");
    }
    if (xTaskCreate(close_ap_task, "close_ap", 3072, NULL, 2, NULL) != pdPASS) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to close AP");
    }
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

static esp_err_t receive_body(httpd_req_t *request, char **body, size_t maximum)
{
    if (request->content_len <= 0 || (size_t)request->content_len > maximum) return ESP_ERR_INVALID_SIZE;
    char *buffer = malloc(request->content_len + 1);
    if (buffer == NULL) return ESP_ERR_NO_MEM;
    int offset = 0;
    while (offset < request->content_len) {
        int received = httpd_req_recv(request, &buffer[offset], request->content_len - offset);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0) {
            free(buffer);
            return ESP_FAIL;
        }
        offset += received;
    }
    buffer[offset] = '\0';
    *body = buffer;
    return ESP_OK;
}

static esp_err_t power_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *body;
    if (receive_body(request, &body, 256) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    cJSON *action = json ? cJSON_GetObjectItemCaseSensitive(json, "action") : NULL;
    bc250_power_action_t command = BC250_POWER_ACTION_NONE;
    if (cJSON_IsString(action)) {
        if (strcmp(action->valuestring, "on") == 0) command = BC250_POWER_ACTION_ON;
        else if (strcmp(action->valuestring, "off") == 0) command = BC250_POWER_ACTION_OFF;
        else if (strcmp(action->valuestring, "toggle") == 0) command = BC250_POWER_ACTION_TOGGLE;
        else if (strcmp(action->valuestring, "force_off") == 0) command = BC250_POWER_ACTION_FORCE_OFF;
    }
    if (command != BC250_POWER_ACTION_NONE) ESP_LOGI(TAG, "HTTP power command: %s", action->valuestring);
    cJSON_Delete(json);
    if (command == BC250_POWER_ACTION_NONE || !bc250_power_service_request(command)) {
        httpd_resp_set_status(request, "409 Conflict");
        httpd_resp_send(request, "Command rejected", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

static esp_err_t config_get_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *json = bc250_config_to_json(bc250_config_get(), false);
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return err;
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(750));
    ESP_LOGI(TAG, "HTTP request restarting controller");
    esp_restart();
}

static esp_err_t config_put_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *body;
    if (receive_body(request, &body, 12288) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Configuration is empty or too large");
        return ESP_FAIL;
    }
    bc250_config_t next = *bc250_config_get();
    char error[160];
    esp_err_t err = bc250_config_patch_json(&next, body, error, sizeof(error));
    free(body);
    if (err == ESP_OK) err = bc250_config_save_pending(&next);
    if (err != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, error[0] ? error : esp_err_to_name(err));
        return err;
    }
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, "{\"accepted\":true,\"rebooting\":true}");
    xTaskCreate(restart_task, "restart", 2048, NULL, 2, NULL);
    return ESP_OK;
}

static esp_err_t ble_scan_post_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    bc250_ble_start_learning(15000);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"accepted\":true,\"duration_ms\":15000}");
}

static esp_err_t ble_scan_get_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *json = bc250_ble_scan_results_json();
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return err;
}

static esp_err_t i2c_scan_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *body = NULL;
    if (receive_body(request, &body, 128) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid I2C scan request");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    cJSON *sda = json ? cJSON_GetObjectItemCaseSensitive(json, "sda_gpio") : NULL;
    cJSON *scl = json ? cJSON_GetObjectItemCaseSensitive(json, "scl_gpio") : NULL;
    bool valid = cJSON_IsNumber(sda) && cJSON_IsNumber(scl) &&
                 sda->valuedouble == sda->valueint && scl->valuedouble == scl->valueint &&
                 sda->valueint >= 0 && sda->valueint <= 31 &&
                 scl->valueint >= 0 && scl->valueint <= 31;
    int sda_gpio = valid ? sda->valueint : -1;
    int scl_gpio = valid ? scl->valueint : -1;
    cJSON_Delete(json);
    if (!valid) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "SDA and SCL must be GPIO numbers from 0 to 31");
        return ESP_FAIL;
    }

    char error[160];
    if (bc250_config_validate_i2c_pins(bc250_config_get(), sda_gpio, scl_gpio,
                                       error, sizeof(error)) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, error);
        return ESP_FAIL;
    }

    uint8_t addresses[BC250_I2C_MAX_SCAN_ADDRESSES];
    size_t count = 0;
    esp_err_t err = bc250_i2c_service_scan(sda_gpio, scl_gpio, addresses, sizeof(addresses), &count);
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "The active I2C bus uses different GPIOs or is unavailable");
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            err == ESP_ERR_TIMEOUT ? "I2C bus timed out; check wiring and pull-ups" : esp_err_to_name(err));
        return ESP_FAIL;
    }
    cJSON *response = cJSON_CreateObject();
    cJSON *found = response ? cJSON_AddArrayToObject(response, "addresses") : NULL;
    if (found != NULL) {
        for (size_t i = 0; i < count; ++i) {
            cJSON *address = cJSON_CreateNumber(addresses[i]);
            if (address == NULL) break;
            cJSON_AddItemToArray(found, address);
        }
    }
    char *output = found && (size_t)cJSON_GetArraySize(found) == count ?
                   cJSON_PrintUnformatted(response) : NULL;
    cJSON_Delete(response);
    if (output == NULL) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json");
    err = httpd_resp_sendstr(request, output);
    cJSON_free(output);
    return err;
}

static esp_err_t zigbee_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    char *body = NULL;
    if (receive_body(request, &body, 128) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid Zigbee action");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    cJSON *action = json ? cJSON_GetObjectItemCaseSensitive(json, "action") : NULL;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(action)) {
        if (strcmp(action->valuestring, "commission") == 0) err = bc250_zigbee_commission();
        else if (strcmp(action->valuestring, "reset") == 0) err = bc250_zigbee_factory_reset();
    }
    cJSON_Delete(json);
    if (err != ESP_OK) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Zigbee action unavailable");
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

static void factory_reset_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(750));
    ESP_LOGW(TAG, "HTTP factory reset requested; erasing all settings and Zigbee state");
    esp_err_t err = nvs_flash_erase();
    if (err == ESP_OK) esp_restart();
    ESP_LOGE(TAG, "Factory reset failed: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
}

static esp_err_t factory_reset_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    if (!bc250_wifi_is_config_ap()) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Open the configuration AP first");
        return ESP_FAIL;
    }
    char *body = NULL;
    if (receive_body(request, &body, 128) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Confirmation required");
        return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    cJSON *confirm = json ? cJSON_GetObjectItemCaseSensitive(json, "confirm") : NULL;
    bool confirmed = cJSON_IsString(confirm) && strcmp(confirm->valuestring, "ERASE ALL") == 0;
    cJSON_Delete(json);
    if (!confirmed) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Confirmation required");
        return ESP_FAIL;
    }
    if (xTaskCreate(factory_reset_task, "factory_reset", 3072, NULL, 2, NULL) != pdPASS) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to start reset");
        return ESP_FAIL;
    }
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_sendstr(request, "{\"accepted\":true,\"rebooting\":true}");
}

static void events_task(void *arg)
{
    httpd_req_t *request = arg;
    httpd_resp_set_type(request, "text/event-stream");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(request, "Connection", "keep-alive");
    bc250_power_state_t previous_state = BC250_POWER_UNKNOWN;
    bool previous_sense = !bc250_power_service_sensed_on();
    int64_t started = esp_timer_get_time();
    int64_t keepalive_at = started;
    while (esp_timer_get_time() - started < 60000000LL) {
        bc250_power_state_t state = bc250_power_service_state();
        bool sensed = bc250_power_service_sensed_on();
        if (state != previous_state || sensed != previous_sense) {
            char event[192];
            snprintf(event, sizeof(event),
                     "event: status\ndata: {\"power_state\":\"%s\",\"sensed_on\":%s}\n\n",
                     bc250_power_state_name(state), sensed ? "true" : "false");
            if (httpd_resp_send_chunk(request, event, HTTPD_RESP_USE_STRLEN) != ESP_OK) break;
            previous_state = state;
            previous_sense = sensed;
            keepalive_at = esp_timer_get_time();
        } else if (esp_timer_get_time() - keepalive_at >= 10000000LL) {
            if (httpd_resp_send_chunk(request, ": keepalive\n\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) break;
            keepalive_at = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    httpd_resp_send_chunk(request, NULL, 0);
    httpd_req_async_handler_complete(request);
    portENTER_CRITICAL(&s_events_lock);
    --s_event_clients;
    portEXIT_CRITICAL(&s_events_lock);
    vTaskDelete(NULL);
}

static esp_err_t events_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    portENTER_CRITICAL(&s_events_lock);
    bool available = s_event_clients < 2;
    if (available) ++s_event_clients;
    portEXIT_CRITICAL(&s_events_lock);
    if (!available) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        httpd_resp_sendstr(request, "Too many event clients");
        return ESP_FAIL;
    }
    httpd_req_t *copy = NULL;
    if (httpd_req_async_handler_begin(request, &copy) != ESP_OK ||
        xTaskCreate(events_task, "web_events", 4096, copy, 3, NULL) != pdPASS) {
        if (copy != NULL) httpd_req_async_handler_complete(copy);
        portENTER_CRITICAL(&s_events_lock);
        --s_event_clients;
        portEXIT_CRITICAL(&s_events_lock);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t ota_handler(httpd_req_t *request)
{
    if (!require_auth(request)) return ESP_OK;
    esp_err_t err = bc250_ota_handle_http(request);
    if (err == ESP_OK) xTaskCreate(restart_task, "ota_restart", 2048, NULL, 2, NULL);
    return err;
}

static esp_err_t captive_handler(httpd_req_t *request)
{
    if (!bc250_wifi_is_config_ap()) return root_handler(request);
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
    return httpd_resp_send(request, NULL, 0);
}

esp_err_t bc250_web_server_start(void)
{
    if (s_server != NULL) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 17;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "HTTP server start");
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/api/v1/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/v1/wifi/ap/close", .method = HTTP_POST, .handler = close_ap_handler},
        {.uri = "/api/v1/power", .method = HTTP_POST, .handler = power_handler},
        {.uri = "/api/v1/config", .method = HTTP_GET, .handler = config_get_handler},
        {.uri = "/api/v1/config", .method = HTTP_PUT, .handler = config_put_handler},
        {.uri = "/api/v1/ble/scan", .method = HTTP_POST, .handler = ble_scan_post_handler},
        {.uri = "/api/v1/ble/scan", .method = HTTP_GET, .handler = ble_scan_get_handler},
        {.uri = "/api/v1/i2c/scan", .method = HTTP_POST, .handler = i2c_scan_handler},
        {.uri = "/api/v1/zigbee", .method = HTTP_POST, .handler = zigbee_handler},
        {.uri = "/api/v1/factory-reset", .method = HTTP_POST, .handler = factory_reset_handler},
        {.uri = "/api/v1/events", .method = HTTP_GET, .handler = events_handler},
        {.uri = "/api/v1/update", .method = HTTP_POST, .handler = ota_handler},
        {.uri = "/generate_204", .method = HTTP_GET, .handler = captive_handler},
        {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_handler},
        {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_handler},
        {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_handler},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(s_server, &handlers[i]));
    }
    ESP_LOGI(TAG, "Web interface started");
    return ESP_OK;
}
