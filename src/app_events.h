#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config_store.h"
#include "core/power_logic.h"
#include "esp_err.h"

typedef enum {
    BC250_EVENT_BUTTON_ACTION = 0,
    BC250_EVENT_BLE_ARRIVED,
    BC250_EVENT_POWER_STATE_CHANGED,
    BC250_EVENT_OPEN_CONFIG_AP,
    BC250_EVENT_ZIGBEE_COMMISSION,
    BC250_EVENT_ZIGBEE_RESET,
} bc250_app_event_type_t;

typedef struct {
    bc250_app_event_type_t type;
    union {
        bc250_button_action_t button_action;
        bc250_power_state_t power_state;
        uint8_t ble_device_index;
    } data;
} bc250_app_event_t;

esp_err_t bc250_app_events_init(void);
bool bc250_app_event_post(const bc250_app_event_t *event, uint32_t timeout_ms);
bool bc250_app_event_receive(bc250_app_event_t *event, uint32_t timeout_ms);

