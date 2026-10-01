#include "wifi_service.h"

esp_err_t bc250_wifi_service_start(const bc250_config_t *config, bool force_config_ap)
{
    (void)force_config_ap;
    return config == NULL ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t bc250_wifi_open_config_ap(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t bc250_wifi_open_setup_ap(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t bc250_wifi_close_config_ap(void) { return ESP_OK; }
esp_err_t bc250_wifi_pair_zigbee(void) { return ESP_ERR_NOT_SUPPORTED; }
bool bc250_wifi_is_config_ap(void) { return false; }
bool bc250_wifi_is_connected(void) { return false; }
const char *bc250_wifi_ip_address(void) { return "0.0.0.0"; }
