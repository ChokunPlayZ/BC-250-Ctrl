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
    /* Every supported GPIO saves without an override, even outside board guidance. */
    for (int gpio = 0; gpio <= 31; ++gpio) {
        config = fixture();
        config.status_led.gpio = gpio;
        if (gpio == 0 || gpio == 1 || gpio == 6) continue;
        assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
        assert(bc250_config_pin_warnings(&config, warning, sizeof(warning)) ==
               !bc250_config_pin_is_safe(gpio));
    }
    config = fixture();
    config.ps_on.gpio = 12;
    assert(!bc250_config_pin_is_safe(12));
    assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
    assert(bc250_config_pin_warnings(&config, warning, sizeof(warning)));
    assert(strstr(warning, "12") && strstr(warning, "Saving is allowed"));
    config.advanced_gpio_override = true;
    assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
    assert(bc250_config_pin_warnings(&config, warning, sizeof(warning)));

    config.psu_i2c.enabled = true;
    config.psu_i2c.sda_gpio = 13; config.psu_i2c.scl_gpio = 14;
    config.button_count = 1;
    config.buttons[0] = (bc250_button_config_t) {
        .enabled = true, .input = {.gpio = 15, .debounce_ms = 40},
        .double_press_ms = 350, .long_press_ms = 1500,
    };
    assert(bc250_config_validate(&config, error, sizeof(error)) == ESP_OK);
    assert(bc250_config_pin_warnings(&config, warning, sizeof(warning)));
    assert(strstr(warning, "13") && strstr(warning, "14") && strstr(warning, "15"));
    assert(bc250_config_validate_i2c_pins(&config, 13, 14, error, sizeof(error)) == ESP_OK);
    assert(bc250_config_validate_i2c_pins(&config, 13, 13, error, sizeof(error)) != ESP_OK);
    assert(bc250_config_validate_i2c_pins(&config, 1, 14, error, sizeof(error)) != ESP_OK);
    config.psu_i2c.scl_gpio = 13;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
    config.psu_i2c.scl_gpio = 14;
    config.buttons[0].input.gpio = 12;
    assert(bc250_config_validate(&config, error, sizeof(error)) != ESP_OK);
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
