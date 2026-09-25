#include <stdbool.h>
#include <stdint.h>

#include "app_events.h"
#include "ble_presence.h"
#include "button_service.h"
#include "config_store.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ota_service.h"
#include "power_service.h"
#include "psu_i2c_service.h"
#include "status_led.h"
#include "wifi_service.h"
#include "zigbee_service.h"

#define RAPID_RESET_MAGIC 0xBC25055AU

typedef struct {
    uint32_t magic;
    uint8_t count;
} rapid_reset_state_t;

RTC_NOINIT_ATTR static rapid_reset_state_t s_rapid_reset;
static const char *TAG = "bc250";
static bool s_boot_config_valid;

static bool detect_triple_reset(void)
{
    if (s_rapid_reset.magic != RAPID_RESET_MAGIC) {
        s_rapid_reset.magic = RAPID_RESET_MAGIC;
        s_rapid_reset.count = 0;
    }
    if (s_rapid_reset.count < UINT8_MAX) s_rapid_reset.count++;
    return s_rapid_reset.count >= 3;
}

static void healthy_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(30000));
    s_rapid_reset.count = 0;
    const bc250_config_t *config = bc250_config_get();
    bool station_required = config->configured &&
                            (config->radio_profile == BC250_RADIO_WIFI ||
                             config->radio_profile == BC250_RADIO_HYBRID);
    if (s_boot_config_valid && (!station_required || bc250_wifi_is_connected())) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bc250_config_mark_healthy());
    } else {
        ESP_LOGW(TAG, "Configuration not healthy; opening recovery AP");
        ESP_ERROR_CHECK_WITHOUT_ABORT(bc250_wifi_open_config_ap());
    }
    bc250_ota_mark_running_valid();
    ESP_LOGI(TAG, "Firmware health check complete");
    vTaskDelete(NULL);
}

static void dispatch_button_action(bc250_button_action_t action)
{
    switch (action) {
    case BC250_BUTTON_ACTION_ON:
        bc250_power_service_request(BC250_POWER_ACTION_ON);
        break;
    case BC250_BUTTON_ACTION_OFF:
        bc250_power_service_request(BC250_POWER_ACTION_OFF);
        break;
    case BC250_BUTTON_ACTION_TOGGLE:
        bc250_power_service_request(BC250_POWER_ACTION_TOGGLE);
        break;
    case BC250_BUTTON_ACTION_FORCE_OFF:
        bc250_power_service_request(BC250_POWER_ACTION_FORCE_OFF);
        break;
    case BC250_BUTTON_ACTION_CONFIG_AP:
        bc250_wifi_open_config_ap();
        break;
    case BC250_BUTTON_ACTION_ZIGBEE_COMMISSION:
        bc250_zigbee_commission();
        break;
    case BC250_BUTTON_ACTION_ZIGBEE_RESET:
        bc250_zigbee_factory_reset();
        break;
    case BC250_BUTTON_ACTION_NONE:
    default:
        break;
    }
}

static void dispatcher_task(void *arg)
{
    (void)arg;
    bc250_app_event_t event;
    while (true) {
        if (!bc250_app_event_receive(&event, 1000)) continue;
        switch (event.type) {
        case BC250_EVENT_BUTTON_ACTION:
            dispatch_button_action(event.data.button_action);
            break;
        case BC250_EVENT_BLE_ARRIVED:
            if (!bc250_power_service_sensed_on()) {
                bc250_power_service_request(BC250_POWER_ACTION_ON);
            }
            break;
        case BC250_EVENT_POWER_STATE_CHANGED:
            bc250_zigbee_update_power_state(event.data.power_state);
            break;
        case BC250_EVENT_OPEN_CONFIG_AP:
            bc250_wifi_open_config_ap();
            break;
        case BC250_EVENT_ZIGBEE_COMMISSION:
            bc250_zigbee_commission();
            break;
        case BC250_EVENT_ZIGBEE_RESET:
            bc250_zigbee_factory_reset();
            break;
        default:
            break;
        }
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    ESP_LOGI(TAG, "BC250 Controller %s starting", BC250_VERSION);
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(bc250_app_events_init());

    bool first_boot = false;
    bool pending = false;
    ESP_ERROR_CHECK(bc250_config_store_init(&first_boot, &pending));
    const bc250_config_t *config = bc250_config_get();
    bool force_ap = detect_triple_reset() || first_boot || !config->configured ||
                    bc250_config_recovery_required();

    char validation_error[160];
    bool config_valid = bc250_config_validate(config, validation_error, sizeof(validation_error)) == ESP_OK;
    s_boot_config_valid = config_valid;
    if (!config_valid) {
        ESP_LOGE(TAG, "Configuration invalid; outputs remain disabled: %s", validation_error);
        force_ap = true;
    } else {
        ESP_ERROR_CHECK(bc250_power_service_start(config));
        ESP_ERROR_CHECK(bc250_status_led_start(config));
        ESP_ERROR_CHECK(bc250_button_service_start(config));
        ESP_ERROR_CHECK_WITHOUT_ABORT(bc250_psu_i2c_service_start(config));
    }

    if (config_valid) ESP_ERROR_CHECK(bc250_ble_presence_start(config));
    if (config_valid && config->configured) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bc250_zigbee_service_start(config));
    }
    ESP_ERROR_CHECK(bc250_wifi_service_start(config, force_ap));

    xTaskCreate(dispatcher_task, "dispatcher", 4096, NULL, 7, NULL);
    xTaskCreate(healthy_task, "healthy", 3072, NULL, 2, NULL);

    ESP_LOGI(TAG, "Ready%s%s", force_ap ? " in configuration mode" : "",
             pending ? " with pending configuration" : "");
}
