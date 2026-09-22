#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool present;
    uint64_t last_seen_ms;
} bc250_presence_logic_t;

void bc250_presence_logic_init(bc250_presence_logic_t *presence);
bool bc250_presence_logic_seen(bc250_presence_logic_t *presence, uint64_t now_ms);
bool bc250_presence_logic_expire(bc250_presence_logic_t *presence, uint64_t now_ms,
                                 uint32_t absent_ms);

