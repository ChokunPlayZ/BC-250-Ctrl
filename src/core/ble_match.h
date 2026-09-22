#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t address[6];
    int8_t rssi;
    const uint8_t *data;
    size_t data_length;
} bc250_ble_advertisement_t;

bool bc250_ble_match_advertisement(int matcher_type, const char *value, const char *mask,
                                   int8_t min_rssi, const bc250_ble_advertisement_t *advertisement);
void bc250_ble_format_address(const uint8_t address[6], char output[18]);
bool bc250_ble_extract_name(const uint8_t *data, size_t data_length, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

