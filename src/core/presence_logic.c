#include "presence_logic.h"

#include <stddef.h>

void bc250_presence_logic_init(bc250_presence_logic_t *presence)
{
    if (presence == NULL) return;
    presence->present = false;
    presence->last_seen_ms = 0;
}

bool bc250_presence_logic_seen(bc250_presence_logic_t *presence, uint64_t now_ms)
{
    if (presence == NULL) return false;
    bool arrived = !presence->present;
    presence->present = true;
    presence->last_seen_ms = now_ms;
    return arrived;
}

bool bc250_presence_logic_expire(bc250_presence_logic_t *presence, uint64_t now_ms,
                                 uint32_t absent_ms)
{
    if (presence == NULL || !presence->present || now_ms < presence->last_seen_ms ||
        now_ms - presence->last_seen_ms < absent_ms) {
        return false;
    }
    presence->present = false;
    return true;
}

