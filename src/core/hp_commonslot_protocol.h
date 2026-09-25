#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BC250_HP_COMMONSLOT_REGISTER_COUNT 6

extern const uint8_t bc250_hp_commonslot_registers[BC250_HP_COMMONSLOT_REGISTER_COUNT];

void bc250_hp_commonslot_read_command(uint8_t address, uint8_t reg, uint8_t command[2]);
bool bc250_hp_commonslot_decode_reply(const uint8_t reply[3], uint16_t *raw);
float bc250_hp_commonslot_scale(unsigned index, uint16_t raw);
