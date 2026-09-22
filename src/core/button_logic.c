#include "button_logic.h"

#include <string.h>

static uint64_t elapsed(uint64_t now, uint64_t then)
{
    return now >= then ? now - then : 0;
}

void bc250_button_logic_init(bc250_button_logic_t *button, bool initial_raw, uint64_t now_ms)
{
    if (button == NULL) return;
    memset(button, 0, sizeof(*button));
    button->raw = initial_raw;
    button->stable = initial_raw;
    button->raw_changed_ms = now_ms;
    if (initial_raw) button->pressed_ms = now_ms;
}

bc250_button_gesture_t bc250_button_logic_update(bc250_button_logic_t *button, bool raw,
                                                  uint64_t now_ms, uint32_t debounce_ms,
                                                  uint32_t double_press_ms,
                                                  uint32_t long_press_ms)
{
    if (button == NULL) return BC250_GESTURE_NONE;
    if (raw != button->raw) {
        button->raw = raw;
        button->raw_changed_ms = now_ms;
    }
    if (button->stable != raw && elapsed(now_ms, button->raw_changed_ms) >= debounce_ms) {
        button->stable = raw;
        if (raw) {
            button->pressed_ms = now_ms;
            button->long_sent = false;
        } else if (!button->long_sent) {
            if (button->clicks < UINT8_MAX) button->clicks++;
            button->released_ms = now_ms;
        }
    }
    if (button->stable && !button->long_sent &&
        elapsed(now_ms, button->pressed_ms) >= long_press_ms) {
        button->long_sent = true;
        button->clicks = 0;
        return BC250_GESTURE_LONG;
    }
    if (!button->stable && button->clicks > 0 &&
        elapsed(now_ms, button->released_ms) >= double_press_ms) {
        bc250_button_gesture_t result = button->clicks >= 2 ? BC250_GESTURE_DOUBLE
                                                            : BC250_GESTURE_SHORT;
        button->clicks = 0;
        return result;
    }
    return BC250_GESTURE_NONE;
}

