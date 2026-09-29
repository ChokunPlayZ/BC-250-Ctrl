#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BC250_HP_EEPROM_SIZE 256
#define BC250_HP_IDENTITY_TEXT_SIZE 64

typedef struct {
    char manufacturer[BC250_HP_IDENTITY_TEXT_SIZE];
    char product_name[BC250_HP_IDENTITY_TEXT_SIZE];
    char part_number[BC250_HP_IDENTITY_TEXT_SIZE];
    char revision[BC250_HP_IDENTITY_TEXT_SIZE];
    char serial_number[BC250_HP_IDENTITY_TEXT_SIZE];
    char board_part_number[BC250_HP_IDENTITY_TEXT_SIZE];
    uint16_t rated_capacity_w; // Zero when no valid power-supply record is present.
} bc250_hp_commonslot_identity_t;

// Decode checksum-verified IPMI FRU areas, without assuming fixed string offsets.
// Unsupported text encodings are omitted. Output is cleared on failure.
bool bc250_hp_commonslot_decode_identity(const uint8_t *data, size_t size,
                                          bc250_hp_commonslot_identity_t *identity);
