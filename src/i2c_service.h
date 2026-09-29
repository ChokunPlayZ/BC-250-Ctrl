#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define BC250_I2C_MAX_SCAN_ADDRESSES 112
#define BC250_I2C_ERROR_SIZE 224

typedef struct {
    size_t scanned_addresses;
    size_t timeout_count;
} bc250_i2c_scan_progress_t;

// One shared bus for I2C clients. Repeated starts must use the same pins.
esp_err_t bc250_i2c_service_start(int sda_gpio, int scl_gpio);
esp_err_t bc250_i2c_service_add_device(uint8_t address, uint32_t speed_hz,
                                       i2c_master_dev_handle_t *device);
void bc250_i2c_service_remove_device(i2c_master_dev_handle_t device);

// Hold the bus across multi-step device transactions.
// Returns ESP_ERR_TIMEOUT if another client holds the bus for over one second.
esp_err_t bc250_i2c_service_lock(void);
void bc250_i2c_service_unlock(void);

// Caller must hold the bus lock for recovery and GPIO diagnostics.
esp_err_t bc250_i2c_service_recover(void);
void bc250_i2c_service_describe_error(esp_err_t err, uint8_t address, const char *operation,
                                      char *error, size_t error_size);

// Scan the active bus, or temporarily open one on the supplied pins.
esp_err_t bc250_i2c_service_scan(int sda_gpio, int scl_gpio, uint8_t *addresses,
                                size_t capacity, size_t *count, char *error, size_t error_size,
                                bc250_i2c_scan_progress_t *progress);
