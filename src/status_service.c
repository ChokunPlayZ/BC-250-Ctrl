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
