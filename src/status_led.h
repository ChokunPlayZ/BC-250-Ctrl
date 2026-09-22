#pragma once

#include <stdbool.h>

#include "config_store.h"
#include "esp_err.h"

esp_err_t bc250_status_led_start(const bc250_config_t *config);
void bc250_status_led_set_config_mode(bool active);

