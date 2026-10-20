#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>

/* Exercise the actual service, including its worker and event callback. */
#include "../../../src/mqtt_service.c"

static esp_mqtt_client_config_t client_config;
static void (*worker)(void *);
static mqtt_handler_t handler;
static bool wifi_connected = true, fail_task, fail_register, fail_publish;
static bool birth_on_second_wait;
static unsigned messages, actions, subscriptions, destroyed, iteration, iterations;
static uint32_t notifications;
static bc250_power_action_t last_action;
static jmp_buf worker_exit;
static struct { char topic[180], payload[2000]; } published[128];

esp_err_t esp_read_mac(uint8_t *mac, int type)
{
    assert(type == ESP_MAC_WIFI_STA);
    const uint8_t value[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    memcpy(mac, value, sizeof(value));
    return ESP_OK;
}
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config)
{
    client_config = *config;
    return (void *)1;
}
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, int id, mqtt_handler_t cb, void *arg)
{
    (void)client; (void)id; (void)arg;
    handler = cb;
    return fail_register ? ESP_ERR_NO_MEM : ESP_OK;
}
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client)
{
    (void)client;
    handler(NULL, NULL, MQTT_EVENT_CONNECTED, NULL);
    return ESP_OK;
}
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client) { (void)client; ++destroyed; return ESP_OK; }
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *handle)
{
    (void)name; (void)stack; (void)arg; (void)priority;
    if (fail_task) return pdFALSE;
    worker = task; *handle = (void *)2;
    return pdPASS;
}
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t bits, eNotifyAction action)
{
    (void)task; (void)action;
    notifications |= bits;
    return pdTRUE;
}
BaseType_t xTaskNotifyWait(uint32_t clear_entry, uint32_t clear_exit, uint32_t *bits, TickType_t wait)
{
    (void)clear_entry; (void)clear_exit; (void)wait;
    if (iteration++ >= iterations) longjmp(worker_exit, 1);
    if (birth_on_second_wait && iteration == 2) {
        esp_mqtt_event_t event = {.topic = s_birth_topic, .topic_len = (int)strlen(s_birth_topic),
            .data = "online", .data_len = 6, .total_data_len = 6, .retain = true};
        handler(NULL, NULL, MQTT_EVENT_DATA, &event);
    }
    *bits = notifications; notifications = 0;
    return pdTRUE;
}
void vTaskDelay(TickType_t delay) { (void)delay; wifi_connected = true; }
bool bc250_wifi_is_connected(void) { return wifi_connected; }
bool bc250_power_service_request(bc250_power_action_t action) { ++actions; last_action = action; return true; }
char *bc250_status_json(void) { return strdup("{\"power\":\"ON\",\"power_state\":\"on\",\"psu_i2c\":{\"available\":false}}"); }
int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t client) { (void)client; return 0; }
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos)
{
    (void)client;
    assert(qos == 1);
    assert(!strcmp(topic, s_command_topic) || !strcmp(topic, s_birth_topic));
    ++subscriptions;
    return subscriptions;
}
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t client, const char *topic, const char *data,
                            int len, int qos, int retain, bool store)
{
    (void)client;
    assert(len == 0 && qos == 1 && retain && store);
    if (fail_publish) return -2;
    assert(messages < 128);
    snprintf(published[messages].topic, sizeof(published[messages].topic), "%s", topic);
    snprintf(published[messages].payload, sizeof(published[messages].payload), "%s", data);
    return ++messages;
}
static void run_worker(unsigned count)
{
    iteration = 0; iterations = count;
    if (!setjmp(worker_exit)) worker(NULL);
}
static void receive(const char *topic, const char *payload, bool retained, int offset, int total)
{
    esp_mqtt_event_t event = {.topic = (char *)topic, .topic_len = (int)strlen(topic),
        .data = (char *)payload, .data_len = (int)strlen(payload), .retain = retained,
        .current_data_offset = offset, .total_data_len = total};
    handler(NULL, NULL, MQTT_EVENT_DATA, &event);
}
static const char *text_field(cJSON *object, const char *name)
{
    cJSON *field = cJSON_GetObjectItemCaseSensitive(object, name);
    assert(cJSON_IsString(field));
    return field->valuestring;
}
int main(void)
{
    bc250_config_t config = {.configured = true, .radio_profile = BC250_RADIO_WIFI,
        .hostname = "BC250 \"workbench\"", .mqtt = {.enabled = true, .host = "broker.local", .port = 1883,
        .username = "controller", .password = "secret", .discovery_prefix = "ha/custom"}};
    assert(bc250_mqtt_service_start(NULL) == ESP_ERR_INVALID_ARG);
    config.mqtt.enabled = false;
    assert(bc250_mqtt_service_start(&config) == ESP_OK && !worker);
    config.mqtt.enabled = true; config.radio_profile = BC250_RADIO_ZIGBEE;
    assert(bc250_mqtt_service_start(&config) == ESP_OK && !worker);
    config.radio_profile = BC250_RADIO_WIFI;
    fail_task = true;
    assert(bc250_mqtt_service_start(&config) == ESP_ERR_NO_MEM && destroyed == 1);
    fail_task = false; fail_register = true;
    assert(bc250_mqtt_service_start(&config) == ESP_ERR_NO_MEM && destroyed == 2);
    fail_register = false;
    assert(bc250_mqtt_service_start(&config) == ESP_OK);
    assert(!strcmp(client_config.credentials.client_id, "bc250_aabbccddeeff"));
    assert(!strcmp(client_config.credentials.authentication.password, "secret"));
    assert(!strcmp(client_config.session.last_will.msg, "offline"));
    assert(client_config.session.last_will.qos == 1 && client_config.session.last_will.retain);
    assert(client_config.outbox.limit == 24576);
    assert(bc250_mqtt_service_start(&config) == ESP_OK && destroyed == 2);
    wifi_connected = false;
    run_worker(1);
    assert(bc250_mqtt_is_connected() && subscriptions == 2 && messages == 11);
    cJSON *discovery = cJSON_Parse(published[0].payload);
    assert(discovery);
    assert(!strcmp(text_field(discovery, "command_topic"), "bc250/bc250_aabbccddeeff/set"));
    assert(!strcmp(text_field(discovery, "value_template"), "{{ value_json.power }}"));
    assert(!strcmp(text_field(cJSON_GetObjectItemCaseSensitive(discovery, "device"), "name"), config.hostname));
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(discovery, "retain")));
    cJSON_Delete(discovery);
    assert(!strcmp(published[0].topic, "ha/custom/switch/bc250_aabbccddeeff/power/config"));
    for (unsigned i = 3; i < 9; ++i) assert(!published[i].payload[0]);
    assert(!strcmp(published[10].payload, "online"));
    receive(s_command_topic, "ON", false, 0, 2);
    assert(actions == 1 && last_action == BC250_POWER_ACTION_ON);
    receive(s_command_topic, "OFF", false, 0, 3);
    assert(actions == 2 && last_action == BC250_POWER_ACTION_OFF);
    receive(s_command_topic, "FORCE_OFF", false, 0, 9);
    assert(actions == 3 && last_action == BC250_POWER_ACTION_FORCE_OFF);
    receive(s_command_topic, "ON", true, 0, 2);
    receive(s_command_topic, "ON", false, 1, 3);
    receive(s_command_topic, "OFF", false, 0, 3000);
    receive(s_command_topic, "OFFjunk", false, 0, 7);
    receive("unrelated/set", "ON", false, 0, 2);
    wifi_connected = false;
    receive(s_command_topic, "ON", false, 0, 2);
    wifi_connected = true;
    assert(actions == 3);
    notifications = 0;
    receive(s_birth_topic, "online", true, 0, 6);
    assert(notifications & SETUP_DISCOVERY);
    handler(NULL, NULL, MQTT_EVENT_DISCONNECTED, NULL);
    assert(!bc250_mqtt_is_connected());
    config.psu_i2c.enabled = true;
    messages = 0; fail_publish = true;
    run_worker(1);
    assert(messages == 0);
    fail_publish = false;
    run_worker(2);
    assert(messages == 13); /* discovery + immediate state + periodic state */
    discovery = cJSON_Parse(published[3].payload);
    assert(discovery);
    assert(!strcmp(text_field(discovery, "device_class"), "voltage"));
    assert(!strcmp(text_field(discovery, "availability_mode"), "all"));
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(discovery, "availability")) == 2);
    cJSON_Delete(discovery);
    unsigned previous_subscriptions = subscriptions;
    birth_on_second_wait = true; messages = 0;
    run_worker(2);
    assert(subscriptions == previous_subscriptions + 2);
    assert(messages == 22); /* HA birth republishes discovery without resubscribing to retained birth. */
    puts("MQTT discovery, reconnect, availability, and command filtering tests passed");
}
