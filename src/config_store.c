#include "config_store.h"

#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
static SemaphoreHandle_t s_write_lock;

static uint32_t config_crc(const bc250_config_t *config)
{
    const uint8_t *data = (const uint8_t *)config;
    const size_t crc_offset = offsetof(bc250_config_t, crc32);
    const size_t after_crc = crc_offset + sizeof(config->crc32);
    const uint32_t zero = 0;
    // Preserve the stored blob format without copying the whole configuration onto the stack.
    uint32_t crc = esp_crc32_le(0, data, crc_offset);
    crc = esp_crc32_le(crc, (const uint8_t *)&zero, sizeof(zero));
    return esp_crc32_le(crc, data + after_crc, sizeof(*config) - after_crc);
}

static void psu_i2c_defaults(bc250_psu_i2c_config_t *psu)
{
    psu->enabled = false;
    psu->sda_gpio = BC250_GPIO_DISABLED;
    psu->scl_gpio = BC250_GPIO_DISABLED;
    psu->address = 0x5f;
    psu->poll_interval_ms = 2000;
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
    memset(config, 0, sizeof(*config));
    size_t size = sizeof(*config);
    esp_err_t err = nvs_get_blob(handle, key, config, &size);
    if (err != ESP_OK) {
        return err;
    }
    if (size == sizeof(*config) && config_blob_valid(config)) {
        if (bc250_config_migrate_legacy_profile(config)) {
            ESP_LOGW(TAG, "Saved Wi-Fi + Zigbee profile migrated to Zigbee-only");
            finalize_config(config);
        }
        return ESP_OK;
    }
    // Version 1 ended at the Zigbee model, with tail padding up to the next 4-byte boundary.
    if (size == offsetof(bc250_config_t, psu_i2c) && config->schema_version == 1) {
        uint32_t saved_crc = config->crc32;
        config->crc32 = 0;
        bool valid = saved_crc == esp_crc32_le(0, (const uint8_t *)config, size);
        config->crc32 = saved_crc;
        if (valid) {
            psu_i2c_defaults(&config->psu_i2c);
            bc250_config_migrate_legacy_profile(config);
            finalize_config(config);
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_CRC;
}

static esp_err_t write_blob(nvs_handle_t handle, const char *key, const bc250_config_t *source)
{
    bc250_config_t *config = malloc(sizeof(*config));
    if (config == NULL) return ESP_ERR_NO_MEM;
    *config = *source;
    finalize_config(config);
    esp_err_t err = nvs_set_blob(handle, key, config, sizeof(*config));
    free(config);
    ESP_RETURN_ON_ERROR(err, TAG, "set %s", key);
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

esp_err_t bc250_config_reset_admin_password(char output[17])
{
    if (output == NULL) return ESP_ERR_INVALID_ARG;
    output[0] = '\0';
    bc250_config_t *next = malloc(sizeof(*next));
    bc250_config_t *active = malloc(sizeof(*active));
    if (next == NULL || active == NULL) {
        free(next);
        free(active);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_write_lock, portMAX_DELAY);
    char password[17];
    random_password(password);
    *next = s_config;
    bc250_config_set_admin_password(next, password);
    finalize_config(next);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = read_blob(handle, KEY_ACTIVE, active);
        if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_INVALID_CRC) {
            *active = s_config;
            err = ESP_OK;
        }
        if (err == ESP_OK) {
            memcpy(active->admin_salt, next->admin_salt, sizeof(next->admin_salt));
            memcpy(active->admin_hash, next->admin_hash, sizeof(next->admin_hash));
            finalize_config(active);
            err = nvs_set_blob(handle, KEY_ACTIVE, active, sizeof(*active));
            if (err == ESP_OK) {
                esp_err_t pending_err = read_blob(handle, KEY_PENDING, active);
                bool pending_exists = pending_err == ESP_OK;
                if (s_using_pending && !pending_exists) {
                    *active = *next;
                    pending_exists = true;
                } else if (pending_err != ESP_OK && pending_err != ESP_ERR_NVS_NOT_FOUND &&
                           pending_err != ESP_ERR_INVALID_CRC) {
                    err = pending_err;
                }
                if (err == ESP_OK && pending_exists) {
                    memcpy(active->admin_salt, next->admin_salt, sizeof(next->admin_salt));
                    memcpy(active->admin_hash, next->admin_hash, sizeof(next->admin_hash));
                    finalize_config(active);
                    err = nvs_set_blob(handle, KEY_PENDING, active, sizeof(*active));
                }
            }
            if (err == ESP_OK) err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err == ESP_OK) {
        s_config = *next;
        strlcpy(output, password, 17);
    }
    memset(password, 0, sizeof(password));
    xSemaphoreGive(s_write_lock);
    free(next);
    free(active);
    return err;
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
    psu_i2c_defaults(&config->psu_i2c);
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
    if (s_write_lock == NULL) {
        s_write_lock = xSemaphoreCreateMutex();
        if (s_write_lock == NULL) return ESP_ERR_NO_MEM;
    }
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
    ESP_LOGW(TAG, "First boot admin password: %s", s_config.ap_password);
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

esp_err_t bc250_config_save_pending(const bc250_config_t *config)
{
    char error[128];
    ESP_RETURN_ON_ERROR(bc250_config_validate(config, error, sizeof(error)), TAG, "%s", error);
    xSemaphoreTake(s_write_lock, portMAX_DELAY);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = write_blob(handle, KEY_PENDING, config);
        nvs_close(handle);
    }
    xSemaphoreGive(s_write_lock);
    return err;
}

esp_err_t bc250_config_mark_healthy(void)
{
    xSemaphoreTake(s_write_lock, portMAX_DELAY);
    if (!s_using_pending) {
        xSemaphoreGive(s_write_lock);
        return ESP_OK;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = write_blob(handle, KEY_ACTIVE, &s_config);
        if (err == ESP_OK) {
            nvs_erase_key(handle, KEY_PENDING);
            nvs_erase_key(handle, KEY_PENDING_BOOTS);
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err == ESP_OK) s_using_pending = false;
    xSemaphoreGive(s_write_lock);
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
    cJSON *recommended = cJSON_AddArrayToObject(root, "recommended_gpios");
    cJSON *blocked = cJSON_AddArrayToObject(root, "blocked_gpios");
    for (int gpio = 0; gpio <= 31; ++gpio) {
        if (bc250_config_pin_is_safe(gpio)) cJSON_AddItemToArray(recommended, cJSON_CreateNumber(gpio));
        if (bc250_config_pin_is_blocked(gpio)) cJSON_AddItemToArray(blocked, cJSON_CreateNumber(gpio));
    }
    cJSON_AddNumberToObject(root, "sense_on_ms", config->sense_on_ms);
    cJSON_AddNumberToObject(root, "sense_off_ms", config->sense_off_ms);
    cJSON_AddNumberToObject(root, "ble_scan_interval_ms", config->ble_scan_interval_ms);
    cJSON_AddNumberToObject(root, "ble_scan_window_ms", config->ble_scan_window_ms);
    cJSON_AddNumberToObject(root, "ble_absent_ms", config->ble_absent_ms);
    cJSON_AddNumberToObject(root, "zigbee_channel", config->zigbee_channel);
    cJSON_AddStringToObject(root, "zigbee_manufacturer", config->zigbee_manufacturer);
    cJSON_AddStringToObject(root, "zigbee_model", config->zigbee_model);
    cJSON *psu = cJSON_AddObjectToObject(root, "psu_i2c");
    cJSON_AddBoolToObject(psu, "enabled", config->psu_i2c.enabled);
    cJSON_AddNumberToObject(psu, "sda_gpio", config->psu_i2c.sda_gpio);
    cJSON_AddNumberToObject(psu, "scl_gpio", config->psu_i2c.scl_gpio);
    cJSON_AddNumberToObject(psu, "address", config->psu_i2c.address);
    cJSON_AddNumberToObject(psu, "poll_interval_ms", config->psu_i2c.poll_interval_ms);

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
    if (item != NULL) {
        if (cJSON_IsString(item) && strcmp(item->valuestring, "wifi") == 0) {
            config->radio_profile = BC250_RADIO_WIFI;
        } else if (cJSON_IsString(item) && strcmp(item->valuestring, "zigbee") == 0) {
            config->radio_profile = BC250_RADIO_ZIGBEE;
        } else {
            snprintf(error, error_size, "radio_profile must be wifi or zigbee");
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
    }
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

    cJSON *psu = cJSON_GetObjectItemCaseSensitive(root, "psu_i2c");
    if (cJSON_IsObject(psu)) {
        cJSON *v = cJSON_GetObjectItemCaseSensitive(psu, "enabled");
        if (cJSON_IsBool(v)) config->psu_i2c.enabled = cJSON_IsTrue(v);
        v = cJSON_GetObjectItemCaseSensitive(psu, "sda_gpio");
        if (v != NULL) {
            if (!valid_integer(v, BC250_GPIO_DISABLED, 31)) goto invalid_numeric;
            config->psu_i2c.sda_gpio = (int8_t)v->valueint;
        }
        v = cJSON_GetObjectItemCaseSensitive(psu, "scl_gpio");
        if (v != NULL) {
            if (!valid_integer(v, BC250_GPIO_DISABLED, 31)) goto invalid_numeric;
            config->psu_i2c.scl_gpio = (int8_t)v->valueint;
        }
        v = cJSON_GetObjectItemCaseSensitive(psu, "address");
        if (v != NULL) {
            if (!valid_integer(v, 0x58, 0x5f)) goto invalid_numeric;
            config->psu_i2c.address = (uint8_t)v->valueint;
        }
        uint32_t interval = config->psu_i2c.poll_interval_ms;
        if (!patch_u32(psu, "poll_interval_ms", &interval, 60000)) goto invalid_numeric;
        config->psu_i2c.poll_interval_ms = interval;
    }

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
