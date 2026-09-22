#include "ble_match.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum {
    MATCH_ADDRESS = 0,
    MATCH_NAME_EXACT = 1,
    MATCH_NAME_PREFIX = 2,
    MATCH_SERVICE_UUID = 3,
    MATCH_MANUFACTURER_DATA = 4,
};

typedef struct {
    uint8_t type;
    const uint8_t *value;
    size_t length;
} ad_field_t;

static bool next_field(const uint8_t *data, size_t length, size_t *offset, ad_field_t *field)
{
    if (*offset >= length) return false;
    uint8_t field_length = data[*offset];
    if (field_length == 0 || *offset + field_length >= length) return false;
    field->type = data[*offset + 1];
    field->value = &data[*offset + 2];
    field->length = field_length - 1;
    *offset += field_length + 1;
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t parse_hex(const char *text, uint8_t *output, size_t capacity)
{
    size_t count = 0;
    int high = -1;
    for (; text != NULL && *text; ++text) {
        int value = hex_value(*text);
        if (value < 0) continue;
        if (high < 0) {
            high = value;
        } else {
            if (count >= capacity) return 0;
            output[count++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    return high < 0 ? count : 0;
}

void bc250_ble_format_address(const uint8_t address[6], char output[18])
{
    snprintf(output, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             address[5], address[4], address[3], address[2], address[1], address[0]);
}

bool bc250_ble_extract_name(const uint8_t *data, size_t data_length, char *output, size_t output_size)
{
    if (output == NULL || output_size == 0) return false;
    output[0] = '\0';
    size_t offset = 0;
    ad_field_t field;
    while (next_field(data, data_length, &offset, &field)) {
        if (field.type == 0x08 || field.type == 0x09) {
            size_t size = field.length < output_size - 1 ? field.length : output_size - 1;
            memcpy(output, field.value, size);
            output[size] = '\0';
            return true;
        }
    }
    return false;
}

static bool match_address(const char *value, const bc250_ble_advertisement_t *advertisement)
{
    char address[18];
    bc250_ble_format_address(advertisement->address, address);
    return value != NULL && strcasecmp(value, address) == 0;
}

static bool match_name(const char *value, bool prefix, const bc250_ble_advertisement_t *advertisement)
{
    char name[64];
    if (value == NULL || !bc250_ble_extract_name(advertisement->data, advertisement->data_length,
                                                 name, sizeof(name))) {
        return false;
    }
    return prefix ? strncmp(name, value, strlen(value)) == 0 : strcmp(name, value) == 0;
}

static bool match_uuid(const char *value, const bc250_ble_advertisement_t *advertisement)
{
    uint8_t expected[16];
    size_t expected_length = parse_hex(value, expected, sizeof(expected));
    if (expected_length != 2 && expected_length != 16) return false;

    size_t offset = 0;
    ad_field_t field;
    while (next_field(advertisement->data, advertisement->data_length, &offset, &field)) {
        size_t width = 0;
        if (expected_length == 2 && (field.type == 0x02 || field.type == 0x03)) width = 2;
        if (expected_length == 16 && (field.type == 0x06 || field.type == 0x07)) width = 16;
        if (width == 0) continue;
        for (size_t position = 0; position + width <= field.length; position += width) {
            bool same = true;
            for (size_t i = 0; i < width; ++i) {
                if (field.value[position + i] != expected[width - i - 1]) {
                    same = false;
                    break;
                }
            }
            if (same) return true;
        }
    }
    return false;
}

static bool match_manufacturer(const char *value, const char *mask,
                               const bc250_ble_advertisement_t *advertisement)
{
    uint8_t expected[32];
    uint8_t parsed_mask[32];
    size_t expected_length = parse_hex(value, expected, sizeof(expected));
    size_t mask_length = parse_hex(mask, parsed_mask, sizeof(parsed_mask));
    if (expected_length == 0) return false;
    if (mask_length == 0) memset(parsed_mask, 0xff, expected_length);
    else if (mask_length != expected_length) return false;

    size_t offset = 0;
    ad_field_t field;
    while (next_field(advertisement->data, advertisement->data_length, &offset, &field)) {
        if (field.type != 0xff || field.length < expected_length) continue;
        bool same = true;
        for (size_t i = 0; i < expected_length; ++i) {
            if ((field.value[i] & parsed_mask[i]) != (expected[i] & parsed_mask[i])) {
                same = false;
                break;
            }
        }
        if (same) return true;
    }
    return false;
}

bool bc250_ble_match_advertisement(int matcher_type, const char *value, const char *mask,
                                   int8_t min_rssi, const bc250_ble_advertisement_t *advertisement)
{
    if (advertisement == NULL || advertisement->rssi < min_rssi) return false;
    switch (matcher_type) {
    case MATCH_ADDRESS: return match_address(value, advertisement);
    case MATCH_NAME_EXACT: return match_name(value, false, advertisement);
    case MATCH_NAME_PREFIX: return match_name(value, true, advertisement);
    case MATCH_SERVICE_UUID: return match_uuid(value, advertisement);
    case MATCH_MANUFACTURER_DATA: return match_manufacturer(value, mask, advertisement);
    default: return false;
    }
}
