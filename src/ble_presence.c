#include "ble_presence.h"

#include <assert.h>
#include <string.h>

#include "app_events.h"
#include "cJSON.h"
#include "core/ble_match.h"
#include "core/presence_logic.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define LEARN_RESULT_COUNT 24

typedef struct {
    bool used;
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[64];
    uint64_t last_seen_ms;
} learn_result_t;

static const char *TAG = "ble_presence";
static bc250_config_t s_config;
static bc250_presence_logic_t s_presence[BC250_MAX_BLE_DEVICES];
static learn_result_t s_results[LEARN_RESULT_COUNT];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_learning_until_ms;
static uint8_t s_own_address_type;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static uint16_t ms_to_scan_units(uint16_t ms)
{
    uint32_t units = ((uint32_t)ms * 1000U) / 625U;
    if (units < 4) units = 4;
    if (units > 0xffff) units = 0xffff;
    return (uint16_t)units;
}

static void remember_result(const struct ble_gap_disc_desc *disc)
{
    if (now_ms() >= s_learning_until_ms) return;
    portENTER_CRITICAL(&s_lock);
    int slot = -1;
    int oldest = 0;
    for (int i = 0; i < LEARN_RESULT_COUNT; ++i) {
        if (s_results[i].used && memcmp(s_results[i].address, disc->addr.val, 6) == 0) {
            slot = i;
            break;
        }
        if (!s_results[i].used && slot < 0) slot = i;
        if (s_results[i].last_seen_ms < s_results[oldest].last_seen_ms) oldest = i;
    }
    if (slot < 0) slot = oldest;
    learn_result_t *result = &s_results[slot];
    result->used = true;
    memcpy(result->address, disc->addr.val, 6);
    result->address_type = disc->addr.type;
    result->rssi = disc->rssi;
    result->last_seen_ms = now_ms();
    bc250_ble_extract_name(disc->data, disc->length_data, result->name, sizeof(result->name));
    portEXIT_CRITICAL(&s_lock);
}

static void process_advertisement(const struct ble_gap_disc_desc *disc)
{
    remember_result(disc);
    bc250_ble_advertisement_t advertisement = {
        .rssi = disc->rssi,
        .data = disc->data,
        .data_length = disc->length_data,
    };
    memcpy(advertisement.address, disc->addr.val, 6);

    for (int i = 0; i < s_config.ble_device_count; ++i) {
        const bc250_ble_device_config_t *matcher = &s_config.ble_devices[i];
        if (!matcher->enabled) continue;
        if (!bc250_ble_match_advertisement(matcher->type, matcher->value, matcher->mask,
                                           matcher->min_rssi, &advertisement)) {
            continue;
        }
        bool arrived = bc250_presence_logic_seen(&s_presence[i], now_ms());
        if (arrived) {
            ESP_LOGI(TAG, "BLE device arrived: %s", matcher->label);
            bc250_app_event_t event = {
                .type = BC250_EVENT_BLE_ARRIVED,
                .data.ble_device_index = i,
            };
            bc250_app_event_post(&event, 0);
        }
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        process_advertisement(&event->disc);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGW(TAG, "BLE scan stopped (%d); restarting", event->disc_complete.reason);
        break;
    default:
        break;
    }
    return 0;
}

static esp_err_t start_scan(bool active)
{
    struct ble_gap_disc_params params = {
        .itvl = ms_to_scan_units(s_config.ble_scan_interval_ms),
        .window = ms_to_scan_units(s_config.ble_scan_window_ms),
        .filter_policy = 0,
        .limited = 0,
        .passive = active ? 0 : 1,
        .filter_duplicates = 0,
    };
    int rc = ble_gap_disc(s_own_address_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset: %d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_own_address_type);
    if (rc != 0 || start_scan(false) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to start BLE scanner: %d", rc);
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void presence_task(void *arg)
{
    (void)arg;
    bool was_learning = false;
    while (true) {
        uint64_t now = now_ms();
        bool learning = now < s_learning_until_ms;
        if (learning != was_learning && ble_hs_synced()) {
            ble_gap_disc_cancel();
            start_scan(learning);
            was_learning = learning;
        }
        for (int i = 0; i < s_config.ble_device_count; ++i) {
            if (bc250_presence_logic_expire(&s_presence[i], now, s_config.ble_absent_ms)) {
                ESP_LOGI(TAG, "BLE device absent: %s", s_config.ble_devices[i].label);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t bc250_ble_presence_start(const bc250_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    memset(s_presence, 0, sizeof(s_presence));
    memset(s_results, 0, sizeof(s_results));
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    return xTaskCreate(presence_task, "ble_presence", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t bc250_ble_start_learning(uint32_t duration_ms)
{
    if (duration_ms < 1000) duration_ms = 1000;
    if (duration_ms > 60000) duration_ms = 60000;
    portENTER_CRITICAL(&s_lock);
    memset(s_results, 0, sizeof(s_results));
    s_learning_until_ms = now_ms() + duration_ms;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

char *bc250_ble_scan_results_json(void)
{
    learn_result_t snapshot[LEARN_RESULT_COUNT];
    portENTER_CRITICAL(&s_lock);
    memcpy(snapshot, s_results, sizeof(snapshot));
    portEXIT_CRITICAL(&s_lock);

    cJSON *array = cJSON_CreateArray();
    for (int i = 0; i < LEARN_RESULT_COUNT; ++i) {
        if (!snapshot[i].used) continue;
        char address[18];
        bc250_ble_format_address(snapshot[i].address, address);
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "address", address);
        cJSON_AddNumberToObject(item, "address_type", snapshot[i].address_type);
        cJSON_AddNumberToObject(item, "rssi", snapshot[i].rssi);
        cJSON_AddStringToObject(item, "name", snapshot[i].name);
        cJSON_AddItemToArray(array, item);
    }
    char *json = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);
    return json;
}

bool bc250_ble_device_present(unsigned index)
{
    return index < BC250_MAX_BLE_DEVICES && s_presence[index].present;
}
