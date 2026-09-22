#pragma once

#include <stdbool.h>

#include "config_store.h"
#include "esp_err.h"

esp_err_t bc250_gpio_init_output(const bc250_output_config_t *config);
esp_err_t bc250_gpio_init_input(const bc250_input_config_t *config);
void bc250_gpio_write(const bc250_output_config_t *config, bool active);
bool bc250_gpio_read(const bc250_input_config_t *config);

