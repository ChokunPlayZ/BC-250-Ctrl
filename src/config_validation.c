#include "config_store.h"

#include <stdio.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

static bool pin_in_use(const bc250_config_t *config, int gpio, int except_button)
{
    if (gpio < 0) return false;
    if (config->ps_on.gpio == gpio || config->power_button.gpio == gpio ||
        config->power_sense.gpio == gpio || config->status_led.gpio == gpio ||
        (config->psu_i2c.enabled &&
         (config->psu_i2c.sda_gpio == gpio || config->psu_i2c.scl_gpio == gpio))) {
        return true;
    }
    for (int i = 0; i < config->button_count; ++i) {
        if (i != except_button && config->buttons[i].enabled && config->buttons[i].input.gpio == gpio) {
            return true;
        }
    }
    return false;
}

bool bc250_config_pin_is_safe(int gpio)
{
#if CONFIG_IDF_TARGET_ESP32C5
    static const uint8_t safe[] = {0, 1, 4, 5, 6, 8, 9, 10, 23, 24};
#elif CONFIG_IDF_TARGET_ESP32C6
    static const uint8_t safe[] = {0, 1, 2, 3, 6, 7, 10, 11, 18, 19, 20, 21, 22, 23};
#else
    static const uint8_t safe[] = {0};
#endif
    for (size_t i = 0; i < sizeof(safe); ++i) {
        if (gpio == safe[i]) return true;
    }
    return false;
}

static esp_err_t validate_one_pin(int gpio,
                                  const char *name, char *error, size_t error_size)
{
    if (gpio == BC250_GPIO_DISABLED) return ESP_OK;
    if (gpio < 0 || gpio > 31) {
        snprintf(error, error_size, "%s GPIO is outside the supported range", name);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t bc250_config_validate_i2c_pins(const bc250_config_t *config, int sda_gpio,
                                         int scl_gpio, char *error, size_t error_size)
{
    if (config == NULL || error == NULL || error_size == 0) return ESP_ERR_INVALID_ARG;
    error[0] = '\0';
    if (sda_gpio < 0 || scl_gpio < 0 || sda_gpio == scl_gpio) {
        snprintf(error, error_size, "I2C requires distinct SDA and SCL GPIOs");
        return ESP_ERR_INVALID_ARG;
    }
    if (validate_one_pin(sda_gpio, "I2C SDA", error, error_size) != ESP_OK ||
        validate_one_pin(scl_gpio, "I2C SCL", error, error_size) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    // The active I2C pins may be scanned; all other configured GPIO roles are excluded.
    bc250_config_t other_roles = *config;
    other_roles.psu_i2c.enabled = false;
    if (pin_in_use(&other_roles, sda_gpio, -1) || pin_in_use(&other_roles, scl_gpio, -1)) {
        snprintf(error, error_size, "I2C scan pins conflict with a configured GPIO role");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t bc250_config_validate(const bc250_config_t *config, char *error, size_t error_size)
{
    if (config == NULL || error == NULL || error_size == 0) return ESP_ERR_INVALID_ARG;
    error[0] = '\0';
    if (config->schema_version != BC250_CONFIG_SCHEMA_VERSION) {
        snprintf(error, error_size, "unsupported configuration schema");
        return ESP_ERR_INVALID_VERSION;
    }
    if (config->radio_profile > BC250_RADIO_HYBRID) {
        snprintf(error, error_size, "invalid radio profile");
        return ESP_ERR_INVALID_ARG;
    }
    if (config->timing.strategy > BC250_START_SIMULTANEOUS) {
        snprintf(error, error_size, "invalid start strategy");
        return ESP_ERR_INVALID_ARG;
    }
    if (config->configured) {
        if (config->power_sense.gpio == BC250_GPIO_DISABLED) {
            snprintf(error, error_size, "power LED sense GPIO is required");
            return ESP_ERR_INVALID_ARG;
        }
        if ((config->timing.strategy == BC250_START_PS_ON_ONLY ||
             config->timing.strategy == BC250_START_PS_ON_THEN_BUTTON ||
             config->timing.strategy == BC250_START_SIMULTANEOUS) &&
            config->ps_on.gpio == BC250_GPIO_DISABLED) {
            snprintf(error, error_size, "selected start strategy requires PS_ON GPIO");
            return ESP_ERR_INVALID_ARG;
        }
        if (config->power_button.gpio == BC250_GPIO_DISABLED) {
            snprintf(error, error_size, "power-button GPIO is required for shutdown");
            return ESP_ERR_INVALID_ARG;
        }
        if ((config->radio_profile == BC250_RADIO_WIFI ||
             config->radio_profile == BC250_RADIO_HYBRID) && config->wifi_ssid[0] == '\0') {
            snprintf(error, error_size, "Wi-Fi SSID is required for this radio profile");
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (config->button_count > BC250_MAX_BUTTONS || config->ble_device_count > BC250_MAX_BLE_DEVICES) {
        snprintf(error, error_size, "too many buttons or BLE devices");
        return ESP_ERR_INVALID_SIZE;
    }
    if (config->ble_scan_window_ms == 0 || config->ble_scan_interval_ms < config->ble_scan_window_ms) {
        snprintf(error, error_size, "BLE scan window must be nonzero and not exceed the interval");
        return ESP_ERR_INVALID_ARG;
    }
    if (config->zigbee_channel != 0 &&
        (config->zigbee_channel < 11 || config->zigbee_channel > 26)) {
        snprintf(error, error_size, "Zigbee channel must be automatic (0) or 11-26");
        return ESP_ERR_INVALID_ARG;
    }
    if (config->sense_on_ms == 0 || config->sense_off_ms == 0 ||
        config->ble_absent_ms < config->ble_scan_interval_ms) {
        snprintf(error, error_size, "invalid sense or BLE presence timing");
        return ESP_ERR_INVALID_ARG;
    }
    if (config->psu_i2c.poll_interval_ms < 500 || config->psu_i2c.poll_interval_ms > 60000 ||
        config->psu_i2c.address < 0x58 || config->psu_i2c.address > 0x5f) {
        snprintf(error, error_size, "invalid PSU I2C polling interval or PIC address");
        return ESP_ERR_INVALID_ARG;
    }
    if (config->psu_i2c.enabled &&
        (config->psu_i2c.sda_gpio == BC250_GPIO_DISABLED ||
         config->psu_i2c.scl_gpio == BC250_GPIO_DISABLED)) {
        snprintf(error, error_size, "enabled PSU I2C requires SDA and SCL GPIOs");
        return ESP_ERR_INVALID_ARG;
    }
    const struct { int gpio; const char *name; } fixed[] = {
        {config->ps_on.gpio, "PS_ON"},
        {config->power_button.gpio, "power button"},
        {config->power_sense.gpio, "power sense"},
        {config->status_led.gpio, "status LED"},
        {config->psu_i2c.enabled ? config->psu_i2c.sda_gpio : BC250_GPIO_DISABLED, "PSU SDA"},
        {config->psu_i2c.enabled ? config->psu_i2c.scl_gpio : BC250_GPIO_DISABLED, "PSU SCL"},
    };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        if (validate_one_pin(fixed[i].gpio, fixed[i].name, error, error_size) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
        if (fixed[i].gpio < 0) continue;
        for (size_t j = i + 1; j < sizeof(fixed) / sizeof(fixed[0]); ++j) {
            if (fixed[i].gpio == fixed[j].gpio) {
                snprintf(error, error_size, "GPIO %d is assigned to both %s and %s",
                         fixed[i].gpio, fixed[i].name, fixed[j].name);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    for (int i = 0; i < config->button_count; ++i) {
        const bc250_button_config_t *button = &config->buttons[i];
        if (!button->enabled) continue;
        if (validate_one_pin(button->input.gpio, "input button", error, error_size) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
        if (button->input.gpio < 0 || pin_in_use(config, button->input.gpio, i)) {
            snprintf(error, error_size, "button %d uses a disabled or duplicate GPIO", i);
            return ESP_ERR_INVALID_ARG;
        }
        if (button->input.debounce_ms == 0 || button->double_press_ms == 0 ||
            button->long_press_ms <= button->input.debounce_ms) {
            snprintf(error, error_size, "button %d has invalid gesture timing", i);
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (int i = 0; i < config->ble_device_count; ++i) {
        const bc250_ble_device_config_t *device = &config->ble_devices[i];
        if (!device->enabled) continue;
        if (device->type > BC250_BLE_MATCH_MANUFACTURER_DATA || device->value[0] == '\0') {
            snprintf(error, error_size, "BLE matcher %d is incomplete", i);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (config->timing.button_pulse_ms < 50 || config->timing.force_off_ms < 1000 ||
        config->timing.start_timeout_ms <= config->timing.button_pulse_ms ||
        config->timing.shutdown_timeout_ms <= config->timing.button_pulse_ms) {
        snprintf(error, error_size, "unsafe power timing values");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

bool bc250_config_pin_warnings(const bc250_config_t *config, char *warning, size_t size)
{
    if (config == NULL || warning == NULL || size == 0) return false;
    warning[0] = '\0';
    uint32_t pins = 0;
    const int fixed[] = {config->ps_on.gpio, config->power_button.gpio,
                         config->power_sense.gpio, config->status_led.gpio,
                         config->psu_i2c.enabled ? config->psu_i2c.sda_gpio : -1,
                         config->psu_i2c.enabled ? config->psu_i2c.scl_gpio : -1};
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        if (fixed[i] >= 0 && fixed[i] <= 31) pins |= UINT32_C(1) << fixed[i];
    }
    for (int i = 0; i < config->button_count && i < BC250_MAX_BUTTONS; ++i) {
        int gpio = config->buttons[i].input.gpio;
        if (config->buttons[i].enabled && gpio >= 0 && gpio <= 31) pins |= UINT32_C(1) << gpio;
    }
    size_t used = 0;
    for (int gpio = 0; gpio <= 31; ++gpio) {
        if (!(pins & (UINT32_C(1) << gpio)) || bc250_config_pin_is_safe(gpio)) continue;
        int written = snprintf(warning + used, size - used, "%s%d", used ? ", " : "GPIOs ", gpio);
        if (written < 0 || (size_t)written >= size - used) return true;
        used += written;
    }
    if (!used) return false;
    snprintf(warning + used, size - used,
             " may be reserved or board-specific; verify your board schematic. Saving is allowed.");
    return true;
}
