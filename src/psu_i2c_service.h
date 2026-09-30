#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config_store.h"
#include "core/hp_commonslot_identity.h"
#include "core/hp_commonslot_protocol.h"
#include "esp_err.h"
#include "i2c_service.h"

typedef struct {
    bool available;
    uint8_t eeprom_address;
    char error[BC250_I2C_ERROR_SIZE];
    bc250_hp_commonslot_identity_t data;
} bc250_psu_i2c_identity_status_t;

typedef struct {
    bool enabled;
    bool available;
    char error[BC250_I2C_ERROR_SIZE];
    uint32_t age_ms;
    float input_voltage_v;
    float input_current_a;
    float output_voltage_v;
    float output_current_a;
    float internal_temperature_c;
    uint16_t fan_speed_raw;
    bc250_psu_i2c_identity_status_t identity;
} bc250_psu_i2c_status_t;

typedef struct {
    bool enabled;
    bool pic_read;
    bool pic_available;
    uint16_t pic_registers[BC250_HP_COMMONSLOT_REGISTER_COUNT];
    uint32_t pic_age_ms;
    bool eeprom_read;
    uint8_t eeprom_address;
    uint8_t eeprom[BC250_HP_EEPROM_SIZE];
    uint32_t eeprom_age_ms;
} bc250_psu_i2c_data_t;

esp_err_t bc250_psu_i2c_service_start(const bc250_config_t *config);
bc250_psu_i2c_status_t bc250_psu_i2c_service_status(void);
bc250_psu_i2c_data_t bc250_psu_i2c_service_data(void);
