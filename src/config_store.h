#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "core/power_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_CONFIG_SCHEMA_VERSION 1U
#define BC250_MAX_BUTTONS 8
#define BC250_MAX_BLE_DEVICES 16
#define BC250_GPIO_DISABLED (-1)

typedef enum {
    BC250_RADIO_WIFI = 0,
    BC250_RADIO_ZIGBEE,
    BC250_RADIO_HYBRID,
} bc250_radio_profile_t;

typedef enum {
    BC250_BUTTON_ACTION_NONE = 0,
    BC250_BUTTON_ACTION_ON,
    BC250_BUTTON_ACTION_OFF,
    BC250_BUTTON_ACTION_TOGGLE,
    BC250_BUTTON_ACTION_FORCE_OFF,
    BC250_BUTTON_ACTION_CONFIG_AP,
    BC250_BUTTON_ACTION_ZIGBEE_COMMISSION,
    BC250_BUTTON_ACTION_ZIGBEE_RESET,
} bc250_button_action_t;

typedef enum {
    BC250_BLE_MATCH_ADDRESS = 0,
    BC250_BLE_MATCH_NAME_EXACT,
    BC250_BLE_MATCH_NAME_PREFIX,
    BC250_BLE_MATCH_SERVICE_UUID,
    BC250_BLE_MATCH_MANUFACTURER_DATA,
} bc250_ble_match_type_t;

typedef struct {
    int8_t gpio;
    bool active_high;
} bc250_output_config_t;

typedef struct {
    int8_t gpio;
    bool active_high;
    bool pull_up;
    uint16_t debounce_ms;
} bc250_input_config_t;

typedef struct {
    bool enabled;
    bc250_input_config_t input;
    uint16_t double_press_ms;
    uint16_t long_press_ms;
    bc250_button_action_t short_action;
    bc250_button_action_t double_action;
    bc250_button_action_t long_action;
} bc250_button_config_t;

typedef struct {
    bool enabled;
    bc250_ble_match_type_t type;
    char label[32];
    char value[65];
    char mask[65];
    int8_t min_rssi;
} bc250_ble_device_config_t;

typedef struct {
    uint32_t schema_version;
    uint32_t crc32;
    bool configured;
    bool advanced_gpio_override;
    bc250_radio_profile_t radio_profile;
    char hostname[32];
    char wifi_ssid[33];
    char wifi_password[65];
    char ap_password[17];
    uint8_t admin_salt[16];
    uint8_t admin_hash[32];
    bc250_output_config_t ps_on;
    bc250_output_config_t power_button;
    bc250_input_config_t power_sense;
    bc250_output_config_t status_led;
    bc250_power_timing_t timing;
    uint16_t sense_on_ms;
    uint16_t sense_off_ms;
    uint16_t ble_scan_interval_ms;
    uint16_t ble_scan_window_ms;
    uint32_t ble_absent_ms;
    uint8_t button_count;
    bc250_button_config_t buttons[BC250_MAX_BUTTONS];
    uint8_t ble_device_count;
    bc250_ble_device_config_t ble_devices[BC250_MAX_BLE_DEVICES];
    uint8_t zigbee_channel;
} bc250_config_t;

esp_err_t bc250_config_store_init(bool *first_boot, bool *using_pending);
const bc250_config_t *bc250_config_get(void);
void bc250_config_defaults(bc250_config_t *config);
esp_err_t bc250_config_validate(const bc250_config_t *config, char *error, size_t error_size);
esp_err_t bc250_config_save_pending(const bc250_config_t *config);
esp_err_t bc250_config_mark_healthy(void);
esp_err_t bc250_config_factory_reset(void);
bool bc250_config_pin_is_safe(int gpio);
bool bc250_config_password_verify(const char *password);
void bc250_config_set_admin_password(bc250_config_t *config, const char *password);
char *bc250_config_to_json(const bc250_config_t *config, bool include_secrets);
esp_err_t bc250_config_patch_json(bc250_config_t *config, const char *json,
                                  char *error, size_t error_size);
const char *bc250_radio_profile_name(bc250_radio_profile_t profile);
const char *bc250_button_action_name(bc250_button_action_t action);

#ifdef __cplusplus
}
#endif

