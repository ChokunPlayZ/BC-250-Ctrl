#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define BC250_I2C_MAX_SCAN_ADDRESSES 112

// One shared bus for I2C clients. Repeated starts must use the same pins.
esp_err_t bc250_i2c_service_start(int sda_gpio, int scl_gpio);
esp_err_t bc250_i2c_service_add_device(uint8_t address, uint32_t speed_hz,
                                       i2c_master_dev_handle_t *device);
void bc250_i2c_service_remove_device(i2c_master_dev_handle_t device);

// Hold the bus across multi-step device transactions.
void bc250_i2c_service_lock(void);
void bc250_i2c_service_unlock(void);

// Scan the active bus, or temporarily open one on the supplied pins.
esp_err_t bc250_i2c_service_scan(int sda_gpio, int scl_gpio, uint8_t *addresses,
                                size_t capacity, size_t *count);
