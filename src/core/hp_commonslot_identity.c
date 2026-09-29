#include "hp_commonslot_identity.h"

#include <string.h>

static bool checksum_valid(const uint8_t *data, size_t size)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < size; ++i) sum = (uint8_t)(sum + data[i]);
    return sum == 0;
}

static void copy_ascii(const uint8_t *data, size_t size, char output[BC250_HP_IDENTITY_TEXT_SIZE])
{
    for (size_t i = 0; i < size; ++i) {
        if (data[i] < 0x20 || data[i] > 0x7e) return;
    }
    while (size > 0 && data[size - 1] == ' ') --size;
    memcpy(output, data, size);
    output[size] = '\0';
}

static bool decode_area(const uint8_t *data, size_t size, size_t offset, bool board,
                         bc250_hp_commonslot_identity_t *identity)
{
    if (offset < 8 || offset > size || size - offset < 8 || data[offset] != 1) return false;
    size_t length = (size_t)data[offset + 1] * 8;
    size_t field_offset = board ? 6 : 3;
    if (length < 8 || length > size - offset || !checksum_valid(data + offset, length)) return false;
    const uint8_t *area = data + offset;
    char fields[7][BC250_HP_IDENTITY_TEXT_SIZE] = {{0}};
    unsigned field = 0;
    bool terminated = false;
    while (field_offset < length - 1) {
        uint8_t descriptor = area[field_offset++];
        if (descriptor == 0xc1) {
            terminated = true;
            break;
        }
        size_t bytes = descriptor & 0x3f;
        if (bytes > length - 1 - field_offset) return false;
        // Serial numbers and part numbers use English encoding regardless of area language.
        bool english = area[2] == 0 || area[2] == 25 ||
                       (board ? field >= 2 && field <= 4 : field == 4 || field == 6);
        if (field < 7 && (descriptor >> 6) == 3 && english)
            copy_ascii(area + field_offset, bytes, fields[field]);
        field_offset += bytes;
        ++field;
    }
    if (!terminated || field < (board ? 5U : 7U)) return false;
    while (field_offset < length - 1) {
        if (area[field_offset++] != 0) return false;
    }
    char *destinations[] = {
        identity->manufacturer, identity->product_name, identity->part_number,
        identity->revision, identity->serial_number,
    };
    if (board) {
        destinations[2] = identity->serial_number;
        destinations[3] = identity->board_part_number;
        destinations[4] = NULL; // Board FRU file ID is not a manufacturing date.
    }
    for (unsigned i = 0; i < 5; ++i) {
        if (destinations[i] != NULL && fields[i][0])
            memcpy(destinations[i], fields[i], BC250_HP_IDENTITY_TEXT_SIZE);
    }
    return true;
}

static uint16_t decode_capacity(const uint8_t *data, size_t size, size_t offset)
{
    if (offset < 8) return 0;
    while (offset <= size && size - offset >= 5) {
        const uint8_t *record = data + offset;
        size_t length = record[2];
        if ((record[1] & 0x0f) != 2 || !checksum_valid(record, 5) || length > size - offset - 5) return 0;
        uint8_t sum = record[3];
        for (size_t i = 0; i < length; ++i) sum = (uint8_t)(sum + record[5 + i]);
        if (sum != 0) return 0;
        if (record[0] == 0x00 && length == 24) {
            uint16_t watts = (uint16_t)record[5] | ((uint16_t)record[6] << 8);
            return (watts & 0xf000) == 0 ? watts : 0;
        }
        if (record[1] & 0x80) break;
        offset += 5 + length;
    }
    return 0;
}

bool bc250_hp_commonslot_decode_identity(const uint8_t *data, size_t size,
                                          bc250_hp_commonslot_identity_t *identity)
{
    if (identity == NULL) return false;
    memset(identity, 0, sizeof(*identity));
    if (data == NULL || size < 8 || data[0] != 1 || !checksum_valid(data, 8)) return false;
    bool board_valid = decode_area(data, size, (size_t)data[3] * 8, true, identity);
    bool product_valid = decode_area(data, size, (size_t)data[4] * 8, false, identity);
    identity->rated_capacity_w = decode_capacity(data, size, (size_t)data[5] * 8);
    if ((board_valid || product_valid) && (identity->manufacturer[0] || identity->product_name[0] ||
        identity->part_number[0] || identity->serial_number[0] || identity->board_part_number[0])) return true;
    memset(identity, 0, sizeof(*identity));
    return false;
}
