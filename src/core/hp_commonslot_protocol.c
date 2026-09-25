#include "hp_commonslot_protocol.h"

const uint8_t bc250_hp_commonslot_registers[BC250_HP_COMMONSLOT_REGISTER_COUNT] = {
    0x08, 0x0a, 0x0e, 0x10, 0x1c, 0x1e,
};

void bc250_hp_commonslot_read_command(uint8_t address, uint8_t reg, uint8_t command[2])
{
    command[0] = reg;
    command[1] = (uint8_t)(0U - ((uint8_t)(address << 1) + reg));
}

bool bc250_hp_commonslot_decode_reply(const uint8_t reply[3], uint16_t *raw)
{
    if ((uint8_t)(reply[0] + reply[1] + reply[2]) != 0) return false;
    *raw = (uint16_t)reply[0] | ((uint16_t)reply[1] << 8);
    return true;
}

float bc250_hp_commonslot_scale(unsigned index, uint16_t raw)
{
    static const float divisors[BC250_HP_COMMONSLOT_REGISTER_COUNT] = {
        32.0f, 128.0f, 256.0f, 128.0f, 32.0f, 1.0f,
    };
    return index < BC250_HP_COMMONSLOT_REGISTER_COUNT ? raw / divisors[index] : 0.0f;
}
