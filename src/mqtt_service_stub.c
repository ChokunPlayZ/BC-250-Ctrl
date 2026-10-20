#include "mqtt_service.h"

esp_err_t bc250_mqtt_service_start(const bc250_config_t *config)
{
    return config == NULL ? ESP_ERR_INVALID_ARG : ESP_OK;
}
bool bc250_mqtt_is_connected(void) { return false; }
void bc250_mqtt_refresh(void) {}
