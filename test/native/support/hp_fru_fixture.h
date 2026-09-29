#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void fixture_checksum(uint8_t *data, size_t size)
{
    uint8_t sum = 0;
    for (size_t i = 0; i + 1 < size; ++i) sum = (uint8_t)(sum + data[i]);
    data[size - 1] = (uint8_t)(0U - sum);
}

// Product/board text matches the DPS-1200FB EEPROM documented by slundell/dps_charger.
static void hp_fru_fixture(uint8_t data[256])
{
    memset(data, 0, 256);
    data[0] = 1; data[3] = 1; data[4] = 5; data[5] = 14;
    fixture_checksum(data, 8);
    const char *board[] = {"", "", "", "441830-001", "10/10/08"};
    const char *product[] = {"DELTA", "HP PROLIANT SERVER PS     ", "437572-B21", "01", "5AMJQ0D4DXO3LU", "", ""};
    for (unsigned area = 0; area < 2; ++area) {
        size_t offset = area ? 0x28 : 8;
        size_t length = area ? 72 : 32;
        data[offset] = 1; data[offset + 1] = (uint8_t)(length / 8); data[offset + 2] = 25;
        const char **fields = area ? product : board;
        size_t position = offset + (area ? 3 : 6);
        for (unsigned i = 0; i < (area ? 7U : 5U); ++i) {
            size_t n = strlen(fields[i]);
            data[position++] = (uint8_t)(0xc0 | n);
            memcpy(data + position, fields[i], n);
            position += n;
        }
        data[position] = 0xc1;
        fixture_checksum(data + offset, length);
    }
    uint8_t *record = data + 0x70;
    record[0] = 0; record[1] = 0x82; record[2] = 24;
    record[5] = 0xb0; record[6] = 0x04; // Rated 1200 W.
    uint8_t sum = 0;
    for (unsigned i = 0; i < 24; ++i) sum = (uint8_t)(sum + record[5 + i]);
    record[3] = (uint8_t)(0U - sum);
    fixture_checksum(record, 5);
}
