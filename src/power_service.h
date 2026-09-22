#pragma once

#include <stdbool.h>

#include "config_store.h"
#include "core/power_logic.h"
#include "esp_err.h"

esp_err_t bc250_power_service_start(const bc250_config_t *config);
bool bc250_power_service_request(bc250_power_action_t action);
bc250_power_state_t bc250_power_service_state(void);
bool bc250_power_service_sensed_on(void);
bc250_power_outputs_t bc250_power_service_outputs(void);

