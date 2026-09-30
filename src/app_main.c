#include <inttypes.h>
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
#include "serial_service.h"
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

static void log_boot_config(const bc250_config_t *config, bool first_boot, bool pending,
                            bool config_valid, bool config_ap)
{
    static const char *const strategies[] = {
        "ps_on_only", "button_only", "ps_on_then_button", "simultaneous",
    };
    static const char *const match_types[] = {
        "address", "name_exact", "name_prefix", "service_uuid", "manufacturer_data",
    };
    ESP_LOGI(TAG, "Mode: %s; radio profile: %s", config_ap ? "configuration AP" : "normal",
             bc250_radio_profile_name(config->radio_profile));
    ESP_LOGI(TAG, "Configuration: %s; configured: %s; validation: %s; hostname: %.31s",
             first_boot ? "defaults (first boot)" : pending ? "pending" : "saved active",
             config->configured ? "yes" : "no", config_valid ? "valid" : "invalid", config->hostname);
    ESP_LOGI(TAG, "Wi-Fi SSID: %.32s; password: %s", config->wifi_ssid[0] ? config->wifi_ssid : "(unset)",
             config->wifi_password[0] ? "set" : "unset");
    ESP_LOGI(TAG, "Zigbee: %s; preferred channel: %u (0=auto); model: %.32s",
             config->radio_profile == BC250_RADIO_WIFI ? "disabled" : "router",
             config->zigbee_channel, config->zigbee_model);
    ESP_LOGI(TAG, "GPIOs: PS_ON=%d (%s), power button=%d (%s), power sense=%d (%s), status LED=%d",
             config->ps_on.gpio, config->ps_on.active_high ? "active high" : "active low",
             config->power_button.gpio, config->power_button.active_high ? "active high" : "active low",
             config->power_sense.gpio, config->power_sense.active_high ? "active high" : "active low",
             config->status_led.gpio);
    unsigned strategy = (unsigned)config->timing.strategy;
    ESP_LOGI(TAG, "Power startup: %s; output delay=%" PRIu32 " ms; button pulse=%" PRIu32
             " ms; handoff=%" PRIu32 " ms",
             strategy < sizeof(strategies) / sizeof(strategies[0]) ? strategies[strategy] : "invalid",
             config->timing.inter_output_delay_ms, config->timing.button_pulse_ms,
             config->timing.handoff_delay_ms);
    ESP_LOGI(TAG, "Hold PS_ON while power is detected: %s", config->hold_ps_on ? "enabled" : "disabled");
    ESP_LOGI(TAG, "Power timeouts: start=%" PRIu32 " ms; shutdown=%" PRIu32
             " ms; force-off=%" PRIu32 " ms; retry cooldown=%" PRIu32 " ms; sense on/off=%u/%u ms",
             config->timing.start_timeout_ms, config->timing.shutdown_timeout_ms,
             config->timing.force_off_ms, config->timing.retry_cooldown_ms,
             config->sense_on_ms, config->sense_off_ms);
    unsigned enabled = 0;
    for (unsigned i = 0; i < config->ble_device_count && i < BC250_MAX_BLE_DEVICES; ++i) {
        if (config->ble_devices[i].enabled) ++enabled;
    }
    ESP_LOGI(TAG, "Controllers on file: %u (%u enabled); BLE scan interval/window=%u/%u ms; absence=%" PRIu32 " ms",
             config->ble_device_count, enabled, config->ble_scan_interval_ms,
             config->ble_scan_window_ms, config->ble_absent_ms);
    for (unsigned i = 0; i < config->ble_device_count && i < BC250_MAX_BLE_DEVICES; ++i) {
        const bc250_ble_device_config_t *device = &config->ble_devices[i];
        unsigned type = (unsigned)device->type;
        ESP_LOGI(TAG, "Controller on file [%u]: %.31s; %s; match=%s; value=%.64s; min RSSI=%d dBm",
                 i, device->label[0] ? device->label : "(unnamed)", device->enabled ? "enabled" : "disabled",
                 type < sizeof(match_types) / sizeof(match_types[0]) ? match_types[type] : "invalid",
                 device->value, device->min_rssi);
    }
    for (unsigned i = 0; i < config->button_count && i < BC250_MAX_BUTTONS; ++i) {
        const bc250_button_config_t *button = &config->buttons[i];
        ESP_LOGI(TAG, "Button [%u]: %s; GPIO=%d; short=%s; double=%s; long=%s",
                 i, button->enabled ? "enabled" : "disabled", button->input.gpio,
                 bc250_button_action_name(button->short_action), bc250_button_action_name(button->double_action),
                 bc250_button_action_name(button->long_action));
    }
    ESP_LOGI(TAG, "PSU I2C: %s; SDA=%d; SCL=%d; address=0x%02x; poll=%" PRIu32 " ms",
             config->psu_i2c.enabled ? "enabled" : "disabled", config->psu_i2c.sda_gpio,
             config->psu_i2c.scl_gpio, config->psu_i2c.address, config->psu_i2c.poll_interval_ms);
}

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
                            config->radio_profile == BC250_RADIO_WIFI;
    if (s_boot_config_valid && (!station_required || bc250_wifi_is_connected())) {
        ESP_LOGI(TAG, "Configuration health check passed");
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
    ESP_LOGI(TAG, "Dispatching action: %s", bc250_button_action_name(action));
    esp_err_t err = ESP_OK;
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
        err = bc250_wifi_open_config_ap();
        break;
    case BC250_BUTTON_ACTION_ZIGBEE_COMMISSION:
        err = bc250_zigbee_commission();
        break;
    case BC250_BUTTON_ACTION_ZIGBEE_RESET:
        err = bc250_zigbee_factory_reset();
        break;
    case BC250_BUTTON_ACTION_NONE:
    default:
        break;
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "Action %s failed: %s", bc250_button_action_name(action), esp_err_to_name(err));
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
        case BC250_EVENT_BLE_ARRIVED: {
            const bc250_config_t *config = bc250_config_get();
            unsigned index = event.data.ble_device_index;
            const char *label = index < config->ble_device_count && index < BC250_MAX_BLE_DEVICES &&
                                config->ble_devices[index].label[0] ? config->ble_devices[index].label : "(unnamed)";
            if (bc250_power_service_sensed_on()) {
                ESP_LOGI(TAG, "Controller [%u] %.31s: power-on skipped; power sense is already on", index, label);
            } else if (bc250_power_service_request(BC250_POWER_ACTION_ON)) {
                ESP_LOGI(TAG, "Controller [%u] %.31s: power-on queued; power sense is off", index, label);
            } else {
                ESP_LOGW(TAG, "Controller [%u] %.31s: power-on not queued; power service unavailable or queue full", index, label);
            }
            break;
        }
        case BC250_EVENT_POWER_STATE_CHANGED:
            bc250_zigbee_update_power_state(event.data.power_state);
            break;
        case BC250_EVENT_OPEN_CONFIG_AP:
            dispatch_button_action(BC250_BUTTON_ACTION_CONFIG_AP);
            break;
        case BC250_EVENT_ZIGBEE_COMMISSION:
            dispatch_button_action(BC250_BUTTON_ACTION_ZIGBEE_COMMISSION);
            break;
        case BC250_EVENT_ZIGBEE_RESET:
            dispatch_button_action(BC250_BUTTON_ACTION_ZIGBEE_RESET);
            break;
        default:
            ESP_LOGW(TAG, "Unknown application event: %d", event.type);
            break;
        }
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requires reinitialization: %s; erasing saved data", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    ESP_LOGI(TAG, "BC250 Controller %s starting", BC250_VERSION);
    ESP_LOGI(TAG, "Reset reason: %d", esp_reset_reason());
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(bc250_app_events_init());

    bool first_boot = false;
    bool pending = false;
    ESP_ERROR_CHECK(bc250_config_store_init(&first_boot, &pending));
    const bc250_config_t *config = bc250_config_get();
    bool triple_reset = detect_triple_reset();
    bool recovery = bc250_config_recovery_required();
    bool force_ap = triple_reset || first_boot || !config->configured || recovery;

    char validation_error[160];
    bool config_valid = bc250_config_validate(config, validation_error, sizeof(validation_error)) == ESP_OK;
    s_boot_config_valid = config_valid;
    log_boot_config(config, first_boot, pending, config_valid,
                    force_ap || !config_valid ||
                    (config->radio_profile == BC250_RADIO_WIFI && !config->wifi_ssid[0]));
    if (triple_reset) ESP_LOGW(TAG, "Configuration AP requested by triple reset");
    if (first_boot || !config->configured) ESP_LOGI(TAG, "Configuration AP required: initial setup incomplete");
    if (recovery) ESP_LOGW(TAG, "Configuration AP required: pending configuration rolled back");
    char pin_warning[192];
    if (bc250_config_pin_warnings(config, pin_warning, sizeof(pin_warning))) {
        ESP_LOGW(TAG, "%s", pin_warning);
    }
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
    /* Decide AP mode before starting the Zigbee router on the shared radio. */
    ESP_ERROR_CHECK(bc250_wifi_service_start(config, force_ap));
    if (config_valid && config->configured) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bc250_zigbee_service_start(config));
    } else {
        ESP_LOGI(TAG, "Zigbee startup skipped: configuration %s", config_valid ? "not completed" : "invalid");
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(bc250_serial_service_start());

    xTaskCreate(dispatcher_task, "dispatcher", 4096, NULL, 7, NULL);
    xTaskCreate(healthy_task, "healthy", 3072, NULL, 2, NULL);

    ESP_LOGI(TAG, "Ready%s%s", bc250_wifi_is_config_ap() ? " in configuration mode" : "",
             pending ? " with pending configuration" : "");
}
