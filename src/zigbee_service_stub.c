#include "zigbee_service.h"

esp_err_t bc250_zigbee_service_start(const bc250_config_t *config)
{
    return config == NULL ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bc250_zigbee_set_config_ap_active(bool active)
{
    (void)active;
    return ESP_OK;
}

esp_err_t bc250_zigbee_commission(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t bc250_zigbee_factory_reset(void) { return ESP_ERR_NOT_SUPPORTED; }
void bc250_zigbee_update_power_state(bc250_power_state_t state) { (void)state; }
void bc250_zigbee_update_psu_status(void) {}
bool bc250_zigbee_is_started(void) { return false; }
bool bc250_zigbee_is_joining(void) { return false; }
bool bc250_zigbee_is_joined(void) { return false; }
