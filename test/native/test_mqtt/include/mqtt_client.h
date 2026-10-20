#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERR_NO_MEM 0x101
#define ESP_EVENT_ANY_ID (-1)
#define MQTT_TRANSPORT_OVER_TCP 1
#define MQTT_PROTOCOL_V_3_1_1 2
#define MQTT_EVENT_CONNECTED 1
#define MQTT_EVENT_DISCONNECTED 2
#define MQTT_EVENT_DATA 3
typedef const char *esp_event_base_t;
typedef void *esp_mqtt_client_handle_t;
typedef void (*mqtt_handler_t)(void *, esp_event_base_t, int32_t, void *);
typedef struct {
    char *topic, *data;
    int topic_len, data_len, total_data_len, current_data_offset;
    bool retain;
} esp_mqtt_event_t, *esp_mqtt_event_handle_t;
typedef struct {
    struct { struct { const char *hostname; unsigned port; int transport; } address; } broker;
    struct { const char *client_id, *username; struct { const char *password; } authentication; } credentials;
    struct { int protocol_ver, keepalive; struct { const char *topic, *msg; int qos; bool retain; } last_will; } session;
    struct { int reconnect_timeout_ms, timeout_ms; } network;
    struct { int size; } buffer;
    struct { uint64_t limit; } outbox;
} esp_mqtt_client_config_t;
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t, int, mqtt_handler_t, void *);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t);
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t, const char *, const char *, int, int, int, bool);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char *, int);
int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t);
