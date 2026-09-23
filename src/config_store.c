#include "config_store.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"

static const char *TAG = "config";
static const char *NVS_NAMESPACE = "bc250";
static const char *KEY_ACTIVE = "active";
static const char *KEY_PENDING = "pending";
static const char *KEY_PENDING_BOOTS = "pboots";
static bc250_config_t s_config;
static bool s_using_pending;
static bool s_recovery_required;

static uint32_t config_crc(const bc250_config_t *config)
{
    bc250_config_t copy = *config;
    copy.crc32 = 0;
    return esp_crc32_le(0, (const uint8_t *)&copy, sizeof(copy));
}

static void finalize_config(bc250_config_t *config)
{
    config->schema_version = BC250_CONFIG_SCHEMA_VERSION;
    config->crc32 = config_crc(config);
}

static bool config_blob_valid(const bc250_config_t *config)
{
    return config->schema_version == BC250_CONFIG_SCHEMA_VERSION &&
           config->crc32 == config_crc(config);
}

static esp_err_t read_blob(nvs_handle_t handle, const char *key, bc250_config_t *config)
{
    size_t size = sizeof(*config);
    esp_err_t err = nvs_get_blob(handle, key, config, &size);
    if (err != ESP_OK) {
        return err;
    }
    return size == sizeof(*config) && config_blob_valid(config) ? ESP_OK : ESP_ERR_INVALID_CRC;
}

static esp_err_t write_blob(nvs_handle_t handle, const char *key, const bc250_config_t *source)
{
    bc250_config_t config = *source;
    finalize_config(&config);
    ESP_RETURN_ON_ERROR(nvs_set_blob(handle, key, &config, sizeof(config)), TAG, "set %s", key);
    return nvs_commit(handle);
}

static void random_password(char output[17])
{
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    uint8_t random[12];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        output[i] = alphabet[random[i] % (sizeof(alphabet) - 1)];
    }
    output[sizeof(random)] = '\0';
}

static void hash_password(const uint8_t salt[16], const char *password, uint8_t output[32])
{
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    size_t output_length = 0;
    psa_status_t status = psa_crypto_init();
    if (status == PSA_SUCCESS) status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
    if (status == PSA_SUCCESS) status = psa_hash_update(&operation, salt, 16);
    if (status == PSA_SUCCESS) {
        status = psa_hash_update(&operation, (const uint8_t *)password, strlen(password));
    }
    if (status == PSA_SUCCESS) {
        status = psa_hash_finish(&operation, output, 32, &output_length);
    }
    if (status != PSA_SUCCESS || output_length != 32) memset(output, 0, 32);
    psa_hash_abort(&operation);
}

void bc250_config_set_admin_password(bc250_config_t *config, const char *password)
{
    if (config == NULL || password == NULL) {
        return;
    }
    esp_fill_random(config->admin_salt, sizeof(config->admin_salt));
    hash_password(config->admin_salt, password, config->admin_hash);
}

void bc250_config_defaults(bc250_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->schema_version = BC250_CONFIG_SCHEMA_VERSION;
    config->configured = false;
    config->radio_profile = BC250_RADIO_WIFI;
    strlcpy(config->hostname, "bc250-ctrl", sizeof(config->hostname));
    random_password(config->ap_password);
    bc250_config_set_admin_password(config, config->ap_password);
    config->ps_on.gpio = BC250_GPIO_DISABLED;
    config->ps_on.active_high = true;
    config->power_button.gpio = BC250_GPIO_DISABLED;
    config->power_button.active_high = true;
    config->power_sense.gpio = BC250_GPIO_DISABLED;
    config->power_sense.active_high = false;
    config->power_sense.pull_up = true;
    config->power_sense.debounce_ms = 50;
    config->status_led.gpio = BC250_GPIO_DISABLED;
    config->status_led.active_high = true;
    config->timing = bc250_power_default_timing();
    config->sense_on_ms = 500;
    config->sense_off_ms = 2000;
    config->ble_scan_interval_ms = 500;
    config->ble_scan_window_ms = 100;
    config->ble_absent_ms = 30000;
    config->zigbee_channel = 0;
    strlcpy(config->zigbee_manufacturer, "BC250", sizeof(config->zigbee_manufacturer));
    strlcpy(config->zigbee_model, "BC250 Controller", sizeof(config->zigbee_model));
    for (size_t i = 0; i < BC250_MAX_BUTTONS; ++i) {
        config->buttons[i].input.gpio = BC250_GPIO_DISABLED;
        config->buttons[i].input.active_high = false;
        config->buttons[i].input.pull_up = true;
        config->buttons[i].input.debounce_ms = 40;
        config->buttons[i].double_press_ms = 350;
        config->buttons[i].long_press_ms = 1500;
    }
    finalize_config(config);
}

esp_err_t bc250_config_store_init(bool *first_boot, bool *using_pending)
{
    s_recovery_required = false;
    if (first_boot != NULL) *first_boot = false;
    if (using_pending != NULL) *using_pending = false;

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "open NVS");

    esp_err_t err = read_blob(handle, KEY_PENDING, &s_config);
    if (err == ESP_OK) {
        uint8_t boots = 0;
        nvs_get_u8(handle, KEY_PENDING_BOOTS, &boots);
        boots++;
        if (boots <= 2) {
            nvs_set_u8(handle, KEY_PENDING_BOOTS, boots);
            nvs_commit(handle);
            s_using_pending = true;
            if (using_pending != NULL) *using_pending = true;
            nvs_close(handle);
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Pending configuration failed to become healthy; rolling back");
        s_recovery_required = true;
        nvs_erase_key(handle, KEY_PENDING);
        nvs_erase_key(handle, KEY_PENDING_BOOTS);
        nvs_commit(handle);
    }
    err = read_blob(handle, KEY_ACTIVE, &s_config);
    if (err == ESP_OK) {
        s_using_pending = false;
        nvs_close(handle);
        return ESP_OK;
    }

    bc250_config_defaults(&s_config);
    err = write_blob(handle, KEY_ACTIVE, &s_config);
    nvs_close(handle);
    if (first_boot != NULL) *first_boot = true;
    s_using_pending = false;
    ESP_LOGW(TAG, "First boot provisioning password: %s", s_config.ap_password);
    return err;
}

const bc250_config_t *bc250_config_get(void)
{
    return &s_config;
}

bool bc250_config_recovery_required(void)
{
    return s_recovery_required;
}

static bool pin_in_use(const bc250_config_t *config, int gpio, int except_button)
{
    if (gpio < 0) return false;
    if (config->ps_on.gpio == gpio || config->power_button.gpio == gpio ||
        config->power_sense.gpio == gpio || config->status_led.gpio == gpio) {
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

static esp_err_t validate_one_pin(const bc250_config_t *config, int gpio,
                                  const char *name, char *error, size_t error_size)
{
    if (gpio == BC250_GPIO_DISABLED) return ESP_OK;
    if (gpio < 0 || gpio > 31) {
        snprintf(error, error_size, "%s GPIO is outside the supported range", name);
        return ESP_ERR_INVALID_ARG;
    }
    if (!config->advanced_gpio_override && !bc250_config_pin_is_safe(gpio)) {
        snprintf(error, error_size, "%s GPIO %d is reserved or not portable", name, gpio);
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
    const struct { int gpio; const char *name; } fixed[] = {
        {config->ps_on.gpio, "PS_ON"},
        {config->power_button.gpio, "power button"},
        {config->power_sense.gpio, "power sense"},
        {config->status_led.gpio, "status LED"},
    };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        ESP_RETURN_ON_ERROR(validate_one_pin(config, fixed[i].gpio, fixed[i].name, error, error_size),
                            TAG, "pin validation");
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
        ESP_RETURN_ON_ERROR(validate_one_pin(config, button->input.gpio, "input button", error, error_size),
                            TAG, "button pin validation");
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

esp_err_t bc250_config_save_pending(const bc250_config_t *config)
{
    char error[128];
    ESP_RETURN_ON_ERROR(bc250_config_validate(config, error, sizeof(error)), TAG, "%s", error);
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "open NVS");
    esp_err_t err = write_blob(handle, KEY_PENDING, config);
    nvs_close(handle);
    return err;
}

esp_err_t bc250_config_mark_healthy(void)
{
    if (!s_using_pending) return ESP_OK;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "open NVS");
    esp_err_t err = write_blob(handle, KEY_ACTIVE, &s_config);
    if (err == ESP_OK) {
        nvs_erase_key(handle, KEY_PENDING);
        nvs_erase_key(handle, KEY_PENDING_BOOTS);
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) s_using_pending = false;
    return err;
}

esp_err_t bc250_config_factory_reset(void)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "open NVS");
    esp_err_t err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

bool bc250_config_password_verify(const char *password)
{
    if (password == NULL) return false;
    uint8_t hash[32];
    hash_password(s_config.admin_salt, password, hash);
    unsigned diff = 0;
    for (size_t i = 0; i < sizeof(hash); ++i) diff |= hash[i] ^ s_config.admin_hash[i];
    return diff == 0;
}

const char *bc250_radio_profile_name(bc250_radio_profile_t profile)
{
    switch (profile) {
    case BC250_RADIO_WIFI: return "wifi";
    case BC250_RADIO_ZIGBEE: return "zigbee";
    case BC250_RADIO_HYBRID: return "hybrid";
    default: return "invalid";
    }
}

const char *bc250_button_action_name(bc250_button_action_t action)
{
    static const char *names[] = {"none", "on", "off", "toggle", "force_off", "config_ap",
                                  "zigbee_commission", "zigbee_reset"};
    return action <= BC250_BUTTON_ACTION_ZIGBEE_RESET ? names[action] : "none";
}

static cJSON *pin_to_json(int gpio, bool active_high)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "gpio", gpio);
    cJSON_AddBoolToObject(obj, "active_high", active_high);
    return obj;
}

char *bc250_config_to_json(const bc250_config_t *config, bool include_secrets)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema_version", config->schema_version);
    cJSON_AddBoolToObject(root, "configured", config->configured);
    cJSON_AddStringToObject(root, "radio_profile", bc250_radio_profile_name(config->radio_profile));
    cJSON_AddStringToObject(root, "hostname", config->hostname);
    cJSON_AddStringToObject(root, "wifi_ssid", config->wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_password", include_secrets ? config->wifi_password : "");
    cJSON_AddBoolToObject(root, "advanced_gpio_override", config->advanced_gpio_override);
    cJSON_AddNumberToObject(root, "sense_on_ms", config->sense_on_ms);
    cJSON_AddNumberToObject(root, "sense_off_ms", config->sense_off_ms);
    cJSON_AddNumberToObject(root, "ble_scan_interval_ms", config->ble_scan_interval_ms);
    cJSON_AddNumberToObject(root, "ble_scan_window_ms", config->ble_scan_window_ms);
    cJSON_AddNumberToObject(root, "ble_absent_ms", config->ble_absent_ms);
    cJSON_AddNumberToObject(root, "zigbee_channel", config->zigbee_channel);
    cJSON_AddStringToObject(root, "zigbee_manufacturer", config->zigbee_manufacturer);
    cJSON_AddStringToObject(root, "zigbee_model", config->zigbee_model);

    cJSON *pins = cJSON_AddObjectToObject(root, "pins");
    cJSON_AddItemToObject(pins, "ps_on", pin_to_json(config->ps_on.gpio, config->ps_on.active_high));
    cJSON_AddItemToObject(pins, "power_button", pin_to_json(config->power_button.gpio, config->power_button.active_high));
    cJSON_AddItemToObject(pins, "power_sense", pin_to_json(config->power_sense.gpio, config->power_sense.active_high));
    cJSON_AddItemToObject(pins, "status_led", pin_to_json(config->status_led.gpio, config->status_led.active_high));
    cJSON *sense = cJSON_GetObjectItemCaseSensitive(pins, "power_sense");
    cJSON_AddBoolToObject(sense, "pull_up", config->power_sense.pull_up);
    cJSON_AddNumberToObject(sense, "debounce_ms", config->power_sense.debounce_ms);

    cJSON *timing = cJSON_AddObjectToObject(root, "timing");
    cJSON_AddNumberToObject(timing, "strategy", config->timing.strategy);
    cJSON_AddNumberToObject(timing, "inter_output_delay_ms", config->timing.inter_output_delay_ms);
    cJSON_AddNumberToObject(timing, "button_pulse_ms", config->timing.button_pulse_ms);
    cJSON_AddNumberToObject(timing, "handoff_delay_ms", config->timing.handoff_delay_ms);
    cJSON_AddNumberToObject(timing, "start_timeout_ms", config->timing.start_timeout_ms);
    cJSON_AddNumberToObject(timing, "shutdown_timeout_ms", config->timing.shutdown_timeout_ms);
    cJSON_AddNumberToObject(timing, "force_off_ms", config->timing.force_off_ms);
    cJSON_AddNumberToObject(timing, "retry_cooldown_ms", config->timing.retry_cooldown_ms);

    cJSON *buttons = cJSON_AddArrayToObject(root, "buttons");
    for (int i = 0; i < config->button_count; ++i) {
        const bc250_button_config_t *button = &config->buttons[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddBoolToObject(item, "enabled", button->enabled);
        cJSON_AddNumberToObject(item, "gpio", button->input.gpio);
        cJSON_AddBoolToObject(item, "active_high", button->input.active_high);
        cJSON_AddBoolToObject(item, "pull_up", button->input.pull_up);
        cJSON_AddNumberToObject(item, "debounce_ms", button->input.debounce_ms);
        cJSON_AddNumberToObject(item, "double_press_ms", button->double_press_ms);
        cJSON_AddNumberToObject(item, "long_press_ms", button->long_press_ms);
        cJSON_AddStringToObject(item, "short_action", bc250_button_action_name(button->short_action));
        cJSON_AddStringToObject(item, "double_action", bc250_button_action_name(button->double_action));
        cJSON_AddStringToObject(item, "long_action", bc250_button_action_name(button->long_action));
        cJSON_AddItemToArray(buttons, item);
    }

    cJSON *ble = cJSON_AddArrayToObject(root, "ble_devices");
    for (int i = 0; i < config->ble_device_count; ++i) {
        const bc250_ble_device_config_t *device = &config->ble_devices[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddBoolToObject(item, "enabled", device->enabled);
        cJSON_AddNumberToObject(item, "type", device->type);
        cJSON_AddStringToObject(item, "label", device->label);
        cJSON_AddStringToObject(item, "value", device->value);
        cJSON_AddStringToObject(item, "mask", device->mask);
        cJSON_AddNumberToObject(item, "min_rssi", device->min_rssi);
        cJSON_AddItemToArray(ble, item);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static bc250_radio_profile_t parse_profile(const char *value)
{
    if (value && strcmp(value, "zigbee") == 0) return BC250_RADIO_ZIGBEE;
    if (value && strcmp(value, "hybrid") == 0) return BC250_RADIO_HYBRID;
    return BC250_RADIO_WIFI;
}

static bc250_button_action_t parse_action(const char *value)
{
    for (int i = 0; i <= BC250_BUTTON_ACTION_ZIGBEE_RESET; ++i) {
        if (value && strcmp(value, bc250_button_action_name(i)) == 0) return i;
    }
    return BC250_BUTTON_ACTION_NONE;
}

static bool valid_integer(const cJSON *item, double minimum, double maximum)
{
    return cJSON_IsNumber(item) && isfinite(item->valuedouble) &&
           item->valuedouble >= minimum && item->valuedouble <= maximum &&
           floor(item->valuedouble) == item->valuedouble;
}

static bool patch_u32(cJSON *parent, const char *name, uint32_t *target, uint32_t maximum)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (item == NULL) return true;
    if (!valid_integer(item, 0, maximum)) return false;
    *target = (uint32_t)item->valuedouble;
    return true;
}

static bool patch_pin(cJSON *parent, const char *name, bc250_output_config_t *pin)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (!cJSON_IsObject(item)) return true;
    cJSON *gpio = cJSON_GetObjectItemCaseSensitive(item, "gpio");
    cJSON *active = cJSON_GetObjectItemCaseSensitive(item, "active_high");
    if (gpio != NULL) {
        if (!valid_integer(gpio, BC250_GPIO_DISABLED, 31)) return false;
        pin->gpio = (int8_t)gpio->valueint;
    }
    if (cJSON_IsBool(active)) pin->active_high = cJSON_IsTrue(active);
    return true;
}

esp_err_t bc250_config_patch_json(bc250_config_t *config, const char *json,
                                  char *error, size_t error_size)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        snprintf(error, error_size, "invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "configured");
    if (cJSON_IsBool(item)) config->configured = cJSON_IsTrue(item);
    item = cJSON_GetObjectItemCaseSensitive(root, "advanced_gpio_override");
    if (cJSON_IsBool(item)) config->advanced_gpio_override = cJSON_IsTrue(item);
    item = cJSON_GetObjectItemCaseSensitive(root, "radio_profile");
    if (cJSON_IsString(item)) config->radio_profile = parse_profile(item->valuestring);
    item = cJSON_GetObjectItemCaseSensitive(root, "hostname");
    if (cJSON_IsString(item)) strlcpy(config->hostname, item->valuestring, sizeof(config->hostname));
    item = cJSON_GetObjectItemCaseSensitive(root, "wifi_ssid");
    if (cJSON_IsString(item)) strlcpy(config->wifi_ssid, item->valuestring, sizeof(config->wifi_ssid));
    item = cJSON_GetObjectItemCaseSensitive(root, "wifi_password");
    if (cJSON_IsString(item) && item->valuestring[0]) {
        strlcpy(config->wifi_password, item->valuestring, sizeof(config->wifi_password));
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "admin_password");
    if (cJSON_IsString(item) && item->valuestring[0] != '\0') {
        if (strlen(item->valuestring) < 8) {
            cJSON_Delete(root);
            snprintf(error, error_size, "admin password must contain at least eight characters");
            return ESP_ERR_INVALID_ARG;
        }
        bc250_config_set_admin_password(config, item->valuestring);
    }
    uint32_t number;
#define PATCH_ROOT_U32(field, maximum) do { number = config->field; \
    if (!patch_u32(root, #field, &number, maximum)) goto invalid_numeric; \
    config->field = number; } while (0)
    PATCH_ROOT_U32(sense_on_ms, UINT16_MAX);
    PATCH_ROOT_U32(sense_off_ms, UINT16_MAX);
    PATCH_ROOT_U32(ble_scan_interval_ms, UINT16_MAX);
    PATCH_ROOT_U32(ble_scan_window_ms, UINT16_MAX);
    PATCH_ROOT_U32(ble_absent_ms, UINT32_MAX);
#undef PATCH_ROOT_U32
    item = cJSON_GetObjectItemCaseSensitive(root, "zigbee_channel");
    if (cJSON_IsNumber(item)) config->zigbee_channel = item->valueint;
    item = cJSON_GetObjectItemCaseSensitive(root, "zigbee_manufacturer");
    if (cJSON_IsString(item)) strlcpy(config->zigbee_manufacturer, item->valuestring,
                                      sizeof(config->zigbee_manufacturer));
    item = cJSON_GetObjectItemCaseSensitive(root, "zigbee_model");
    if (cJSON_IsString(item)) strlcpy(config->zigbee_model, item->valuestring,
                                      sizeof(config->zigbee_model));

    cJSON *pins = cJSON_GetObjectItemCaseSensitive(root, "pins");
    if (cJSON_IsObject(pins)) {
        if (!patch_pin(pins, "ps_on", &config->ps_on) ||
            !patch_pin(pins, "power_button", &config->power_button) ||
            !patch_pin(pins, "status_led", &config->status_led)) goto invalid_numeric;
        bc250_output_config_t sense = {.gpio = config->power_sense.gpio,
                                       .active_high = config->power_sense.active_high};
        if (!patch_pin(pins, "power_sense", &sense)) goto invalid_numeric;
        config->power_sense.gpio = sense.gpio;
        config->power_sense.active_high = sense.active_high;
        cJSON *sense_json = cJSON_GetObjectItemCaseSensitive(pins, "power_sense");
        cJSON *v = cJSON_GetObjectItemCaseSensitive(sense_json, "pull_up");
        if (cJSON_IsBool(v)) config->power_sense.pull_up = cJSON_IsTrue(v);
        v = cJSON_GetObjectItemCaseSensitive(sense_json, "debounce_ms");
        if (v != NULL) {
            if (!valid_integer(v, 0, UINT16_MAX)) goto invalid_numeric;
            config->power_sense.debounce_ms = (uint16_t)v->valueint;
        }
    }

    cJSON *timing = cJSON_GetObjectItemCaseSensitive(root, "timing");
#define PATCH_U32(field) do { \
    if (!patch_u32(timing, #field, &config->timing.field, UINT32_MAX)) goto invalid_numeric; \
    } while (0)
    if (cJSON_IsObject(timing)) {
        cJSON *strategy = cJSON_GetObjectItemCaseSensitive(timing, "strategy");
        if (cJSON_IsNumber(strategy)) config->timing.strategy = strategy->valueint;
        PATCH_U32(inter_output_delay_ms);
        PATCH_U32(button_pulse_ms);
        PATCH_U32(handoff_delay_ms);
        PATCH_U32(start_timeout_ms);
        PATCH_U32(shutdown_timeout_ms);
        PATCH_U32(force_off_ms);
        PATCH_U32(retry_cooldown_ms);
    }
#undef PATCH_U32

    cJSON *buttons = cJSON_GetObjectItemCaseSensitive(root, "buttons");
    if (cJSON_IsArray(buttons)) {
        int count = cJSON_GetArraySize(buttons);
        if (count > BC250_MAX_BUTTONS) goto invalid_numeric;
        config->button_count = count;
        for (int i = 0; i < count; ++i) {
            cJSON *src = cJSON_GetArrayItem(buttons, i);
            bc250_button_config_t *dst = &config->buttons[i];
            cJSON *v = cJSON_GetObjectItemCaseSensitive(src, "enabled");
            dst->enabled = !cJSON_IsBool(v) || cJSON_IsTrue(v);
            v = cJSON_GetObjectItemCaseSensitive(src, "gpio");
            if (v != NULL) {
                if (!valid_integer(v, BC250_GPIO_DISABLED, 31)) goto invalid_numeric;
                dst->input.gpio = (int8_t)v->valueint;
            }
            v = cJSON_GetObjectItemCaseSensitive(src, "active_high");
            if (cJSON_IsBool(v)) dst->input.active_high = cJSON_IsTrue(v);
            v = cJSON_GetObjectItemCaseSensitive(src, "pull_up");
            if (cJSON_IsBool(v)) dst->input.pull_up = cJSON_IsTrue(v);
            v = cJSON_GetObjectItemCaseSensitive(src, "debounce_ms");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0) dst->input.debounce_ms = v->valueint;
            v = cJSON_GetObjectItemCaseSensitive(src, "double_press_ms");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0) dst->double_press_ms = v->valueint;
            v = cJSON_GetObjectItemCaseSensitive(src, "long_press_ms");
            if (cJSON_IsNumber(v) && v->valuedouble >= 0) dst->long_press_ms = v->valueint;
            v = cJSON_GetObjectItemCaseSensitive(src, "short_action");
            if (cJSON_IsString(v)) dst->short_action = parse_action(v->valuestring);
            v = cJSON_GetObjectItemCaseSensitive(src, "double_action");
            if (cJSON_IsString(v)) dst->double_action = parse_action(v->valuestring);
            v = cJSON_GetObjectItemCaseSensitive(src, "long_action");
            if (cJSON_IsString(v)) dst->long_action = parse_action(v->valuestring);
        }
    }

    cJSON *ble = cJSON_GetObjectItemCaseSensitive(root, "ble_devices");
    if (cJSON_IsArray(ble)) {
        int count = cJSON_GetArraySize(ble);
        if (count > BC250_MAX_BLE_DEVICES) goto invalid_numeric;
        config->ble_device_count = count;
        for (int i = 0; i < count; ++i) {
            cJSON *src = cJSON_GetArrayItem(ble, i);
            bc250_ble_device_config_t *dst = &config->ble_devices[i];
            memset(dst, 0, sizeof(*dst));
            cJSON *v = cJSON_GetObjectItemCaseSensitive(src, "enabled");
            dst->enabled = !cJSON_IsBool(v) || cJSON_IsTrue(v);
            v = cJSON_GetObjectItemCaseSensitive(src, "type");
            if (cJSON_IsNumber(v)) dst->type = v->valueint;
            v = cJSON_GetObjectItemCaseSensitive(src, "label");
            if (cJSON_IsString(v)) strlcpy(dst->label, v->valuestring, sizeof(dst->label));
            v = cJSON_GetObjectItemCaseSensitive(src, "value");
            if (cJSON_IsString(v)) strlcpy(dst->value, v->valuestring, sizeof(dst->value));
            v = cJSON_GetObjectItemCaseSensitive(src, "mask");
            if (cJSON_IsString(v)) strlcpy(dst->mask, v->valuestring, sizeof(dst->mask));
            v = cJSON_GetObjectItemCaseSensitive(src, "min_rssi");
            dst->min_rssi = cJSON_IsNumber(v) ? v->valueint : -100;
        }
    }
    cJSON_Delete(root);
    finalize_config(config);
    return bc250_config_validate(config, error, error_size);

invalid_numeric:
    cJSON_Delete(root);
    snprintf(error, error_size, "numeric value or array length is outside its supported range");
    return ESP_ERR_INVALID_ARG;
}
