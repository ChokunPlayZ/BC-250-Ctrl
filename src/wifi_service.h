#pragma once

#include <stdbool.h>

#include "config_store.h"
#include "esp_err.h"

esp_err_t bc250_wifi_service_start(const bc250_config_t *config, bool force_config_ap);
esp_err_t bc250_wifi_open_config_ap(void);
bool bc250_wifi_is_config_ap(void);
bool bc250_wifi_is_connected(void);
const char *bc250_wifi_ip_address(void);

