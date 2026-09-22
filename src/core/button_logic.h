#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BC250_GESTURE_NONE = 0,
    BC250_GESTURE_SHORT,
    BC250_GESTURE_DOUBLE,
    BC250_GESTURE_LONG,
} bc250_button_gesture_t;

typedef struct {
    bool raw;
    bool stable;
    bool long_sent;
    uint8_t clicks;
    uint64_t raw_changed_ms;
    uint64_t pressed_ms;
    uint64_t released_ms;
} bc250_button_logic_t;

void bc250_button_logic_init(bc250_button_logic_t *button, bool initial_raw, uint64_t now_ms);
bc250_button_gesture_t bc250_button_logic_update(bc250_button_logic_t *button, bool raw,
                                                  uint64_t now_ms, uint32_t debounce_ms,
                                                  uint32_t double_press_ms,
                                                  uint32_t long_press_ms);

