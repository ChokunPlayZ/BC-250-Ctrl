#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config_store.h"
#include "esp_err.h"

typedef struct {
    bool enabled;
    bool available;
    uint32_t age_ms;
    float input_voltage_v;
    float input_current_a;
    float output_voltage_v;
    float output_current_a;
    float internal_temperature_f;
    uint16_t fan_speed_raw;
} bc250_psu_i2c_status_t;

esp_err_t bc250_psu_i2c_service_start(const bc250_config_t *config);
bc250_psu_i2c_status_t bc250_psu_i2c_service_status(void);
