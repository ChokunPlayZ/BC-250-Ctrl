#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "config_store.h"

static bc250_config_t fixture(void)
{
    return (bc250_config_t) {
        .schema_version = BC250_CONFIG_SCHEMA_VERSION,
        .configured = true,
        .radio_profile = BC250_RADIO_ZIGBEE,
        .ps_on.gpio = 0, .power_button.gpio = 1, .power_sense.gpio = 6,
        .status_led.gpio = -1,
        .timing = {.strategy = BC250_START_PS_ON_THEN_BUTTON, .button_pulse_ms = 200,
                   .force_off_ms = 5000, .start_timeout_ms = 15000, .shutdown_timeout_ms = 20000},
        .sense_on_ms = 500, .sense_off_ms = 2000,
        .ble_scan_interval_ms = 100, .ble_scan_window_ms = 50, .ble_absent_ms = 10000,
        .psu_i2c = {.poll_interval_ms = 1000, .address = 0x5f, .sda_gpio = -1, .scl_gpio = -1},
    };
}

int main(void)
{
    char error[192], warning[192];
    bc250_config_t config = fixture();
    assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
    assert(!bc250_config_pin_warnings(&config, warning, sizeof(warning)));
    config.radio_profile = BC250_RADIO_LEGACY_HYBRID;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    bc250_config_t migrated = config;
    migrated.radio_profile = BC250_RADIO_ZIGBEE;
    assert(bc250_config_migrate_legacy_profile(&config));
    assert(memcmp(&config, &migrated, sizeof(config)) == 0);
    assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
    assert(!bc250_config_migrate_legacy_profile(&config));
    config.radio_profile = BC250_RADIO_WIFI;
    assert(!bc250_config_migrate_legacy_profile(&config));
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    strcpy(config.wifi_ssid, "test-network");
    assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
    config = fixture();
    /* Board guidance stays advisory; known boot-breaking C5 pins are excluded. */
    for (int gpio = 0; gpio <= 31; ++gpio) {
        config = fixture();
        config.status_led.gpio = gpio;
        if (gpio == 0 || gpio == 1 || gpio == 6) continue;
        if (bc250_config_pin_is_blocked(gpio)) {
            assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
            assert(strstr(error, "unavailable"));
            continue;
        }
        assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
        assert(bc250_config_pin_warnings(&config, warning, sizeof(warning)) ==
               !bc250_config_pin_is_safe(gpio));
    }
    config = fixture();
    config.ps_on.gpio = 15;
    assert(!bc250_config_pin_is_safe(15));
    assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
    assert(bc250_config_pin_warnings(&config, warning, sizeof(warning)));
    assert(strstr(warning, "15") && strstr(warning, "Saving is allowed"));
    config.advanced_gpio_override = true;
    assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
    assert(bc250_config_pin_warnings(&config, warning, sizeof(warning)));

    config.psu_i2c.enabled = true;
    config.psu_i2c.sda_gpio = 13; config.psu_i2c.scl_gpio = 16;
    config.button_count = 1;
    config.buttons[0] = (bc250_button_config_t) {
        .enabled = true, .input = {.gpio = 17, .debounce_ms = 40},
        .double_press_ms = 350, .long_press_ms = 1500,
    };
    assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
    assert(bc250_config_pin_warnings(&config, warning, sizeof(warning)));
    assert(strstr(warning, "13") && strstr(warning, "16") && strstr(warning, "17"));
    assert(bc250_config_validate_i2c_pins(&config, 13, 16, error, sizeof(error)) == ESP_OK);
    assert(bc250_config_validate_i2c_pins(&config, 13, 13, error, sizeof(error)) != ESP_OK);
    assert(bc250_config_validate_i2c_pins(&config, 1, 16, error, sizeof(error)) != ESP_OK);
    config.psu_i2c.scl_gpio = 13;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    config.psu_i2c.scl_gpio = 16;
    config.buttons[0].input.gpio = 15;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    for (int gpio = 12; gpio <= 14; gpio += 2) {
#if CONFIG_IDF_TARGET_ESP32C5
        const esp_err_t expected = ESP_ERR_INVALID_ARG;
        assert(bc250_config_pin_is_blocked(gpio));
#else
        const esp_err_t expected = ESP_OK;
        assert(!bc250_config_pin_is_blocked(gpio));
#endif
        /* All roles, legacy override, and scans must enforce the same exclusions. */
        for (int role = 0; role < 7; ++role) {
            config = fixture();
            config.advanced_gpio_override = true;
            switch (role) {
            case 0: config.ps_on.gpio = gpio; break;
            case 1: config.power_button.gpio = gpio; break;
            case 2: config.power_sense.gpio = gpio; break;
            case 3: config.status_led.gpio = gpio; break;
            case 4:
            case 5:
                config.psu_i2c.enabled = true;
                config.psu_i2c.sda_gpio = role == 4 ? gpio : 4;
                config.psu_i2c.scl_gpio = role == 5 ? gpio : 5;
                break;
            case 6:
                config.button_count = 1;
                config.buttons[0] = (bc250_button_config_t) {
                    .enabled = true, .input = {.gpio = gpio, .debounce_ms = 40},
                    .double_press_ms = 350, .long_press_ms = 1500,
                };
                break;
            }
            assert(bc250_config_validate(&config, error, sizeof(error)) == expected);
            if (expected != ESP_OK) assert(strstr(error, "unavailable"));
        }
        config = fixture();
        assert(bc250_config_validate_i2c_pins(&config, gpio, 5, error, sizeof(error)) == expected);
        assert(bc250_config_validate_i2c_pins(&config, 4, gpio, error, sizeof(error)) == expected);
    }
    config = fixture();
    config.power_button.gpio = -1;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    config = fixture();
    config.ps_on.gpio = 32;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    config.ps_on.gpio = -2;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    config = fixture();
    config.ble_scan_window_ms = config.ble_scan_interval_ms + 1;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    config = fixture();
    config.zigbee_channel = 5;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    config = fixture();
    config.timing.start_timeout_ms = config.timing.button_pulse_ms;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    puts("Configuration validation and GPIO advisory tests passed");
    return 0;
}
