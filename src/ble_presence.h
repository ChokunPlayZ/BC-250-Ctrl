#pragma once

#include <stdbool.h>

#include "config_store.h"
#include "esp_err.h"

esp_err_t bc250_ble_presence_start(const bc250_config_t *config);
esp_err_t bc250_ble_start_learning(uint32_t duration_ms);
#define BC250_BLE_SCAN_RESULT_COUNT 24
typedef struct {
    char address[18];
    char name[64];
    uint8_t address_type;
    int8_t rssi;
    bool address_may_rotate;
} bc250_ble_scan_result_t;
bool bc250_ble_scan_result(unsigned index, bc250_ble_scan_result_t *result);
char *bc250_ble_scan_results_json(void);
bool bc250_ble_device_present(unsigned index);
