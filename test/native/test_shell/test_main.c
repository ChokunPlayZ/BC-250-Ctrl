#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "serial_config.h"
#include "shell_args.h"

static unsigned password_updates;
void bc250_config_set_admin_password(bc250_config_t *config, const char *password)
{
    (void)config;
    assert(strlen(password) >= 8);
    ++password_updates;
}

static void test_commands(void)
{
    char line[] = " \tconfig  set wifi_ssid 'My Wi-Fi'  \t";
    char *argv[6];
    assert(bc250_shell_split(line, argv, 6) == 4);
    assert(!strcmp(argv[0], "config"));
    assert(!strcmp(argv[1], "set"));
    assert(!strcmp(argv[2], "wifi_ssid"));
    assert(!strcmp(argv[3], "My Wi-Fi"));

    char escapes[] = "set wifi_password \"a \\\"quote\\\" \\\\ $literal\"";
    assert(bc250_shell_split(escapes, argv, 6) == 3);
    assert(!strcmp(argv[2], "a \"quote\" \\ $literal"));
    char empty[] = "set wifi_password ''";
    assert(bc250_shell_split(empty, argv, 6) == 3 && !*argv[2]);
    char joined[] = "set hostname bc250'-lab'";
    assert(bc250_shell_split(joined, argv, 6) == 3 && !strcmp(argv[2], "bc250-lab"));
    char backslash[] = "set wifi_ssid My\\ Wi-Fi";
    assert(bc250_shell_split(backslash, argv, 6) == 3 && !strcmp(argv[2], "My Wi-Fi"));
    char blank[] = " \t ";
    assert(bc250_shell_split(blank, argv, 6) == 0);
    char unclosed[] = "set hostname 'broken";
    assert(bc250_shell_split(unclosed, argv, 6) == -1);
    char trailing[] = "set hostname broken\\";
    assert(bc250_shell_split(trailing, argv, 6) == -1);
    char excess[] = "one two three four five six seven";
    assert(bc250_shell_split(excess, argv, 6) == -1);
    char exact[] = "one two";
    assert(bc250_shell_split(exact, argv, 2) == 2);
}

static void set(bc250_config_t *config, const char *key, const char *value)
{
    char error[160];
    esp_err_t result = bc250_serial_config_set(config, key, value, error, sizeof(error));
    if (result != ESP_OK) fprintf(stderr, "%s\n", error);
    assert(result == ESP_OK);
}

static void reject(bc250_config_t *config, const char *key, const char *value)
{
    bc250_config_t before = *config;
    char error[160] = "";
    assert(bc250_serial_config_set(config, key, value, error, sizeof(error)) != ESP_OK);
    assert(*error);
    assert(memcmp(config, &before, sizeof(before)) == 0);
}

static void test_settings(void)
{
    bc250_config_t config = {0};
    set(&config, "hostname", "bc250-lab");
    set(&config, "wifi_ssid", "My Wi-Fi");
    set(&config, "wifi_password", "secret \\ $ ' \"");
    assert(!strcmp(config.hostname, "bc250-lab"));
    assert(!strcmp(config.wifi_ssid, "My Wi-Fi"));
    assert(!strcmp(config.wifi_password, "secret \\ $ ' \""));
    set(&config, "wifi_password", "");
    assert(!*config.wifi_password);
    set(&config, "radio", "zigbee");
    assert(config.radio_profile == BC250_RADIO_ZIGBEE);
    reject(&config, "radio", "hybrid");
    reject(&config, "zigbee_manufacturer", "Other vendor");
    reject(&config, "zigbee_manufacturer", "CKLabs");
    set(&config, "zigbee_model", "Custom model");
    assert(!strcmp(config.zigbee_model, "Custom model"));
    set(&config, "timing.strategy", "button_only");
    assert(config.timing.strategy == BC250_START_BUTTON_ONLY);
    set(&config, "timing.strategy", "ps_on_latched");
    assert(config.timing.strategy == BC250_START_PS_ON_LATCHED);
    set(&config, "hold_ps_on", "on");
    assert(config.hold_ps_on);
    set(&config, "hold_ps_on", "off");
    assert(!config.hold_ps_on);
    reject(&config, "hold_ps_on", "maybe");
    set(&config, "configured", "yes");
    assert(config.configured);
    set(&config, "configured", "off");
    assert(!config.configured);
    set(&config, "ps_on.gpio", "-1");
    assert(config.ps_on.gpio == -1);
    set(&config, "pins.ps_on.gpio", "04");
    assert(config.ps_on.gpio == 4);
    set(&config, "psu_i2c.address", "0x5F");
    assert(config.psu_i2c.address == 95);
    set(&config, "timing.retry_cooldown_ms", "4294967295");
    assert(config.timing.retry_cooldown_ms == UINT32_MAX);
    set(&config, "buttons.7.gpio", "23");
    set(&config, "buttons.7.short_action", "toggle");
    assert(config.buttons[7].input.gpio == 23);
    assert(config.buttons[7].short_action == BC250_BUTTON_ACTION_TOGGLE);
    set(&config, "ble_devices.15.type", "name_prefix");
    set(&config, "ble_devices.15.min_rssi", "-100");
    assert(config.ble_devices[15].type == BC250_BLE_MATCH_NAME_PREFIX);
    assert(config.ble_devices[15].min_rssi == -100);
    set(&config, "admin_password", "valid secret");
    assert(password_updates == 1);

    reject(&config, "hostname", "12345678901234567890123456789012");
    reject(&config, "radio_profile", "invalid");
    reject(&config, "radio_profile", "2");
    reject(&config, "configured", "maybe");
    reject(&config, "ps_on.gpio", "256");
    reject(&config, "ps_on.gpio", "-2");
    reject(&config, "ps_on.gpio", "4garbage");
    reject(&config, "ps_on.gpio", "4.5");
    reject(&config, "ps_on.gpio", "");
    reject(&config, "timing.force_off_ms", "4294967296");
    reject(&config, "timing.force_off_ms", "999999999999999999999999999999999");
    reject(&config, "timing.force_off_ms", "-1");
    reject(&config, "sense_on_ms", "65536");
    reject(&config, "psu_i2c.address", "0x60");
    reject(&config, "psu_i2c.poll_interval_ms", "499");
    reject(&config, "buttons.8.enabled", "on");
    reject(&config, "buttons.-1.enabled", "on");
    reject(&config, "ble_devices.16.enabled", "on");
    reject(&config, "ble_devices.0.type", "unknown");
    reject(&config, "button_count", "9");
    reject(&config, "ble_device_count", "17");
    reject(&config, "buttons.0.short_action", "wrong");
    reject(&config, "unknown", "value");
    reject(&config, "admin_password", "short");
    assert(password_updates == 1);

    /* Every completion names a real setting. Array counts also expose the last slots. */
    config.button_count = BC250_MAX_BUTTONS;
    config.ble_device_count = BC250_MAX_BLE_DEVICES;
    char key[80], error[160];
    size_t count = 0;
    while (bc250_serial_config_key(count, key, sizeof(key))) {
        assert(strcmp(key, "zigbee_manufacturer") != 0);
        error[0] = '\0';
        bc250_serial_config_set(&config, key, "", error, sizeof(error));
        assert(strncmp(error, "Unknown setting", 15) != 0);
        ++count;
    }
    assert(count > 200);
    bc250_serial_config_print(&config);
}

int main(void)
{
    test_commands();
    test_settings();
    puts("Shell tests passed.");
    return 0;
}
