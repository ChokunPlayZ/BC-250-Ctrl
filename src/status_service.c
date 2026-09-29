#include "status_service.h"

#include "ble_presence.h"
#include "cJSON.h"
#include "config_store.h"
#include "power_service.h"
#include "psu_i2c_service.h"
#include "wifi_service.h"
#include "zigbee_service.h"

char *bc250_status_json(void)
{
    bc250_power_outputs_t outputs = bc250_power_service_outputs();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", BC250_VERSION);
    cJSON_AddStringToObject(root, "power_state", bc250_power_state_name(bc250_power_service_state()));
    cJSON_AddBoolToObject(root, "sensed_on", bc250_power_service_sensed_on());
    cJSON_AddBoolToObject(root, "ps_on_active", outputs.ps_on);
    cJSON_AddBoolToObject(root, "power_button_active", outputs.power_button);
    cJSON_AddBoolToObject(root, "wifi_connected", bc250_wifi_is_connected());
    cJSON_AddStringToObject(root, "wifi_ip", bc250_wifi_ip_address());
    cJSON_AddBoolToObject(root, "config_ap", bc250_wifi_is_config_ap());
    cJSON_AddBoolToObject(root, "zigbee_started", bc250_zigbee_is_started());
    cJSON_AddBoolToObject(root, "zigbee_joined", bc250_zigbee_is_joined());
    bc250_psu_i2c_status_t psu_status = bc250_psu_i2c_service_status();
    cJSON *psu = cJSON_AddObjectToObject(root, "psu_i2c");
    cJSON_AddBoolToObject(psu, "enabled", psu_status.enabled);
    cJSON_AddBoolToObject(psu, "available", psu_status.available);
    if (psu_status.error[0]) cJSON_AddStringToObject(psu, "error", psu_status.error);
    if (psu_status.enabled) {
        const bc250_psu_i2c_identity_status_t *identity = &psu_status.identity;
        cJSON *info = cJSON_AddObjectToObject(psu, "identity");
        cJSON_AddBoolToObject(info, "available", identity->available);
        cJSON_AddNumberToObject(info, "eeprom_address", identity->eeprom_address);
        if (identity->error[0]) cJSON_AddStringToObject(info, "error", identity->error);
        if (identity->available) {
#define ADD_IDENTITY_TEXT(field) \
            if (identity->data.field[0]) cJSON_AddStringToObject(info, #field, identity->data.field)
            ADD_IDENTITY_TEXT(manufacturer);
            ADD_IDENTITY_TEXT(product_name);
            ADD_IDENTITY_TEXT(part_number);
            ADD_IDENTITY_TEXT(revision);
            ADD_IDENTITY_TEXT(serial_number);
            ADD_IDENTITY_TEXT(board_part_number);
#undef ADD_IDENTITY_TEXT
            if (identity->data.rated_capacity_w)
                cJSON_AddNumberToObject(info, "rated_capacity_w", identity->data.rated_capacity_w);
        }
    }
    if (psu_status.available) {
        cJSON_AddNumberToObject(psu, "age_ms", psu_status.age_ms);
        cJSON_AddNumberToObject(psu, "input_voltage_v", psu_status.input_voltage_v);
        cJSON_AddNumberToObject(psu, "input_current_a", psu_status.input_current_a);
        cJSON_AddNumberToObject(psu, "output_voltage_v", psu_status.output_voltage_v);
        cJSON_AddNumberToObject(psu, "output_current_a", psu_status.output_current_a);
        cJSON_AddNumberToObject(psu, "internal_temperature_f", psu_status.internal_temperature_f);
        cJSON_AddNumberToObject(psu, "fan_speed_raw", psu_status.fan_speed_raw);
    }
#ifdef CONFIG_BC250_OTA_ENABLED
    cJSON_AddBoolToObject(root, "ota_enabled", true);
#else
    cJSON_AddBoolToObject(root, "ota_enabled", false);
#endif
    cJSON *present = cJSON_AddArrayToObject(root, "ble_present");
    const bc250_config_t *config = bc250_config_get();
    for (int i = 0; i < config->ble_device_count; ++i) {
        if (bc250_ble_device_present(i)) cJSON_AddItemToArray(present, cJSON_CreateString(config->ble_devices[i].label));
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}
