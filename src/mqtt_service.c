#include "mqtt_service.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "power_service.h"
#include "status_service.h"
#include "wifi_service.h"

#define REFRESH_STATE 1U
#define SETUP_DISCOVERY 2U
#define SUBSCRIBE_TOPICS 4U

static const char *TAG = "mqtt";
static const bc250_config_t *s_config;
static esp_mqtt_client_handle_t s_client;
static TaskHandle_t s_task;
static atomic_bool s_connected;
static char s_id[32];
static char s_state_topic[64];
static char s_command_topic[64];
static char s_availability_topic[64];
static char s_birth_topic[80];

static bool publish(const char *topic, const char *payload)
{
    return esp_mqtt_client_enqueue(s_client, topic, payload, 0, 1, true, true) >= 0;
}

static bool discover(const char *component, const char *key, const char *name,
                     const char *value_template, const char *device_class, const char *unit)
{
    char topic[160], unique_id[80];
    snprintf(topic, sizeof(topic), "%s/%s/%s/%s/config",
             s_config->mqtt.discovery_prefix, component, s_id, key);
    snprintf(unique_id, sizeof(unique_id), "%s_%s", s_id, key);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return false;
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "unique_id", unique_id);
    cJSON_AddStringToObject(root, "availability_topic", s_availability_topic);
    cJSON_AddNumberToObject(root, "qos", 1);
    cJSON *device = cJSON_AddObjectToObject(root, "device");
    cJSON *ids = cJSON_AddArrayToObject(device, "identifiers");
    cJSON_AddItemToArray(ids, cJSON_CreateString(s_id));
    cJSON_AddStringToObject(device, "name", s_config->hostname);
    cJSON_AddStringToObject(device, "manufacturer", "ChokunPlayZ");
    cJSON_AddStringToObject(device, "model", "BC-250 Controller");
    cJSON_AddStringToObject(device, "sw_version", BC250_VERSION);
    if (strcmp(component, "button") == 0) {
        cJSON_AddStringToObject(root, "command_topic", s_command_topic);
        cJSON_AddStringToObject(root, "payload_press", "FORCE_OFF");
        cJSON_AddStringToObject(root, "entity_category", "config");
    } else {
        cJSON_AddStringToObject(root, "state_topic", s_state_topic);
        cJSON_AddStringToObject(root, "value_template", value_template);
    }
    if (strcmp(component, "switch") == 0) {
        cJSON_AddStringToObject(root, "command_topic", s_command_topic);
        cJSON_AddStringToObject(root, "payload_on", "ON");
        cJSON_AddStringToObject(root, "payload_off", "OFF");
        cJSON_AddBoolToObject(root, "retain", false);
    }
    if (device_class) cJSON_AddStringToObject(root, "device_class", device_class);
    if (unit) {
        cJSON_AddStringToObject(root, "unit_of_measurement", unit);
        cJSON_AddStringToObject(root, "state_class", "measurement");
    }
    /* A PSU that stops replying must not leave its last reading looking current. */
    if (strncmp(key, "psu_", 4) == 0) {
        cJSON_DeleteItemFromObjectCaseSensitive(root, "availability_topic");
        cJSON *availability = cJSON_AddArrayToObject(root, "availability");
        cJSON *controller = cJSON_CreateObject();
        cJSON_AddStringToObject(controller, "topic", s_availability_topic);
        cJSON_AddItemToArray(availability, controller);
        cJSON *psu = cJSON_CreateObject();
        cJSON_AddStringToObject(psu, "topic", s_state_topic);
        cJSON_AddStringToObject(psu, "value_template",
                               "{{ 'online' if value_json.psu_i2c.available else 'offline' }}");
        cJSON_AddItemToArray(availability, psu);
        cJSON_AddStringToObject(root, "availability_mode", "all");
    }
    char *json = cJSON_PrintUnformatted(root);
    bool ok = json != NULL && publish(topic, json);
    cJSON_free(json);
    cJSON_Delete(root);
    return ok;
}

static bool publish_discovery(void)
{
    if (!discover("switch", "power", "Power", "{{ value_json.power }}", NULL, NULL) ||
        !discover("button", "force_off", "Force off", NULL, NULL, NULL) ||
        !discover("sensor", "power_state", "Power state", "{{ value_json.power_state }}", NULL, NULL)) {
        return false;
    }
    static const struct {
        const char *key, *name, *value_template, *device_class, *unit;
    } sensors[] = {
        {"psu_input_voltage", "PSU input voltage", "{{ value_json.psu_i2c.input_voltage_v | default(none) }}", "voltage", "V"},
        {"psu_input_current", "PSU input current", "{{ value_json.psu_i2c.input_current_a | default(none) }}", "current", "A"},
        {"psu_output_voltage", "PSU output voltage", "{{ value_json.psu_i2c.output_voltage_v | default(none) }}", "voltage", "V"},
        {"psu_output_current", "PSU output current", "{{ value_json.psu_i2c.output_current_a | default(none) }}", "current", "A"},
        {"psu_temperature", "PSU temperature", "{{ value_json.psu_i2c.internal_temperature_c | default(none) }}", "temperature", "°C"},
        {"psu_fan", "PSU fan raw", "{{ value_json.psu_i2c.fan_speed_raw | default(none) }}", NULL, NULL},
    };
    for (size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); ++i) {
        if (s_config->psu_i2c.enabled) {
            if (!discover("sensor", sensors[i].key, sensors[i].name, sensors[i].value_template,
                          sensors[i].device_class, sensors[i].unit)) return false;
        } else {
            /* Remove retained optional entities when monitoring is disabled. */
            char topic[160];
            snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config",
                     s_config->mqtt.discovery_prefix, s_id, sensors[i].key);
            if (!publish(topic, "")) return false;
        }
    }
    return true;
}

static bool matches(const char *data, int length, const char *expected)
{
    return data != NULL && length >= 0 && (size_t)length == strlen(expected) &&
           memcmp(data, expected, length) == 0;
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t event = data;
    switch (id) {
    case MQTT_EVENT_CONNECTED:
        atomic_store(&s_connected, true);
        xTaskNotify(s_task, SETUP_DISCOVERY | SUBSCRIBE_TOPICS, eSetBits);
        ESP_LOGI(TAG, "Broker connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        atomic_store(&s_connected, false);
        ESP_LOGW(TAG, "Broker disconnected; reconnecting automatically");
        break;
    case MQTT_EVENT_DATA:
        /* Commands are short and must arrive in one complete, non-retained message. */
        if (event->current_data_offset != 0 || event->data_len != event->total_data_len) break;
        if (matches(event->topic, event->topic_len, s_birth_topic) &&
            matches(event->data, event->data_len, "online")) {
            xTaskNotify(s_task, SETUP_DISCOVERY, eSetBits);
        } else if (!event->retain && bc250_wifi_is_connected() &&
                   matches(event->topic, event->topic_len, s_command_topic)) {
            bc250_power_action_t action = BC250_POWER_ACTION_NONE;
            if (matches(event->data, event->data_len, "ON")) action = BC250_POWER_ACTION_ON;
            else if (matches(event->data, event->data_len, "OFF")) action = BC250_POWER_ACTION_OFF;
            else if (matches(event->data, event->data_len, "FORCE_OFF")) action = BC250_POWER_ACTION_FORCE_OFF;
            if (action != BC250_POWER_ACTION_NONE) {
                bool queued = bc250_power_service_request(action);
                ESP_LOGI(TAG, "Power command %d: %s", action, queued ? "queued" : "queue unavailable");
                bc250_mqtt_refresh();
            }
        }
        break;
    default:
        break;
    }
}

static void mqtt_task(void *arg)
{
    (void)arg;
    /* Do not connect from first setup or a recovery AP before the station has an IP. */
    while (!bc250_wifi_is_connected()) vTaskDelay(pdMS_TO_TICKS(1000));
    while (esp_mqtt_client_start(s_client) != ESP_OK) vTaskDelay(pdMS_TO_TICKS(5000));
    bool setup_pending = false;
    bool subscriptions_pending = false;
    while (true) {
        uint32_t flags = 0;
        xTaskNotifyWait(0, UINT32_MAX, &flags, pdMS_TO_TICKS(5000));
        if (flags & SETUP_DISCOVERY) setup_pending = true;
        if (flags & SUBSCRIBE_TOPICS) subscriptions_pending = true;
        if (!bc250_mqtt_is_connected() || !bc250_wifi_is_connected()) continue;
        /* Bound queued telemetry when a broker stops acknowledging publishes. */
        if (esp_mqtt_client_get_outbox_size(s_client) > 16384) continue;
        if (subscriptions_pending) {
            if (esp_mqtt_client_subscribe(s_client, s_command_topic, 1) < 0 ||
                esp_mqtt_client_subscribe(s_client, s_birth_topic, 1) < 0) continue;
            subscriptions_pending = false;
        }
        if (setup_pending) {
            if (!publish_discovery()) continue;
            setup_pending = false;
        }
        char *json = bc250_status_json();
        if (json != NULL) {
            if (publish(s_state_topic, json)) publish(s_availability_topic, "online");
            cJSON_free(json);
        }
    }
}

esp_err_t bc250_mqtt_service_start(const bc250_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    if (!config->mqtt.enabled || !config->configured || config->radio_profile != BC250_RADIO_WIFI) return ESP_OK;
    if (s_task != NULL) return ESP_OK;
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    s_config = config;
    snprintf(s_id, sizeof(s_id), "bc250_%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_state_topic, sizeof(s_state_topic), "bc250/%s/state", s_id);
    snprintf(s_command_topic, sizeof(s_command_topic), "bc250/%s/set", s_id);
    snprintf(s_availability_topic, sizeof(s_availability_topic), "bc250/%s/availability", s_id);
    snprintf(s_birth_topic, sizeof(s_birth_topic), "%s/status", config->mqtt.discovery_prefix);
    const esp_mqtt_client_config_t client_config = {
        .broker.address = {.hostname = config->mqtt.host, .port = config->mqtt.port,
                           .transport = MQTT_TRANSPORT_OVER_TCP},
        .credentials = {.client_id = s_id, .username = config->mqtt.username[0] ? config->mqtt.username : NULL,
                        .authentication.password = config->mqtt.password[0] ? config->mqtt.password : NULL},
        .session = {.protocol_ver = MQTT_PROTOCOL_V_3_1_1, .keepalive = 30,
                    .last_will = {.topic = s_availability_topic, .msg = "offline", .qos = 1, .retain = true}},
        .network = {.reconnect_timeout_ms = 5000, .timeout_ms = 5000},
        .buffer.size = 2048,
        .outbox.limit = 24576,
    };
    s_client = esp_mqtt_client_init(&client_config);
    if (s_client == NULL) return ESP_ERR_NO_MEM;
    err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    if (err == ESP_OK && xTaskCreate(mqtt_task, "mqtt_state", 4096, NULL, 3, &s_task) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    return err;
}

bool bc250_mqtt_is_connected(void) { return atomic_load(&s_connected); }
void bc250_mqtt_refresh(void)
{
    if (s_task != NULL) xTaskNotify(s_task, REFRESH_STATE, eSetBits);
}
