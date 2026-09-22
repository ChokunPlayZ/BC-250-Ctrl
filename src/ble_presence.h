#pragma once

#include <stdbool.h>

#include "config_store.h"
#include "esp_err.h"

esp_err_t bc250_ble_presence_start(const bc250_config_t *config);
esp_err_t bc250_ble_start_learning(uint32_t duration_ms);
char *bc250_ble_scan_results_json(void);
bool bc250_ble_device_present(unsigned index);

