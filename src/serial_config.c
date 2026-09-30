#include "serial_config.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { BOOL, U8, I8, U16, U32, TEXT, ENUM, PASSWORD } field_type_t;
typedef struct {
    const char *name;
    size_t offset;
    field_type_t type;
    int64_t minimum;
    int64_t maximum;
    const char *const *choices;
} field_t;

static const char *const profiles[] = {"wifi", "zigbee", NULL};
static const char *const strategies[] = {"ps_on_only", "button_only", "ps_on_then_button", "simultaneous", NULL};
static const char *const actions[] = {"none", "on", "off", "toggle", "force_off", "config_ap",
                                     "zigbee_commission", "zigbee_reset", NULL};
static const char *const matchers[] = {"address", "name_exact", "name_prefix", "service_uuid",
                                      "manufacturer_data", NULL};

#define FIELD(structure, name, member, type, min, max, choices) \
    {name, offsetof(structure, member), type, min, max, choices}
#define ROOT(member, type, min, max) FIELD(bc250_config_t, #member, member, type, min, max, NULL)
#define STRING(member) ROOT(member, TEXT, 0, sizeof(((bc250_config_t *)0)->member) - 1)
#define PIN(member) ROOT(member.gpio, I8, -1, 31), ROOT(member.active_high, BOOL, 0, 1)
#define TIMING(member) ROOT(timing.member, U32, 0, UINT32_MAX)
static const field_t root_fields[] = {
    ROOT(configured, BOOL, 0, 1), ROOT(advanced_gpio_override, BOOL, 0, 1),
    ROOT(hold_ps_on, BOOL, 0, 1),
    FIELD(bc250_config_t, "radio_profile", radio_profile, ENUM, 0, BC250_RADIO_ZIGBEE, profiles),
    STRING(hostname), STRING(wifi_ssid), STRING(wifi_password),
    {"admin_password", 0, PASSWORD, 8, 64, NULL},
    ROOT(sense_on_ms, U16, 1, UINT16_MAX), ROOT(sense_off_ms, U16, 1, UINT16_MAX),
    ROOT(ble_scan_interval_ms, U16, 1, UINT16_MAX), ROOT(ble_scan_window_ms, U16, 1, UINT16_MAX),
    ROOT(ble_absent_ms, U32, 1, UINT32_MAX), ROOT(zigbee_channel, U8, 0, 26),
    STRING(zigbee_model),
    PIN(ps_on), PIN(power_button), PIN(power_sense), PIN(status_led),
    ROOT(power_sense.pull_up, BOOL, 0, 1), ROOT(power_sense.debounce_ms, U16, 0, UINT16_MAX),
    FIELD(bc250_config_t, "timing.strategy", timing.strategy, ENUM, 0, BC250_START_SIMULTANEOUS, strategies),
    TIMING(inter_output_delay_ms), TIMING(button_pulse_ms), TIMING(handoff_delay_ms),
    TIMING(start_timeout_ms), TIMING(shutdown_timeout_ms), TIMING(force_off_ms), TIMING(retry_cooldown_ms),
    ROOT(psu_i2c.enabled, BOOL, 0, 1), ROOT(psu_i2c.sda_gpio, I8, -1, 31), ROOT(psu_i2c.scl_gpio, I8, -1, 31),
    ROOT(psu_i2c.address, U8, 0x58, 0x5f), ROOT(psu_i2c.poll_interval_ms, U32, 500, 60000),
    ROOT(button_count, U8, 0, BC250_MAX_BUTTONS), ROOT(ble_device_count, U8, 0, BC250_MAX_BLE_DEVICES),
};
#define BUTTON(name, member, type, min, max) FIELD(bc250_button_config_t, name, member, type, min, max, NULL)
static const field_t button_fields[] = {
    BUTTON("enabled", enabled, BOOL, 0, 1), BUTTON("gpio", input.gpio, I8, -1, 31),
    BUTTON("active_high", input.active_high, BOOL, 0, 1), BUTTON("pull_up", input.pull_up, BOOL, 0, 1),
    BUTTON("debounce_ms", input.debounce_ms, U16, 1, UINT16_MAX),
    BUTTON("double_press_ms", double_press_ms, U16, 1, UINT16_MAX),
    BUTTON("long_press_ms", long_press_ms, U16, 1, UINT16_MAX),
    FIELD(bc250_button_config_t, "short_action", short_action, ENUM, 0, BC250_BUTTON_ACTION_ZIGBEE_RESET, actions),
    FIELD(bc250_button_config_t, "double_action", double_action, ENUM, 0, BC250_BUTTON_ACTION_ZIGBEE_RESET, actions),
    FIELD(bc250_button_config_t, "long_action", long_action, ENUM, 0, BC250_BUTTON_ACTION_ZIGBEE_RESET, actions),
};
#define BLE(member, type, min, max) FIELD(bc250_ble_device_config_t, #member, member, type, min, max, NULL)
static const field_t ble_fields[] = {
    BLE(enabled, BOOL, 0, 1),
    FIELD(bc250_ble_device_config_t, "type", type, ENUM, 0, BC250_BLE_MATCH_MANUFACTURER_DATA, matchers),
    BLE(label, TEXT, 0, 31), BLE(value, TEXT, 0, 64), BLE(mask, TEXT, 0, 64), BLE(min_rssi, I8, -128, 0),
};
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

bool bc250_serial_config_key(size_t index, char *key, size_t size)
{
    if (index < COUNT(root_fields)) {
        snprintf(key, size, "%s", root_fields[index].name);
        return true;
    }
    index -= COUNT(root_fields);
    if (index < BC250_MAX_BUTTONS * COUNT(button_fields)) {
        snprintf(key, size, "buttons.%zu.%s", index / COUNT(button_fields),
                 button_fields[index % COUNT(button_fields)].name);
        return true;
    }
    index -= BC250_MAX_BUTTONS * COUNT(button_fields);
    if (index < BC250_MAX_BLE_DEVICES * COUNT(ble_fields)) {
        snprintf(key, size, "ble_devices.%zu.%s", index / COUNT(ble_fields),
                 ble_fields[index % COUNT(ble_fields)].name);
        return true;
    }
    return false;
}

static const field_t *find_field(const char *key, size_t *offset)
{
    /* Short pin names are canonical; accept the web API's pins prefix too. */
    if (strncmp(key, "pins.", 5) == 0) key += 5;
    if (strcmp(key, "radio") == 0) key = "radio_profile";
    for (size_t i = 0; i < COUNT(root_fields); ++i) {
        if (strcmp(key, root_fields[i].name) == 0) {
            *offset = root_fields[i].offset;
            return &root_fields[i];
        }
    }
    const field_t *fields;
    size_t count, stride, base, max;
    if (strncmp(key, "buttons.", 8) == 0) {
        key += 8;
        fields = button_fields; count = COUNT(button_fields);
        stride = sizeof(bc250_button_config_t); base = offsetof(bc250_config_t, buttons);
        max = BC250_MAX_BUTTONS;
    } else if (strncmp(key, "ble_devices.", 12) == 0) {
        key += 12;
        fields = ble_fields; count = COUNT(ble_fields);
        stride = sizeof(bc250_ble_device_config_t); base = offsetof(bc250_config_t, ble_devices);
        max = BC250_MAX_BLE_DEVICES;
    } else return NULL;
    if (*key < '0' || *key > '9') return NULL;
    char *end;
    unsigned long index = strtoul(key, &end, 10);
    if (index >= max || *end != '.') return NULL;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(end + 1, fields[i].name) == 0) {
            *offset = base + index * stride + fields[i].offset;
            return &fields[i];
        }
    }
    return NULL;
}

esp_err_t bc250_serial_config_set(bc250_config_t *config, const char *key,
                                  const char *value, char *error, size_t error_size)
{
    size_t offset = 0;
    const field_t *field = find_field(key, &offset);
    if (field == NULL) {
        snprintf(error, error_size, "Unknown setting '%s'; enter config to list settings", key);
        return ESP_ERR_INVALID_ARG;
    }
    void *target = (char *)config + offset;
    int64_t number = 0;
    if (field->type == TEXT || field->type == PASSWORD) {
        size_t length = strlen(value);
        if (length < (size_t)field->minimum || length > (size_t)field->maximum) {
            snprintf(error, error_size, "%s needs %" PRId64 "-%" PRId64 " characters",
                     key, field->minimum, field->maximum);
            return ESP_ERR_INVALID_ARG;
        }
        if (field->type == PASSWORD) bc250_config_set_admin_password(config, value);
        else memcpy(target, value, length + 1);
        return ESP_OK;
    }
    if (field->type == BOOL) {
        if (!strcmp(value, "true") || !strcmp(value, "on") || !strcmp(value, "yes") || !strcmp(value, "1")) number = 1;
        else if (strcmp(value, "false") && strcmp(value, "off") && strcmp(value, "no") && strcmp(value, "0")) {
            snprintf(error, error_size, "%s expects on or off", key);
            return ESP_ERR_INVALID_ARG;
        }
    } else if (field->type == ENUM) {
        bool found = false;
        for (int i = 0; field->choices[i]; ++i) {
            if (strcmp(value, field->choices[i]) == 0) { number = i; found = true; break; }
        }
        if (!found) {
            snprintf(error, error_size, "Invalid %s; choices:", key);
            for (int i = 0; field->choices[i]; ++i) {
                size_t used = strlen(error);
                if (used < error_size) snprintf(error + used, error_size - used, " %s", field->choices[i]);
            }
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        char *end;
        errno = 0;
        /* Decimal by default (including leading zeros); allow explicit hex addresses. */
        int base = strncmp(value, "0x", 2) == 0 || strncmp(value, "0X", 2) == 0 ? 16 : 10;
        number = strtoll(value, &end, base);
        if (errno == ERANGE || end == value || *end || number < field->minimum || number > field->maximum) {
            snprintf(error, error_size, "%s expects an integer from %" PRId64 " to %" PRId64,
                     key, field->minimum, field->maximum);
            return ESP_ERR_INVALID_ARG;
        }
    }
    switch (field->type) {
    case BOOL: *(bool *)target = number != 0; break;
    case U8: *(uint8_t *)target = number; break;
    case I8: *(int8_t *)target = number; break;
    case U16: *(uint16_t *)target = number; break;
    case U32: *(uint32_t *)target = number; break;
    case ENUM:
        /* Assign through the actual enum type rather than assuming enum storage size. */
        if (field->choices == profiles) *(bc250_radio_profile_t *)target = number;
        else if (field->choices == strategies) *(bc250_start_strategy_t *)target = number;
        else if (field->choices == actions) *(bc250_button_action_t *)target = number;
        else *(bc250_ble_match_type_t *)target = number;
        break;
    default: break;
    }
    return ESP_OK;
}

static void print_field(const bc250_config_t *config, const char *key)
{
    size_t offset;
    const field_t *field = find_field(key, &offset);
    const void *value = (const char *)config + offset;
    printf("  %-34s ", key);
    if (field->type == PASSWORD || strcmp(key, "wifi_password") == 0) puts("(hidden)");
    else if (field->type == TEXT) printf("\"%s\"\n", (const char *)value);
    else if (field->type == BOOL) puts(*(const bool *)value ? "on" : "off");
    else if (field->type == ENUM) {
        int number;
        if (field->choices == profiles) number = *(const bc250_radio_profile_t *)value;
        else if (field->choices == strategies) number = *(const bc250_start_strategy_t *)value;
        else if (field->choices == actions) number = *(const bc250_button_action_t *)value;
        else number = *(const bc250_ble_match_type_t *)value;
        puts(number >= 0 && number <= field->maximum ? field->choices[number] : "invalid");
    } else if (field->type == U8) printf("%u\n", *(const uint8_t *)value);
    else if (field->type == I8) printf("%d\n", *(const int8_t *)value);
    else if (field->type == U16) printf("%u\n", *(const uint16_t *)value);
    else printf("%" PRIu32 "\n", *(const uint32_t *)value);
}

void bc250_serial_config_print(const bc250_config_t *config)
{
    for (size_t i = 0; i < COUNT(root_fields); ++i) print_field(config, root_fields[i].name);
    char key[80];
    for (unsigned i = 0; i < config->button_count; ++i) {
        for (size_t j = 0; j < COUNT(button_fields); ++j) {
            snprintf(key, sizeof(key), "buttons.%u.%s", i, button_fields[j].name);
            print_field(config, key);
        }
    }
    for (unsigned i = 0; i < config->ble_device_count; ++i) {
        for (size_t j = 0; j < COUNT(ble_fields); ++j) {
            snprintf(key, sizeof(key), "ble_devices.%u.%s", i, ble_fields[j].name);
            print_field(config, key);
        }
    }
}
