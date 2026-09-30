#pragma once

#include <stdbool.h>

#include "config_store.h"
#include "esp_err.h"

esp_err_t bc250_wifi_service_start(const bc250_config_t *config, bool force_config_ap);
esp_err_t bc250_wifi_open_config_ap(void);
/* Opens the setup portal in every profile; closes after five client-free minutes. */
esp_err_t bc250_wifi_open_setup_ap(void);
esp_err_t bc250_wifi_close_config_ap(void);
/* Blocking: close Wi-Fi, wait for Zigbee, then pair. Run outside the HTTP task.
 * Reopens setup on startup failure; never erases an existing Zigbee network. */
esp_err_t bc250_wifi_pair_zigbee(void);
bool bc250_wifi_is_config_ap(void);
bool bc250_wifi_is_connected(void);
const char *bc250_wifi_ip_address(void);
