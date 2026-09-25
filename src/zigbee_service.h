#pragma once

#include <stdbool.h>

#include "config_store.h"
#include "core/power_logic.h"
#include "esp_err.h"

esp_err_t bc250_zigbee_service_start(const bc250_config_t *config);
esp_err_t bc250_zigbee_commission(void);
esp_err_t bc250_zigbee_factory_reset(void);
void bc250_zigbee_update_power_state(bc250_power_state_t state);
void bc250_zigbee_update_psu_status(void);
bool bc250_zigbee_is_started(void);
bool bc250_zigbee_is_joined(void);
